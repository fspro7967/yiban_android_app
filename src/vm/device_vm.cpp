#include "device_vm.h"

#include "app_core.h"
#include "board_vm.h"
#include "connection_probe.h"
#include "app_settings.h"
#include "facade_errors.h"
#include "http_client.h"
#include "platform.h"
#ifdef Q_OS_ANDROID
#include "alarm_tts.h"   // 离线播报渲染器（src/ 下，仅 Android 编译）
#endif

#include <QDateTime>
#include <QNetworkInformation>
#include <QDebug>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QTextToSpeech>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

// 通知 id 与 Android 侧保持同一套算法（AlarmReceiver::notificationId）：
// 同一个 occurrence 无论从闹钟来还是从进程内来，都覆盖同一条通知。
int notificationIdFor(qint64 occurrenceId)
{
    return static_cast<int>(occurrenceId % 1000000LL);
}

QString occText(const QVariantMap &occ)
{
    QString text = occ.value(QStringLiteral("dose")).toString();
    const QString note = occ.value(QStringLiteral("note")).toString();
    if (!note.isEmpty())
        text = text.isEmpty() ? note : text + QStringLiteral(" · ") + note;
    if (text.isEmpty())
        text = QStringLiteral("到点该做了");
    return text;
}

} // namespace

DeviceVM::DeviceVM(QObject *parent)
    : QObject(parent)
{
    AppSettings *settings = AppSettings::instance();
    m_voiceEnabled = settings->voiceEnabled();
    m_nightQuiet = settings->nightQuiet();
    m_speechRate = settings->speechRate();
    m_speechPitch = settings->speechPitch();
    // TTS 可能在 Java 侧先初始化（闹钟服务），这里把持久化的语速/音调同步下去。
    Platform::setSpeechRate(m_speechRate);
    Platform::setSpeechPitch(m_speechPitch);
    m_paired = settings->paired();
    m_role = settings->role();
    m_serverUrl = settings->serverUrl();
    m_hasToken = !settings->token().trimmed().isEmpty();

    m_http = new NetworkHttpClient(this);
    connect(m_http, &HttpClient::reply, this,
            [this](quint64 id, int status, const QJsonObject &body) {
                if (id != 0 && id == m_testRequestId) {
                    handleTestReply(id, status, body);
                    return;
                }
                handlePairReply(id, status, body);
            });
    connect(m_http, &HttpClient::failed, this, [this](quint64 id, const QString &code) {
        if (id != 0 && id == m_testRequestId) {
            handleTestFailed(id, code);
            return;
        }
        handlePairFailed(id, code);
    });

    // Android 上不建 qtts：到点响铃时 Qt 进程可能已经被系统回收，
    // 而且多数国产 ROM 上 QTextToSpeech 的 availableEngines() 是空的
    // —— 这正是"到点没有语音播报"的直接原因。Android 一律走系统 TTS
    // （Platform::speak → Java TextToSpeech，进程不在前台也能出声）。
    if (!Platform::isAndroid()) {
        m_tts = new QTextToSpeech(this);
        connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State) {
            emit audioChanged();
        });
        if (!m_tts->availableEngines().isEmpty()) {
            m_tts->setLocale(QLocale(QLocale::Chinese, QLocale::China));
            m_tts->setRate(m_speechRate - 1.0); // B2-7：0.9 → -0.1，与 Android 倍率对齐
        }
    }

    m_speakTimer = new QTimer(this);
    m_speakTimer->setSingleShot(true);
    connect(m_speakTimer, &QTimer::timeout, this, [this] {
        m_speaking = false;
        emit audioChanged();
    });

    AppCore *core = AppCore::instance();
    connect(core, &AppCore::toast, this, &DeviceVM::toast);
    connect(core, &AppCore::demoModeChanged, this, &DeviceVM::demoModeChanged);
    connect(core, &AppCore::reminderDue, this, &DeviceVM::onReminderDue);
    // 计划/状态一变就重排闹钟：确认、推迟、删除、新增都要让系统里的闹钟跟着变，
    // 否则会出现"已经确认了，锁屏十点还是响"。
    connect(core, &AppCore::occurrencesChanged, this,
            [this] { QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms); });
    connect(core, &AppCore::plansChanged, this,
            [this] { QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms); });
    // X-1（方案 B）：升级链由 B2 的 DeviceVM 订阅，不依赖提醒页是否打开。
    // AppCore::advanceEscalation 只在等级真正变化时 emit，escalate(id) 本身不再 emit，
    // 因此这里只认 level==3 且只调用一次 sos(3)；QML 按约定不调，避免拨两次。
    connect(core, &AppCore::escalationChanged, this, [this](qint64, int level) {
        if (level == 3)
            QTimer::singleShot(0, this, [this] { sos(3); });
    });
    // B2-6：板子呼救键和手动 SOS 走同一条路径（拨号/短信/云端 escalate）。
    connect(BoardVM::instance(), &BoardVM::boardSosRequested, this,
            [this] { sos(1); });

    m_permissionCache = Platform::grantedLabels();
    m_permissionDiagnostics = Platform::permissionDiagnostics();
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

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(2000);
    connect(m_tickTimer, &QTimer::timeout, this, &DeviceVM::tick);
    m_tickTimer->start();

    // 启动后先排一次今天剩下的闹钟（AppCore 的 occurrence 是构造完才有的）
    QTimer::singleShot(1500, this, &DeviceVM::rescheduleAlarms);

    // Android 13+ 通知权限：第一次启动问一次，不问就永远收不到到点提醒
    if (Platform::isAndroid() && !Platform::hasPermission(Platform::Permission::Notifications)) {
        QTimer::singleShot(2500, this, &DeviceVM::requestNotificationPermission);
    }

    // 启动时打一行诊断：真机上"闹钟不响"九成能从这行看出原因
    if (Platform::isAndroid()) {
        qInfo().noquote() << "[平台诊断]" << Platform::diagnostics();
    }

    // 离线播报：把仓库自带的 TTS 模型提取出来加载，给每条提醒预渲染播报 WAV。
    // 组里 review 的反馈——很多测试机没有中文 TTS 引擎，qtts 和系统 TTS 都念不出声；
    // 离线模型跟铃声一样必然能放，是"没有语音播报"的根治方案。
    // 仅 Android 编译（桌面没有 sherpa 库，见 CMakeLists 的 SPEECH_SOURCES）。
