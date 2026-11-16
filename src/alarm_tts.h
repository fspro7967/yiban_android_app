#ifndef ALARMTTS_H
#define ALARMTTS_H

#include <QObject>
#include <QString>
#include <QList>
#include <QPair>

class SpeechSynthesizer;

// 到点播报的"离线配音室"。
//
// 背景：到点提醒原来靠系统 TTS（Java TextToSpeech）念文案，实测不少国产 ROM
// 根本没有中文 TTS 引擎——这正是"到点没有语音播报"的直接原因。仓库里本来就
// 带了离线模型（sherpa-onnx + vits-icefall-zh-aishell3，见 third_party/ 与
// android/assets/models/），所以播报改为：
//
//   排程时（应用活着）→ 用离线模型把每条提醒的播报文案**预渲染成 WAV**
//   到点时（Qt 进程多半已死）→ AlarmService 直接 MediaPlayer 放这个 WAV
//
// 为什么到点时不现场跑模型：闹钟广播拉起的是冷进程，等 29 MB 的 ONNX 加载完
// 可能要十几秒，老人早就把手机扔一边了。预渲染把所有开销挪到排程那一刻，
// 到点只剩"读文件、出声"，跟系统铃声一样可靠。
//
// 线程模型：模型提取 + 加载在 QtConcurrent 后台线程（SpeechSynthesizer::init
// 的硬要求）；渲染排队在本对象所在线程逐条进行——sherpa 同一时刻只允许一个
// 合成在跑，SpeechSynthesizer 已经用代次记账保证，这里只需保证不并发排队。
class AlarmTtsRenderer : public QObject
{
    Q_OBJECT
public:
    explicit AlarmTtsRenderer(QObject *parent = nullptr);

    // 提取 APK 内模型资产并加载（幂等；内部自己放后台线程，重复调用无害）。
    // 就绪后发出 readyChanged(true)。
    void start();

    bool isReady() const;

    // 渲染一条播报音频。完成（或失败）后发 rendered(id, path)；
    // path 为空串表示没能渲染（模型未就绪/合成失败），调用方到点时走系统 TTS 兜底。
    // 未就绪时任务会排队，就绪后按序补渲染；队列上限见 kMaxQueue。
    void render(qint64 alarmId, const QString &announcement);

    // 某条提醒的播报 WAV 的稳定落盘路径（无论是否已渲染）。
    // 文件按 occurrenceId 命名：重排时同一 occurrence 覆盖重渲染，
    // 删除的提醒残留的旧文件由 prune() 统一回收。
    static QString wavPath(qint64 alarmId);

signals:
    void readyChanged(bool ready);
    void rendered(qint64 alarmId, const QString &path);

private:
    void pumpNext();
    void finishCurrent(const QString &path);
    bool writeWav(const QString &path, const QByteArray &pcm, int sampleRate) const;

    SpeechSynthesizer *m_tts = nullptr;
    bool m_started = false;
    bool m_ready = false;
    bool m_busy = false;
    qint64 m_currentId = 0;
    QList<QPair<qint64, QString>> m_queue;   // 待渲染（alarmId, 播报文案）
    QByteArray m_pcm;                        // 当前这一条的合成结果（int16 单声道）
};

#endif // ALARMTTS_H
