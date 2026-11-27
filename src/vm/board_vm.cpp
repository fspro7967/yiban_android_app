#include "board_vm.h"

#include "app_core.h"
#include "ble_link.h"
#include "facade_errors.h"
#include "platform.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QTimer>

namespace {

// 灯色映射（B1-6 的 lightColorFor 还没合入，先按 R4-4 语义固定一版，
// 等 B1 的接口进来后改成调 B1 的纯函数，两边语义才不会分叉）。
QString lightColorFor(const QString &state)
{
    if (state == QLatin1String("confirmed"))
        return QStringLiteral("green");
    if (state == QLatin1String("escalated") || state == QLatin1String("missed"))
        return QStringLiteral("red");
    if (state == QLatin1String("snoozed"))
        return QStringLiteral("breath");
    return QStringLiteral("yellow"); // pending：待确认
}

} // namespace

BoardVM *BoardVM::instance()
{
    static BoardVM *vm = new BoardVM(QCoreApplication::instance());
    return vm;
}

BoardVM *BoardVM::create(QQmlEngine *, QJSEngine *)
{
    BoardVM *vm = instance();
    // 对象挂在 QCoreApplication 下，不能让 QML 引擎在退出时再删一次。
    QQmlEngine::setObjectOwnership(vm, QQmlEngine::CppOwnership);
    return vm;
}

BoardVM::BoardVM(QObject *parent)
    : QObject(parent)
    , m_link(new BleLink(this))
{
    connect(m_link, &BleLink::scanningChanged, this, &BoardVM::scanningChanged);
    connect(m_link, &BleLink::devicesChanged, this, &BoardVM::devicesChanged);
    connect(m_link, &BleLink::detailChanged, this, &BoardVM::detailChanged);
    connect(m_link, &BleLink::countersChanged, this, &BoardVM::countersChanged);
    connect(m_link, &BleLink::notice, this, &BoardVM::toast);
    connect(m_link, &BleLink::rejected, this, [this](const QString &code) {
        emit commandRejected(code, facadeErrorText(code));
    });

    connect(m_link, &BleLink::stateChanged, this, [this]() {
        // 连上 / 断开各记一条操作日志；中间状态（正在连接、自动重连的每次尝试）不记。
        const bool nowReady = m_link->isUp();
        if (nowReady != m_wasReady) {
            if (nowReady) {
                m_readyName = m_link->deviceName();
                Platform::startConnectionService(); // B2-2：灭屏保活
                // 重连后只推当前状态，不补发历史提醒（B2-5 验收）。
                pushCurrentReminders();
            } else {
                Platform::stopConnectionService();  // 断开后撤掉常驻通知
            }
            AppCore::instance()->logOperation(
                QStringLiteral("管理"),
                (nowReady ? QStringLiteral("外设板已连接：%1")
                          : QStringLiteral("外设板连接断开：%1")).arg(m_readyName));
            m_wasReady = nowReady;
        }
        emit linkChanged();
    });

    connect(m_link, &DeviceLink::received, this,
            [this](const QString &topic, const QJsonObject &payload) {
        m_lastMessage = QString::fromUtf8(DeviceLink::encodeLine(topic, payload)).trimmed();
        m_lastMessageTs = QDateTime::currentMSecsSinceEpoch();
        emit messageReceived();
        if (topic == QLatin1String(BoardLinkProtocol::kPresenceTopic)) {
            m_presence = payload.toVariantMap();
            emit presenceChanged();
            return;
        }
        // B2-6：板子按键上报。B1-5 的 confirmFromBoard/alarmFromBoard 还没登记，
        // 这里直接走 AppCore 已有的幂等入口；重复 ack 由 B1 的 already_confirmed 挡掉。
        if (topic.startsWith(QLatin1String(BoardLinkProtocol::kAckTopicPrefix))) {
            const qint64 occId =
                static_cast<qint64>(payload.value(QLatin1String("occurrence_id")).toDouble());
            const QString action = payload.value(QLatin1String("action")).toString();
            if (occId <= 0)
                return;
            AppCore *core = AppCore::instance();
            if (action == QLatin1String("snooze")) {
                core->snooze(occId);
            } else if (action == QLatin1String("confirm") || action.isEmpty()) {
                // 缺省当确认（固件早期版本只发 occurrence_id）
                core->confirm(occId);
            }
            AppCore::instance()->logOperation(
                QStringLiteral("提醒"),
                QStringLiteral("板子按键：%1（%2）").arg(occId).arg(action));
            return;
        }
        if (topic == QLatin1String(BoardLinkProtocol::kAlarmTopic)) {
            // 按板子呼救键：发信号让 DeviceVM 走和手动 SOS 完全一样的路径。
            emit boardSosRequested();
            AppCore::instance()->logOperation(QStringLiteral("提醒"),
                                              QStringLiteral("板子呼救键"));
            return;
        }
    });
    connect(m_link, &DeviceLink::malformed, this, [this](const QByteArray &line) {
        // 不是合法消息的行也给联调的人看一眼，多半是固件的调试打印混进来了。
        m_lastMessage = QStringLiteral("（看不懂的一行）") + QString::fromUtf8(line.left(200));
        m_lastMessageTs = QDateTime::currentMSecsSinceEpoch();
        emit messageReceived();
    });

    // 提醒状态变化（弹出/确认/稍后/升级/错过）时推灯板：只发还亮着的当前状态。
    // QueuedConnection：不要在 AppCore 的信号栈帧里同步写 BLE。
    connect(AppCore::instance(), &AppCore::occurrencesChanged, this,
            [this] { QTimer::singleShot(0, this, &BoardVM::pushCurrentReminders); });

    // 之前绑过板就悄悄连回去；没绑过什么也不做。
    m_link->resume();
}