#ifdef Q_OS_ANDROID
    m_alarmTts = new AlarmTtsRenderer(this);
    connect(m_alarmTts, &AlarmTtsRenderer::rendered, this,
            [](qint64 id, const QString &path) {
                qDebug().noquote() << "AlarmTts: 提醒" << id
                                   << (path.isEmpty() ? "渲染失败（到点走系统 TTS 兜底）"
                                                      : "已渲染 " + path);
            });
    m_alarmTts->start();
#endif
}

// ---------------------------------------------------------------- 周期检查

void DeviceVM::tick()
{
    const bool quiet = nightQuietNow();
    if (quiet != m_quietActive) {
        m_quietActive = quiet;
        if (quiet)
            stopSpeaking();
        emit voiceChanged();
    }
    refreshPermissions();
    refreshToken();
    // 联网状态不用轮询：QNetworkInformation 的信号直接接到 networkChanged（见构造函数）

    // 网络日志：2 秒同步一次就够看，管理页打开时不会漏掉任何一次交换
    const QStringList log = NetworkLog::recent();
    if (log != m_networkLog) {
        m_networkLog = log;
        emit networkLogChanged();
    }
}

void DeviceVM::refreshPermissions()
{
    const QStringList current = Platform::grantedLabels();
    const QVariantList diagnostics = Platform::permissionDiagnostics();
    if (current == m_permissionCache && diagnostics == m_permissionDiagnostics)
        return;
    m_permissionCache = current;
    m_permissionDiagnostics = diagnostics;
    emit permissionChanged();
}

void DeviceVM::refreshToken()
{
    const bool hasToken = !AppSettings::instance()->token().trimmed().isEmpty();
    if (hasToken == m_hasToken)
        return;
    m_hasToken = hasToken;
    emit tokenChanged();
}

// ---------------------------------------------------------------- 属性

