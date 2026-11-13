#include "device_vm.h"

#include "app_core.h"
#include "app_settings.h"
#include "facade_errors.h"
#include "http_client.h"
#include "platform.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QTextToSpeech>
#include <QTime>
#include <QTimer>

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
    m_paired = settings->paired();
    m_role = settings->role();
    m_serverUrl = settings->serverUrl();
    m_wifiName = Platform::isAndroid() ? QStringLiteral("无线网络")
                                       : QStringLiteral("Home-2.4G");

    m_http = new NetworkHttpClient(this);
    connect(m_http, &HttpClient::reply, this,
            [this](quint64 id, int status, const QJsonObject &body) {
                handlePairReply(id, status, body);
            });
    connect(m_http, &HttpClient::failed, this, [this](quint64 id, const QString &code) {
        handlePairFailed(id, code);
    });

    m_tts = new QTextToSpeech(this);
    connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State) {
        emit audioChanged();
    });

    const QStringList engines = m_tts->availableEngines();
    if (!engines.isEmpty()) {
        m_tts->setLocale(QLocale(QLocale::Chinese, QLocale::China));
        m_tts->setRate(-0.15);
    }

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

    m_permissionCache = Platform::grantedLabels();
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
    refreshNetwork();
}

void DeviceVM::refreshPermissions()
{
    const QStringList current = Platform::grantedLabels();
    if (current == m_permissionCache)
        return;
    m_permissionCache = current;
    emit permissionChanged();
}

void DeviceVM::refreshNetwork()
{
    const bool online = m_http && m_http->isOnline();
    if (online == m_networkOnline)
        return;
    m_networkOnline = online;
    emit networkChanged();
}

// ---------------------------------------------------------------- 属性

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
    emit voiceChanged();
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
    emit voiceChanged();

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
    AppSettings::instance()->setServerUrl(trimmed);
    m_networkOnline = m_http && m_http->isOnline();
    emit networkChanged();
    emit toast(QStringLiteral("服务器地址已保存"));
    return true;
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
        // 401/409 都是配对码不对或已被用掉，对用户来说是同一件事
        finishPair(false, m_role, facadeErrorText(QStringLiteral("bad_pair_code")));
        return;
    }

    const QString token = body.value(QStringLiteral("token")).toString();
    if (!token.isEmpty())
        AppSettings::instance()->setToken(token);

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

// ---------------------------------------------------------------- 呼救

void DeviceVM::sos(int level)
{
    AppCore *core = AppCore::instance();
    core->sos(); // 先记事件：即使下面拨号失败，日志里也要有这次呼救

    const QString phone = core->caregiverPhone();
    const QString name = core->elderName().isEmpty() ? QStringLiteral("老人")
                                                     : core->elderName();

    Platform::notify(9001, QStringLiteral("已通知家人"),
                     QStringLiteral("正在联系 %1").arg(phone.isEmpty()
                                                           ? QStringLiteral("家人")
                                                           : phone),
                     false);

    bool reached = false;
    if (!phone.isEmpty()) {
        // 没有拨号权限就先申请，别静默失败（老人不会自己想到去设置里开）
        if (!Platform::hasPermission(Platform::Permission::Phone)) {
            Platform::requestPermission(Platform::Permission::Phone);
        } else {
            reached = Platform::call(phone);
        }
        // 拨不通就退化成短信（规范 §6 的降级要求）
        if (!reached && Platform::hasPermission(Platform::Permission::Sms)) {
            reached = Platform::sendSms(
                phone, QStringLiteral("【颐伴】%1 刚刚按下了紧急呼救，请立刻联系。").arg(name));
        }
    }

    if (!reached) {
        emit toast(QStringLiteral("没能联系上家人，请再按一次呼救，或直接拨打 120"));
    }

    emit sosTriggered(level);
}

// ---------------------------------------------------------------- 老人机模式

void DeviceVM::enterKiosk()
{
    if (m_kioskActive)
        return;
    m_kioskActive = true;
    Platform::enterKiosk();
    emit kioskChanged();
}

void DeviceVM::exitKiosk()
{
    if (!m_kioskActive)
        return;
    m_kioskActive = false;
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

        Platform::scheduleAlarm(occ.value(QStringLiteral("occurrenceId")).toLongLong(), due,
                                occ.value(QStringLiteral("title")).toString(), occText(occ));
    }

    // 精确闹钟权限没开时，闹钟会退化成非精确（可能晚几分钟），只在第一次提醒
    if (!m_warnedExactAlarm && !Platform::canScheduleExactAlarms()) {
        m_warnedExactAlarm = true;
        emit toast(QStringLiteral("请允许“闹钟和提醒”，否则提醒可能晚几分钟"));
        Platform::openExactAlarmSettings();
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