bool BoardVM::scanning() const { return m_link->scanning(); }
QVariantList BoardVM::devices() const { return m_link->devices(); }
QString BoardVM::linkState() const { return m_link->state(); }
bool BoardVM::ready() const { return m_link->isUp(); }
bool BoardVM::bound() const { return m_link->bound(); }
QString BoardVM::deviceName() const { return m_link->deviceName(); }
QString BoardVM::detail() const { return m_link->detail(); }
int BoardVM::sentCount() const { return m_link->sentCount(); }
int BoardVM::receivedCount() const { return m_link->receivedCount(); }
DeviceLink *BoardVM::link() const { return m_link; }

bool BoardVM::connected() const
{
    const QString state = m_link->state();
    return state == QStringLiteral("discovering") || state == QStringLiteral("ready")
        || state == QStringLiteral("unsupported");
}

QString BoardVM::statusText() const
{
    const QString state = m_link->state();
    const QString name = m_link->deviceName();
    if (state == QStringLiteral("connecting"))
        return QStringLiteral("正在连接 %1…").arg(name);
    if (state == QStringLiteral("discovering"))
        return QStringLiteral("已连上 %1，正在识别…").arg(name);
    if (state == QStringLiteral("ready"))
        return QStringLiteral("已连接 %1").arg(name);
    if (state == QStringLiteral("unsupported"))
        return QStringLiteral("%1 不能当外设板用：%2").arg(name, m_link->problem());
    if (m_link->bound())
        return QStringLiteral("%1 不在线，会自动重连").arg(name);
    return QStringLiteral("未连接");
}

void BoardVM::startScan() { m_link->startScan(); }
void BoardVM::stopScan() { m_link->stopScan(); }
void BoardVM::connectDevice(const QString &id) { m_link->connectDevice(id); }

void BoardVM::sendReminder(qint64 occurrenceId, const QString &title,
                           const QString &state, qint64 dueTs)
{
    if (!m_link->isUp() || occurrenceId <= 0)
        return; // 不在线不排队；重连后由 pushCurrentReminders 只发当前状态
    const QString device = m_link->deviceName().trimmed().isEmpty()
        ? QStringLiteral("board") : m_link->deviceName();
    const QJsonObject payload {
        { QStringLiteral("occurrence_id"), occurrenceId },
        { QStringLiteral("title"), title },
        { QStringLiteral("state"), state },
        { QStringLiteral("light_color"), lightColorFor(state) },
        { QStringLiteral("due_ts"), dueTs },
    };
    m_link->send(QString::fromLatin1(BoardLinkProtocol::kReminderTopicPrefix) + device, payload);
}

void BoardVM::pushCurrentReminders()
{
    if (!m_link->isUp())
        return;
    const QVariantList today = AppCore::instance()->occurrencesToday();
    for (const QVariant &entry : today) {
        const QVariantMap occ = entry.toMap();
        const QString state = occ.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("confirmed") || state == QLatin1String("missed"))
            continue; // 已终态的不推，重连只发还亮着的当前状态
        sendReminder(occ.value(QStringLiteral("occurrenceId")).toLongLong(),
                     occ.value(QStringLiteral("title")).toString(), state,
                     occ.value(QStringLiteral("dueTs")).toLongLong());
    }
}

void BoardVM::forget()
{
    const QString name = m_link->deviceName();
    const bool wasBound = m_link->bound();
    m_link->forget();
    if (wasBound) {
        AppCore::instance()->logOperation(QStringLiteral("管理"),
                                          QStringLiteral("解除外设板绑定：%1").arg(name));
    }
}