bool DeviceVM::audioReady() const
{
    // Android 侧问系统 TTS 是否可用（与 qtts 无关）
    if (Platform::isAndroid())
        return Platform::canSpeak();
    return m_tts && !m_tts->availableEngines().isEmpty();
}

bool DeviceVM::speaking() const
{
    if (Platform::isAndroid())
        return m_speaking;
    return m_tts && m_tts->state() == QTextToSpeech::Speaking;
}

QStringList DeviceVM::permissions() const
{
    return m_permissionCache;
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
    AppSettings::instance()->setVoiceEnabled(on);
    if (!on)
        stopSpeaking();
    AppCore::instance()->logOperation(QStringLiteral("设置"),
                                      on ? QStringLiteral("打开语音播报") : QStringLiteral("关闭语音播报"));
    emit voiceChanged();
    // 已经排进系统的闹钟还带着旧的"要不要播报"标记，重排一遍才生效
    QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms);
}

void DeviceVM::setSpeechRate(double rate)
{
    if (rate < 0.5 || rate > 2.0 || qFuzzyCompare(m_speechRate, rate))
        return;
    m_speechRate = rate;
    AppSettings::instance()->setSpeechRate(rate);
    Platform::setSpeechRate(rate); // Android 已初始化的 TTS 立即变速
    if (m_tts)
        m_tts->setRate(rate - 1.0); // 桌面 QTextToSpeech 的 rate 是 -1.0~1.0
    AppCore::instance()->logOperation(QStringLiteral("设置"),
                                      QStringLiteral("播报语速改为 %1").arg(rate));
    emit voiceChanged();
    // 到点播报走预渲染的离线 WAV，语速变了要重新渲染才能"下一条立即生效"。
    QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms);
}

void DeviceVM::setSpeechPitch(double pitch)
{
    if (pitch < 0.5 || pitch > 2.0 || qFuzzyCompare(m_speechPitch, pitch))
        return;
    m_speechPitch = pitch;
    AppSettings::instance()->setSpeechPitch(pitch);
    Platform::setSpeechPitch(pitch);
    AppCore::instance()->logOperation(QStringLiteral("设置"),
                                      QStringLiteral("播报音调改为 %1").arg(pitch));
    emit voiceChanged();
    QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms);
}

QStringList DeviceVM::chineseVoices() const
{
    return Platform::chineseVoices();
}

bool DeviceVM::deviceOwner() const
{
    return Platform::isDeviceOwner();
}

bool DeviceVM::chineseVoiceMissing() const
{
    return Platform::chineseVoiceMissing();
}

void DeviceVM::setNightQuiet(bool on)
{
    if (m_nightQuiet == on)
        return;
    m_nightQuiet = on;
    AppSettings::instance()->setNightQuiet(on);
    m_quietActive = nightQuietNow();
    if (m_quietActive)
        stopSpeaking();
    AppCore::instance()->logOperation(QStringLiteral("设置"),
                                      on ? QStringLiteral("打开夜间安静") : QStringLiteral("关闭夜间安静"));
    emit voiceChanged();
    // 夜间安静的"只亮屏"标记要写进每条闹钟里，开关一变就必须重排
    QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms);

    // 白天打开这个开关时界面上看不出任何变化，容易被当成"开关没用"。
    // 这里明说一句什么时候生效，省掉一次"夜间安静无效"的误报。
    if (on && !m_quietActive) {
        emit toast(QStringLiteral("夜间安静已打开，22:00 起自动生效"));
    } else if (on) {
        emit toast(QStringLiteral("夜间安静已生效：到点只亮屏，不出声"));
    }
}

bool DeviceVM::demoMode() const
{
    return AppCore::instance()->demoMode();
}

void DeviceVM::setDemoMode(bool on)
{
    AppCore::instance()->setDemoMode(on);
    QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms);
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
    const QString trimmed = normalizeServerUrl(serverUrl);
    if (trimmed.isEmpty()) {
        emit commandRejected(QStringLiteral("bad_server_url"),
                             QStringLiteral("服务器地址要以 http:// 或 https:// 开头，且长度在 12–200 之间"));
        return false;
    }

    m_serverUrl = trimmed;
    AppCore::instance()->logOperation(QStringLiteral("管理"),
                                      QStringLiteral("保存服务器地址：%1").arg(trimmed));
    AppSettings::instance()->setServerUrl(trimmed);
    emit networkChanged();
    emit toast(QStringLiteral("服务器地址已保存：") + trimmed);
    return true;
}

