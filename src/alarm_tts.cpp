#include "alarm_tts.h"

#include "assetextractor.h"
#include "speechsynthesizer.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent>

namespace {

// 与 android/assets/models/ 下的目录同名：assets:/models/tts-... → 私有目录同名
constexpr char kAssetDirName[] = "tts-vits-icefall-zh-aishell3";

// SpeechSynthesizer::init 需要的文件（见其 kModelFileName/kLexiconFileName/
// kTokensFileName/kRuleFstNames）。assets:/ 引擎不能列目录，清单必须显式给出。
const QStringList kModelFiles = {
    QStringLiteral("model.onnx"),
    QStringLiteral("lexicon.txt"),
    QStringLiteral("tokens.txt"),
    QStringLiteral("phone.fst"),
    QStringLiteral("date.fst"),
    QStringLiteral("number.fst"),
};

// 渲染排队上限。今天最多十几条提醒，正常永远到不了；
// 兜底防止模型一直没就绪时队列无限吃内存。
constexpr int kMaxQueue = 32;

QString modelRootDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/models");
}

QString renderDir()
{
    return modelRootDir() + QStringLiteral("/alarm_tts");
}

} // namespace

AlarmTtsRenderer::AlarmTtsRenderer(QObject *parent)
    : QObject(parent)
{
}

QString AlarmTtsRenderer::wavPath(qint64 alarmId)
{
    return renderDir() + QStringLiteral("/alarm_%1.wav").arg(alarmId);
}

void AlarmTtsRenderer::start()
{
    if (m_started)
        return;
    m_started = true;

    m_tts = new SpeechSynthesizer(this);

    connect(m_tts, &SpeechSynthesizer::audioChunk, this,
            [this](const QByteArray &pcm, int) {
                if (m_busy)
                    m_pcm.append(pcm);   // 只收当前这一条；空闲时来的一律丢弃
            });

    connect(m_tts, &SpeechSynthesizer::sentenceFinished, this, [this] {
        if (!m_busy)
            return;
        const QString path = wavPath(m_currentId);
        // 写盘失败也算"没渲染出来"：path 传空，到点走系统 TTS 兜底，不中断排程。
        finishCurrent(writeWav(path, m_pcm, m_tts->sampleRate()) ? path : QString());
    });

    connect(m_tts, &SpeechSynthesizer::failed, this, [this](const QString &reason) {
        qWarning() << "AlarmTts: 合成失败" << reason;
        if (m_busy) {
            // 这一条没渲染出来，放行走兜底；队列里剩下的继续试
            finishCurrent(QString());
        } else if (!m_ready) {
            // init 失败（模型损坏/存储不足）：放掉全部排队任务，别让它们干等
            const auto pending = m_queue;
            m_queue.clear();
            for (const auto &item : pending)
                emit rendered(item.first, QString());
        }
    });

    // 模型提取 + ONNX 加载都放后台线程：
    // SpeechSynthesizer::init 的注释明确要求"同步阻塞，调用方必须放到后台线程执行"，
    // 主线程上解析 29 MB 的 ONNX 会把界面卡住好几秒。
    // init 内部失败时发 failed()（跨线程 queued 回主线程），那边负责清队列。
    const QString assetDir = QStringLiteral("assets:/models/") + QLatin1String(kAssetDirName);
    const QString targetDir = modelRootDir() + QLatin1Char('/') + QLatin1String(kAssetDirName);

    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher->result()) {
            m_ready = true;
            emit readyChanged(true);
            qDebug() << "AlarmTts: 离线播报就绪";
            pumpNext();
        }
    });
    watcher->setFuture(QtConcurrent::run([this, assetDir, targetDir]() -> bool {
        QString err;
        if (!AssetExtractor::ensureExtracted(assetDir, targetDir, kModelFiles, &err)) {
            qWarning() << "AlarmTts: 模型资产提取失败:" << err;
            return false;
        }
        return m_tts->init(targetDir);
    }));
}

bool AlarmTtsRenderer::isReady() const
{
    return m_ready && m_tts && m_tts->isReady();
}

void AlarmTtsRenderer::render(qint64 alarmId, const QString &announcement)
{
    if (announcement.trimmed().isEmpty()) {
        emit rendered(alarmId, QString());
        return;
    }
    // 已就绪且空闲就直接渲染，不绕队列
    if (isReady() && !m_busy) {
        m_busy = true;
        m_currentId = alarmId;
        m_pcm.clear();
        if (!m_tts->synthesizeSentence(announcement)) {
            // 理论上到不了（ready 且非空白文本）；保险起见照常放行
            finishCurrent(QString());
        }
        return;
    }

    // 未就绪/正在渲染：排队。同一条重排过的先摘掉，以最新文案为准。
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).first == alarmId) {
            m_queue.removeAt(i);
            break;
        }
    }
    if (m_queue.size() >= kMaxQueue) {
        const auto dropped = m_queue.takeFirst();
        emit rendered(dropped.first, QString());
    }
    m_queue.append({alarmId, announcement});
    pumpNext();
}

void AlarmTtsRenderer::pumpNext()
{
    if (m_busy || !isReady() || m_queue.isEmpty())
        return;
    const auto item = m_queue.takeFirst();
    m_busy = true;
    m_currentId = item.first;
    m_pcm.clear();
    if (!m_tts->synthesizeSentence(item.second))
        finishCurrent(QString());
}

void AlarmTtsRenderer::finishCurrent(const QString &path)
{
    m_busy = false;
    m_pcm.clear();
    emit rendered(m_currentId, path);
    pumpNext();
}

bool AlarmTtsRenderer::writeWav(const QString &path, const QByteArray &pcm,
                                int sampleRate) const
{
    if (pcm.isEmpty() || sampleRate <= 0)
        return false;

    QDir().mkpath(renderDir());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    // 标准 44 字节 RIFF/WAVE 头：单声道、16-bit PCM。
    // 采样率以模型实际输出为准（vits-icefall-zh-aishell3 = 8000 Hz）。
    // 字段一律小端，WAV 规范如此，手写字节比 QDataStream 直白。
    const quint32 dataLen = static_cast<quint32>(pcm.size());
    const quint16 channels = 1;
    const quint16 bitsPerSample = 16;
    const quint32 sampleRateU = static_cast<quint32>(sampleRate);
    const quint16 blockAlign = channels * bitsPerSample / 8;               // = 2
    const quint32 byteRate = sampleRateU * blockAlign;
    const quint32 riffSize = 36 + dataLen;

    auto put16 = [&file](quint16 v) -> bool {
        unsigned char b[2] = { quint8(v & 0xFF), quint8(v >> 8) };
        return file.write(reinterpret_cast<const char *>(b), 2) == 2;
    };
    auto put32 = [&file](quint32 v) -> bool {
        unsigned char b[4] = { quint8(v & 0xFF), quint8((v >> 8) & 0xFF),
                               quint8((v >> 16) & 0xFF), quint8((v >> 24) & 0xFF) };
        return file.write(reinterpret_cast<const char *>(b), 4) == 4;
    };

    bool ok = file.write("RIFF", 4) == 4
        && put32(riffSize)
        && file.write("WAVEfmt ", 8) == 8
        && put32(16)                    // fmt 块长度
        && put16(1)                     // PCM
        && put16(channels)
        && put32(sampleRateU)
        && put32(byteRate)
        && put16(blockAlign)
        && put16(bitsPerSample)
        && file.write("data", 4) == 4
        && put32(dataLen)
        && file.write(pcm.constData(), pcm.size()) == pcm.size();

    file.close();
    if (!ok) {
        file.remove();
        return false;
    }
    return true;
}
