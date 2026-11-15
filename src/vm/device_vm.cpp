#include "device_vm.h"

#include "app_core.h"
#include "connection_probe.h"
#include "facade_errors.h"

#include <QDateTime>
#include <QNetworkInformation>
#include <QRegularExpression>
#include <QTextToSpeech>
#include <QTimer>
#include <QUrl>

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
    connect(AppCore::instance(), &AppCore::demoModeChanged,
            this, &DeviceVM::demoModeChanged);

    // 联网状态直接听系统的：Android 上是 ConnectivityManager，桌面是 NetworkManager。
    // 后端加载不了（极少数定制系统、无 NetworkManager 的桌面）时保持 unknown，不假装在线。
    m_networkInfoReady = QNetworkInformation::loadBackendByFeatures(
                             QNetworkInformation::Feature::Reachability)
                         || QNetworkInformation::loadDefaultBackend();
    if (QNetworkInformation *info = m_networkInfoReady ? QNetworkInformation::instance() : nullptr) {
        connect(info, &QNetworkInformation::reachabilityChanged, this, &DeviceVM::networkChanged);
        connect(info, &QNetworkInformation::transportMediumChanged, this, &DeviceVM::networkChanged);
        connect(info, &QNetworkInformation::isBehindCaptivePortalChanged,
                this, &DeviceVM::networkChanged);
    }

    // 进出安静时段时要通知界面，并打断正在进行的播报。
    m_quietActive = nightQuietNow();
    m_quietTimer = new QTimer(this);
    m_quietTimer->setInterval(30 * 1000);
    connect(m_quietTimer, &QTimer::timeout, this, &DeviceVM::checkQuietWindow);
    m_quietTimer->start();
}

void DeviceVM::checkQuietWindow()
{
    const bool active = nightQuietNow();
    if (active == m_quietActive)
        return;
    m_quietActive = active;
    if (active)
        stopSpeaking();
    emit voiceChanged();
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
    AppCore::instance()->logOperation(QStringLiteral("设置"),
                                      on ? QStringLiteral("打开语音播报") : QStringLiteral("关闭语音播报"));
    emit voiceChanged();
}

void DeviceVM::setNightQuiet(bool on)
{
    if (m_nightQuiet == on)
        return;
    m_nightQuiet = on;
    m_quietActive = nightQuietNow();
    if (m_quietActive)
        stopSpeaking();
    AppCore::instance()->logOperation(QStringLiteral("设置"),
                                      on ? QStringLiteral("打开夜间安静") : QStringLiteral("关闭夜间安静"));
    emit voiceChanged();
}

bool DeviceVM::demoMode() const
{
    return AppCore::instance()->demoMode();
}

void DeviceVM::setDemoMode(bool on)
{
    AppCore::instance()->setDemoMode(on);
}

QString DeviceVM::networkStatus() const
{
    const QNetworkInformation *info = m_networkInfoReady ? QNetworkInformation::instance() : nullptr;
    if (!info)
        return QStringLiteral("unknown");
    switch (info->reachability()) {
    case QNetworkInformation::Reachability::Online:
        return info->isBehindCaptivePortal() ? QStringLiteral("portal") : QStringLiteral("online");
    case QNetworkInformation::Reachability::Site:
    case QNetworkInformation::Reachability::Local:
        return info->isBehindCaptivePortal() ? QStringLiteral("portal") : QStringLiteral("limited");
    case QNetworkInformation::Reachability::Disconnected:
        return QStringLiteral("offline");
    case QNetworkInformation::Reachability::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString DeviceVM::networkType() const
{
    const QNetworkInformation *info = m_networkInfoReady ? QNetworkInformation::instance() : nullptr;
    if (!info || !info->supports(QNetworkInformation::Feature::TransportMedium))
        return QString();
    switch (info->transportMedium()) {
    case QNetworkInformation::TransportMedium::WiFi:
        return QStringLiteral("Wi-Fi");
    case QNetworkInformation::TransportMedium::Cellular:
        return QStringLiteral("移动数据");
    case QNetworkInformation::TransportMedium::Ethernet:
        return QStringLiteral("有线网络");
    case QNetworkInformation::TransportMedium::Bluetooth:
        return QStringLiteral("蓝牙");
    case QNetworkInformation::TransportMedium::Unknown:
        break;
    }
    return QString();
}

bool DeviceVM::saveNetwork(const QString &serverUrl)
{
    const QString trimmed = serverUrl.trimmed();
    if (!trimmed.startsWith(QStringLiteral("https://"))
        && !trimmed.startsWith(QStringLiteral("http://"))) {
        emit commandRejected(QStringLiteral("bad_server_url"),
                             QStringLiteral("服务器地址要以 https:// 开头"));
        return false;
    }
    if (trimmed.length() < 12 || trimmed.length() > 200) {
        emit commandRejected(QStringLiteral("bad_server_url"),
                             QStringLiteral("服务器地址长度不对"));
        return false;
    }
    m_serverUrl = trimmed;
    AppCore::instance()->logOperation(QStringLiteral("管理"),
                                      QStringLiteral("保存服务器地址：%1").arg(trimmed));
    emit networkChanged();
    emit toast(QStringLiteral("网络设置已保存（演示版）"));
    return true;
}

bool DeviceVM::testingConnection() const
{
    return m_probe && m_probe->busy();
}

void DeviceVM::testConnection(const QString &serverUrl)
{
    if (testingConnection())
        return;

    const QString trimmed = serverUrl.trimmed();
    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
        emit connectionTested(false, QStringLiteral("服务器地址要以 https:// 开头，后面跟上域名"));
        return;
    }

    if (!m_probe) {
        m_probe = new ConnectionProbe(this);
        connect(m_probe, &ConnectionProbe::busyChanged, this, &DeviceVM::testingConnectionChanged);
        connect(m_probe, &ConnectionProbe::finished, this,
                [this](bool reachable, int, int elapsedMs, const QString &, const QString &message) {
            const QString text = reachable
                ? QStringLiteral("%1，用时 %2 毫秒").arg(message).arg(elapsedMs)
                : message;
            AppCore::instance()->logOperation(
                QStringLiteral("管理"), QStringLiteral("测试连接 %1：%2").arg(m_probeUrl, text));
            emit connectionTested(reachable, text);
        });
    }
    m_probeUrl = trimmed;
    m_probe->start(url);
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
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("完成配对"));
    emit pairingChanged();
    emit toast(QStringLiteral("已和 %1 配对").arg(AppCore::instance()->caregiverName()));
}

void DeviceVM::unpair()
{
    if (!m_paired)
        return;
    m_paired = false;
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("取消配对"));
    emit pairingChanged();
    emit toast(QStringLiteral("已取消配对"));
}

void DeviceVM::speak(const QString &text, const QString &tag)
{
    if (text.trimmed().isEmpty() || !m_voiceEnabled)
        return;
    if (nightQuietNow() && !tag.startsWith(QStringLiteral("manual")))
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
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("进入老人机模式"));
    emit kioskChanged();
}

void DeviceVM::exitKiosk()
{
    if (!m_kioskActive)
        return;
    m_kioskActive = false;
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("退出老人机模式"));
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