bool DeviceVM::saveToken(const QString &token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        AppSettings::instance()->clearToken();
        AppSettings::instance()->setPairing(false, QString());
        m_hasToken = false;
        emit tokenChanged();
        emit pairingChanged();
        emit toast(QStringLiteral("令牌已清除，请重新配对"));
        return true;
    }
    if (trimmed.length() < 8 || trimmed.length() > 256) {
        emit commandRejected(QStringLiteral("bad_token"),
                             QStringLiteral("令牌长度不对，请复制完整的 ses-… 令牌"));
        return false;
    }

    AppSettings::instance()->setToken(trimmed);
    // 新令牌 = 新会话：游标归零，下一轮全量重拉，避免旧游标漏数据。
    AppSettings::instance()->setStateSeq(0);
    AppSettings::instance()->setPlansSeq(0);
    AppSettings::instance()->setLastSyncTs(0);
    AppSettings::instance()->setPairing(true, m_role);
    m_paired = true;
    m_hasToken = true;
    emit tokenChanged();
    emit pairingChanged();
    emit toast(QStringLiteral("令牌已保存，点\"测试连接\"确认能不能收到数据"));
    return true;
}

bool DeviceVM::testingConnection() const
{
    return (m_probe && m_probe->busy()) || m_testRequestId != 0;
}

void DeviceVM::finishTest(bool ok, const QString &message)
{
    AppCore::instance()->logOperation(
        QStringLiteral("管理"), QStringLiteral("测试连接 %1：%2").arg(m_probeUrl, message));
    emit connectionTested(ok, message);
}

void DeviceVM::testConnection(const QString &serverUrl)
{
    if (testingConnection())
        return;

    const QString trimmed = serverUrl.trimmed();
    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
        emit connectionTested(false,
                              QStringLiteral("服务器地址要以 http:// 或 https:// 开头，后面跟上域名或 IP"));
        return;
    }

    if (!m_probe) {
        m_probe = new ConnectionProbe(this);
        connect(m_probe, &ConnectionProbe::busyChanged, this, &DeviceVM::testingConnectionChanged);
        connect(m_probe, &ConnectionProbe::finished, this,
                [this](bool reachable, int, int elapsedMs, const QString &, const QString &message) {
            if (!reachable) {
                finishTest(false, message);
                return;
            }
            // 第二步：测的就是同步用的地址、手上又有令牌，就带令牌拉一次 /state。
            // 只探连通的话，令牌过期也会报"连接正常"，联调时分不清是没发出去还是被拒了。
            if (m_http && m_hasToken && normalizeServerUrl(m_probeUrl) == m_serverUrl) {
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("since"), QStringLiteral("0"));
                m_testRequestId = m_http->getJson(QStringLiteral("/state"), query);
                if (m_testRequestId != 0) {
                    emit testingConnectionChanged();
                    return;
                }
            }
            finishTest(true, QStringLiteral("%1，用时 %2 毫秒").arg(message).arg(elapsedMs));
        });
    }
    m_probeUrl = trimmed;
    m_probe->start(url);
}

void DeviceVM::handleTestReply(quint64 requestId, int status, const QJsonObject &body)
{
    if (requestId != m_testRequestId)
        return;
    m_testRequestId = 0;
    emit testingConnectionChanged();

    if (status >= 200 && status < 300) {
        const int count = body.value(QStringLiteral("occurrences")).toArray().size();
        finishTest(true,
                   QStringLiteral("连接正常：服务器可达、令牌有效，下发 %1 条提醒状态").arg(count));
        return;
    }
    if (status == 401) {
        finishTest(false, QStringLiteral("服务器可达，但令牌无效：请重新配对或换一个令牌"));
        return;
    }
    const QString detail = body.value(QStringLiteral("detail")).toString();
    finishTest(false,
               QStringLiteral("服务器可达，但同步接口返回 %1%2")
                   .arg(status)
                   .arg(detail.isEmpty() ? QString() : QStringLiteral("：") + detail));
}

