#include "device_vm.h"

#include "app_core.h"
#include "facade_errors.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTextToSpeech>

DeviceVM::DeviceVM(QObject *parent)
    : QObject(parent)
{
    m_tts = new QTextToSpeech(this);
    connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State) {
        emit audioChanged();
    });

    const QStringList engines = m_tts->availableEngines();
    if (!engines.isEmpty()) {
        m_tts->setLocale(QLocale(QLocale::Chinese, QLocale::China));
        m_tts->setRate(-0.15);
    }
    connect(AppCore::instance(), &AppCore::toast, this, &DeviceVM::toast);
}

bool DeviceVM::audioReady() const
{
    return m_tts && !m_tts->availableEngines().isEmpty();
}

bool DeviceVM::speaking() const
{
    return m_tts && m_tts->state() == QTextToSpeech::Speaking;
}

QStringList DeviceVM::permissions() const
{
    // demo 里全部视为已授权；B2 接入时改成真实查询结果。
    return {QStringLiteral("通知"), QStringLiteral("麦克风"), QStringLiteral("电话")};
}

bool DeviceVM::nightQuietNow() const
{
    const int hour = QTime::currentTime().hour();
    return m_nightQuiet && (hour >= 22 || hour < 7);
}

void DeviceVM::setVoiceEnabled(bool on)
{
    if (m_voiceEnabled == on)
        return;
    m_voiceEnabled = on;
    if (!on)
        stopSpeaking();
    emit voiceChanged();
}

void DeviceVM::setNightQuiet(bool on)
{
    if (m_nightQuiet == on)
        return;
    m_nightQuiet = on;
    emit voiceChanged();
}

void DeviceVM::pair(const QString &code, const QString &role)
{
    static const QRegularExpression sixDigits(QStringLiteral("\\d{6}"));
    if (!sixDigits.match(code.trimmed()).hasMatch()) {
        emit commandRejected(QStringLiteral("bad_pair_code"),
                             facadeErrorText(QStringLiteral("bad_pair_code")));
        return;
    }
    m_paired = true;
    m_role = role.isEmpty() ? QStringLiteral("elder") : role;
    emit pairingChanged();
    emit toast(QStringLiteral("已和 %1 配对").arg(AppCore::instance()->caregiverName()));
}

void DeviceVM::unpair()
{
    if (!m_paired)
        return;
    m_paired = false;
    emit pairingChanged();
    emit toast(QStringLiteral("已取消配对"));
}

void DeviceVM::speak(const QString &text, const QString &tag)
{
    Q_UNUSED(tag)
    if (text.trimmed().isEmpty() || !m_voiceEnabled)
        return;
    if (nightQuietNow())
        return;
    if (!audioReady()) {
        emit commandRejected(QStringLiteral("audio_unavailable"),
                             facadeErrorText(QStringLiteral("audio_unavailable")));
        return;
    }
    m_tts->stop();
    m_tts->say(text);
}

void DeviceVM::stopSpeaking()
{
    if (m_tts)
        m_tts->stop();
}

void DeviceVM::sos(int level)
{
    AppCore::instance()->sos();
    emit sosTriggered(level);
}

void DeviceVM::enterKiosk()
{
    if (m_kioskActive)
        return;
    m_kioskActive = true;
    emit kioskChanged();
}

void DeviceVM::exitKiosk()
{
    if (!m_kioskActive)
        return;
    m_kioskActive = false;
    emit kioskChanged();
}

void DeviceVM::resetDemo()
{
    AppCore::instance()->resetDemo();
    m_paired = false;
    m_kioskActive = false;
    emit pairingChanged();
    emit kioskChanged();
}
