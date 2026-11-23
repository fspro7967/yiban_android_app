#include "board_vm.h"

#include "app_core.h"
#include "ble_link.h"
#include "facade_errors.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>

namespace {

const QString kPresenceTopic = QStringLiteral("home/sensor/presence");

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
            if (nowReady)
                m_readyName = m_link->deviceName();
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
        if (topic == kPresenceTopic) {
            m_presence = payload.toVariantMap();
            emit presenceChanged();
        }
    });
    connect(m_link, &DeviceLink::malformed, this, [this](const QByteArray &line) {
        // 不是合法消息的行也给联调的人看一眼，多半是固件的调试打印混进来了。
        m_lastMessage = QStringLiteral("（看不懂的一行）") + QString::fromUtf8(line.left(200));
        m_lastMessageTs = QDateTime::currentMSecsSinceEpoch();
        emit messageReceived();
    });

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