void DeviceVM::handleTestFailed(quint64 requestId, const QString &code)
{
    if (requestId != m_testRequestId)
        return;
    m_testRequestId = 0;
    emit testingConnectionChanged();
    finishTest(false,
               code == QStringLiteral("offline")
                   ? QStringLiteral("服务器可达，但同步请求没发出去：设备显示离线")
                   : QStringLiteral("服务器可达，但同步请求失败：地址/端口不对或请求超时"));
}

// ---------------------------------------------------------------- 配对

void DeviceVM::pair(const QString &code, const QString &role)
{
    static const QRegularExpression sixDigits(QStringLiteral("\\d{6}"));
    if (!sixDigits.match(code.trimmed()).hasMatch()) {
        emit commandRejected(QStringLiteral("bad_pair_code"),
                             facadeErrorText(QStringLiteral("bad_pair_code")));
        return;
    }
    if (m_pairing || m_paired)
        return;

    const QString targetRole = role.isEmpty() ? QStringLiteral("elder") : role;

    // 后端还没部署时也要能演示：先按本机配对，联网后再补传。
    if (!m_http || !m_http->isOnline()) {
        m_role = targetRole;
        finishPair(true, targetRole, QStringLiteral("offline"));
        return;
    }

    QJsonObject body;
    // CareMate 后端读 pair_code，参考服务端读 code：两个键都带上，双端兼容
    body.insert(QStringLiteral("pair_code"), code.trimmed());
    body.insert(QStringLiteral("code"), code.trimmed());
    body.insert(QStringLiteral("role"), targetRole);
    body.insert(QStringLiteral("device_id"), AppSettings::instance()->deviceId());

    m_pairing = true;
    m_pairRequestId = m_http->postJson(QStringLiteral("/pair"), body);

    // 兜底：请求万一没回来（代理吞包、服务端挂住），20 秒后解除"配对中"，
    // 让人还能再试一次，而不是一直没反应。
    const quint64 pending = m_pairRequestId;
    QTimer::singleShot(20000, this, [this, pending] {
        if (m_pairing && m_pairRequestId == pending) {
            m_pairing = false;
            m_pairRequestId = 0;
            emit commandRejected(QStringLiteral("offline"),
                                 QStringLiteral("网络不给力，配对没成功，请再试一次"));
        }
    });
}

void DeviceVM::handlePairReply(quint64 requestId, int status, const QJsonObject &body)
{
    if (requestId == 0 || requestId != m_pairRequestId)
        return;

    if (status < 200 || status >= 300) {
        if (status == 429) {
            // CareMate 配对限速：同一来源 10 分钟最多 10 次
            finishPair(false, m_role,
                       QStringLiteral("配对尝试太频繁，请 10 分钟后再试"));
            return;
        }
        const QString detail = body.value(QStringLiteral("detail")).toString();
        if (status == 404 && detail.contains(QStringLiteral("pair_code"))) {
            finishPair(false, m_role,
                       QStringLiteral("配对码不存在，请找管理员要新的 6 位配对码"));
            return;
        }
        // 401/404/409 对用户来说都是"这个码配不上"，但带上服务端原因好排查。
        finishPair(false, m_role,
                   detail.isEmpty() ? facadeErrorText(QStringLiteral("bad_pair_code"))
                                    : detail);
        return;
    }

    const QString token = body.value(QStringLiteral("token")).toString();
    if (token.trimmed().isEmpty()) {
        // 200 却没给令牌：不能标记配对成功，否则之后每次同步都是 401。
        finishPair(false, m_role, QStringLiteral("服务器没有返回令牌，请稍后重试"));
        return;
    }
    AppSettings::instance()->setToken(token);
    m_hasToken = true;
    emit tokenChanged();

    // 新令牌 = 新会话：游标归零，下一轮从服务端全量拉计划与状态
    AppSettings::instance()->setStateSeq(0);
    AppSettings::instance()->setPlansSeq(0);
    AppSettings::instance()->setLastSyncTs(0);

    // 云端带回的老人/家属档案：写进 AppCore，QML 三个页面都读它
    AppCore::instance()->setProfile(
        body.value(QStringLiteral("elder_name")).toString(),
        body.value(QStringLiteral("caregiver_name")).toString(),
        body.value(QStringLiteral("caregiver_phone")).toString());

    const QString role = body.value(QStringLiteral("role")).toString();
    finishPair(true, role.isEmpty() ? m_role : role, QString());
}

void DeviceVM::handlePairFailed(quint64 requestId, const QString &code)
{
    if (requestId == 0 || requestId != m_pairRequestId)
        return;

    if (code == QStringLiteral("offline")) {
        // 演示路径：没网也让人能走完配对流程，界面不该卡在"配对中"
        finishPair(true, m_role, QStringLiteral("offline"));
        return;
    }
    finishPair(false, m_role, facadeErrorText(QStringLiteral("bad_pair_code")));
}

void DeviceVM::finishPair(bool ok, const QString &role, const QString &reason)
{
    m_pairing = false;
    m_pairRequestId = 0;

    if (!ok) {
        emit commandRejected(QStringLiteral("bad_pair_code"),
                             reason.isEmpty() ? facadeErrorText(QStringLiteral("bad_pair_code"))
                                              : reason);
        return;
    }

    m_paired = true;
    if (!role.isEmpty())
        m_role = role;
    AppSettings::instance()->setPairing(true, m_role);
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("完成配对"));
    emit pairingChanged();

    const QString name = AppCore::instance()->caregiverName();
    const QString who = name.isEmpty() ? QStringLiteral("家人") : name;
    if (reason == QStringLiteral("offline")) {
        emit toast(QStringLiteral("已在本机配对（离线），联网后自动补传"));
    } else {
        emit toast(QStringLiteral("已和 %1 配对").arg(who));
    }

    // 配对是这个应用唯一的"初始化"时刻：在这里要电话/短信权限最自然，
    // 也避免老人第一次按呼救时才弹框（那时候已经来不及解释了）。
    if (Platform::isAndroid()) {
        if (!Platform::hasPermission(Platform::Permission::Phone)) {
            QTimer::singleShot(600, this, [this] {
                Platform::requestPermission(Platform::Permission::Phone);
            });
        }
        if (!Platform::hasPermission(Platform::Permission::Sms)) {
            // 系统一次只处理一个权限弹窗，错开一点，否则第二个请求会被丢掉
            QTimer::singleShot(1600, this, [this] {
                Platform::requestPermission(Platform::Permission::Sms);
            });
        }
    }
}

void DeviceVM::unpair()
{
    if (!m_paired)
        return;
    m_paired = false;
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("取消配对"));
    m_role = QStringLiteral("elder");
    AppSettings::instance()->setPairing(false, m_role);
    AppSettings::instance()->clearToken();
    emit pairingChanged();
    emit toast(QStringLiteral("已取消配对"));
}

// ---------------------------------------------------------------- 语音

void DeviceVM::speak(const QString &text, const QString &tag)
{
    if (text.trimmed().isEmpty() || !m_voiceEnabled)
        return;

    const bool manual = tag.startsWith(QStringLiteral("manual"));
    const bool emergency = tag.startsWith(QStringLiteral("sos"));
    // 夜间安静只拦"到点自动播报"：用户主动点的和呼救播报必须出声，
    // 否则半夜发病按呼救反而没声音，这是安全事故。
    if (nightQuietNow() && !manual && !emergency)
        return;

    if (Platform::isAndroid()) {
        // 闹钟服务已经念过这句了（全屏界面被顶起来时 QML 会再调一次 speak），
        // 20 秒内不重复，否则同一句话连念两遍像卡带。用户主动点的不受限制。
        if (!manual && Platform::wasSpokenRecently(20000))
            return;
        if (!Platform::canSpeak()) {
            emit commandRejected(QStringLiteral("audio_unavailable"),
                                 facadeErrorText(QStringLiteral("audio_unavailable")));
            return;
        }
        Platform::speak(text);
        m_speaking = true;
        emit audioChanged();
        // 系统 TTS 没有回调，按字数估一个时长把 speaking 复位（200ms/字，1.5–15s）
        m_speakTimer->start(qBound(1500, int(text.length()) * 200, 15000));
        return;
    }

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
    if (Platform::isAndroid()) {
        Platform::stopSpeaking();
        if (m_speaking) {
            m_speaking = false;
            if (m_speakTimer)
                m_speakTimer->stop();
            emit audioChanged();
        }
        return;
    }
    if (m_tts)
        m_tts->stop();
}

// ---------------------------------------------------------------- 呼救

void DeviceVM::sos(int level)
{
    AppCore *core = AppCore::instance();
    core->sos(); // 先记事件：即使下面拨号失败，日志里也要有这次呼救

    const QString phone = core->caregiverPhone();
    const QString name = core->elderName().isEmpty() ? QStringLiteral("老人")
                                                     : core->elderName();

    Platform::notify(9001, QStringLiteral("已通知家人"),
                     QStringLiteral("正在联系 %1，并发送短信")
                         .arg(phone.isEmpty() ? QStringLiteral("家人") : phone),
                     false);

    bool dialed = false;
    bool smsed = false;
    if (!phone.isEmpty()) {
        // 电话和短信**同时**走，不是二选一：
        //   * 老人可能接通后一句话说不清病情，家属需要一条文字留痕；
        //   * 电话拨出去了但没人接时，短信已经先到了，不会两头落空。
        // 组里 review 的第一版是"拨通就不再发短信"，这里改成并行。
        if (Platform::hasPermission(Platform::Permission::Phone)) {
            dialed = Platform::call(phone);
        } else {
            Platform::requestPermission(Platform::Permission::Phone);
        }
        if (Platform::hasPermission(Platform::Permission::Sms)) {
            smsed = Platform::sendSms(
                phone, QStringLiteral("【颐伴】%1 刚刚按下了紧急呼救，请立刻联系。").arg(name));
        } else {
            Platform::requestPermission(Platform::Permission::Sms);
        }
    }

    if (!dialed && !smsed) {
        emit toast(QStringLiteral("没能联系上家人，请再按一次呼救，或直接拨打 120"));
    } else if (!smsed) {
        emit toast(QStringLiteral("已拨出电话；短信没发出去，请允许短信权限"));
    } else if (!dialed) {
        emit toast(QStringLiteral("已发出求助短信；电话没拨出，请允许电话权限"));
    }

    emit sosTriggered(level);
}

// ---------------------------------------------------------------- 老人机模式

void DeviceVM::enterKiosk()
{
    if (m_kioskActive)
        return;
    m_kioskActive = true;
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("进入老人机模式"));
    Platform::enterKiosk();
    emit kioskChanged();
}

void DeviceVM::exitKiosk()
{
    if (!m_kioskActive)
        return;
    m_kioskActive = false;
    AppCore::instance()->logOperation(QStringLiteral("设置"), QStringLiteral("退出老人机模式"));
    Platform::exitKiosk();
    emit kioskChanged();
}

// ---------------------------------------------------------------- 闹钟

void DeviceVM::rescheduleAlarms()
{
    if (!Platform::isAndroid())
        return;

    // 全清再重排：确认/推迟/删除都会走到这里，增量维护容易漏，重排最稳。
    Platform::cancelAllAlarms();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QVariantList today = AppCore::instance()->occurrencesToday();

    for (const QVariant &entry : today) {
        const QVariantMap occ = entry.toMap();
        const QString state = occ.value(QStringLiteral("state")).toString();
        // 已经确认/错过/已通知/已升级的不用再响
        if (state == QStringLiteral("confirmed") || state == QStringLiteral("missed")
            || state == QStringLiteral("notified") || state == QStringLiteral("escalated")) {
            continue;
        }

        qint64 due = occ.value(QStringLiteral("dueTs")).toLongLong();
        if (state == QStringLiteral("snoozed")) {
            const qint64 snoozeUntil = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong();
            if (snoozeUntil > 0)
                due = snoozeUntil;
        }
        if (due <= now + 1000)
            continue; // 已经过点的不补闹钟，交给状态机标 missed

        // 夜间安静按**触发时刻**判断，不是按排程时刻：
        // 白天排好的 23:00 提醒，到点也不该响。
        const qint64 occurrenceId =
            occ.value(QStringLiteral("occurrenceId")).toLongLong();
        const int hour = QDateTime::fromMSecsSinceEpoch(due).time().hour();
        const bool quietAtDue = m_nightQuiet && (hour >= 22 || hour < 7);

        // 离线播报：把这一条的播报文案预渲染成 WAV。渲染是异步的，排程先照常
        // 进行并带上目标路径——到点通常在几十分钟后，届时文件早已写好；
        // 万一没写好，AlarmService 会退回系统 TTS，不至于哑掉。
        QString audioPath;
#ifdef Q_OS_ANDROID
        if (m_voiceEnabled && !quietAtDue && m_alarmTts) {
            const QString title = occ.value(QStringLiteral("title")).toString();
            const QString body = occText(occ);
            m_alarmTts->render(occurrenceId,
                               body.isEmpty() ? title : title + QStringLiteral("。") + body);
            audioPath = AlarmTtsRenderer::wavPath(occurrenceId);
        }
#endif

        Platform::scheduleAlarm(occurrenceId, due,
                                occ.value(QStringLiteral("title")).toString(), occText(occ),
                                quietAtDue, m_voiceEnabled, audioPath);
    }

    // 精确闹钟权限没开时，闹钟会退化成非精确（可能晚几分钟），只在第一次提醒
    if (!m_warnedExactAlarm && !Platform::canScheduleExactAlarms()) {
        m_warnedExactAlarm = true;
        emit toast(QStringLiteral("请允许“闹钟和提醒”，否则提醒可能晚几分钟"));
        Platform::openExactAlarmSettings();
    }
    // Android 14+ 的"全屏通知"没开时，锁屏只会响不会亮屏
    if (!m_warnedFullScreen && !Platform::canUseFullScreenIntent()) {
        m_warnedFullScreen = true;
        emit toast(QStringLiteral("锁屏提醒未开启，锁屏时可能只响不亮"));
    }
}

void DeviceVM::onReminderDue(const QVariantMap &occurrence)
{
    const qint64 occurrenceId = occurrence.value(QStringLiteral("occurrenceId")).toLongLong();
    // 闹钟已经响过的话这里只是把通知覆盖成最新文案，不会响两次
    Platform::cancelAlarm(occurrenceId);

    if (!Platform::isAndroid())
        return;

    Platform::notify(notificationIdFor(occurrenceId),
                     occurrence.value(QStringLiteral("title")).toString(), occText(occurrence),
                     false);
}

// ---------------------------------------------------------------- 权限 / 演示

void DeviceVM::requestNotificationPermission()
{
    if (!Platform::isAndroid())
        return;
    if (!Platform::hasPermission(Platform::Permission::Notifications))
        Platform::requestPermission(Platform::Permission::Notifications);
}

void DeviceVM::requestExactAlarmPermission()
{
    if (!Platform::isAndroid())
        return;
    Platform::requestPermission(Platform::Permission::ExactAlarm);
    emit toast(QStringLiteral("请在设置里允许“闹钟和提醒”，到点才能准时亮屏"));
}

bool DeviceVM::openSystemSettings(const QString &key)
{
    const bool ok = Platform::openSystemSettings(key);
    if (!ok)
        emit toast(QStringLiteral("这一项在本机没有可跳转的设置页"));
    return ok;
}

void DeviceVM::requestFullScreenPermission()
{
    if (!Platform::isAndroid())
        return;
    if (Platform::canUseFullScreenIntent()) {
        emit toast(QStringLiteral("锁屏全屏提醒已开启"));
        return;
    }
    Platform::requestPermission(Platform::Permission::FullScreenIntent);
    emit toast(QStringLiteral("请在“全屏通知”里允许颐伴，锁屏时才会亮屏"));
}

void DeviceVM::clearNetworkLog()
{
    NetworkLog::clear();
    m_networkLog.clear();
    emit networkLogChanged();
}

void DeviceVM::resetDemo()
{
    AppCore::instance()->resetDemo();
    Platform::cancelAllAlarms();
    // 配对是设备级状态、不是演示数据：恢复演示数据时不动它，
    // 否则演示完一关，真实环境里的配对关系就被悄悄抹掉了。
    m_kioskActive = false;
    emit kioskChanged();
    QTimer::singleShot(0, this, &DeviceVM::rescheduleAlarms);
}
