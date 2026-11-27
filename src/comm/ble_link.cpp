#ifndef YIBAN_NO_BLUETOOTH  // 无 QtBluetooth 的桌面构建改用 ble_link_stub.cpp

#include "ble_link.h"

#include "devicehandler.h"

#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>
#include <QBluetoothUuid>
#include <QCoreApplication>
#include <QDateTime>
#include <QLoggingCategory>
#include <QPermissions>
#include <QSettings>
#include <QTimer>

#include <algorithm>

// 联调时看 logcat / 终端：adb logcat -s yiban.board（桌面上直接打在 stderr）。
Q_LOGGING_CATEGORY(lcBoard, "yiban.board")

namespace {

const QString kIdle = QStringLiteral("idle");
const QString kConnecting = QStringLiteral("connecting");
const QString kDiscovering = QStringLiteral("discovering");
const QString kReady = QStringLiteral("ready");
const QString kUnsupported = QStringLiteral("unsupported");

const QString kSettingsId = QStringLiteral("board/id");
const QString kSettingsName = QStringLiteral("board/name");

// 待发消息的上限。链路正常时队列里至多一两条；堆到这个数说明对端收不动了，
// 丢最旧的——灯板要的是最新状态，过期的状态补发过去没有意义。
constexpr int kMaxOutbox = 32;
// 队列里已经压着这么多条时不再塞心跳：别的消息一样能证明主机还活着。
constexpr int kHeartbeatSkipAbove = 4;

// 自动重连的退避：2 秒起，每次翻倍，封顶 30 秒。
constexpr int kReconnectFirstMs = 2000;
constexpr int kReconnectMaxMs = 30000;

QString uuidText(const char *uuid)
{
    return QString::fromLatin1(uuid);
}

// 设备自己报的名字。没有名字时 Android 给空串，而 BlueZ 会拿地址顶上
// （"47-F3-78-74-DF-44"），两种都算没名字。
QString advertisedName(const QBluetoothDeviceInfo &info)
{
    const QString name = info.name().trimmed();
    QString addressAsName = info.address().toString();
    addressAsName.replace(QLatin1Char(':'), QLatin1Char('-'));
    return name.compare(addressAsName, Qt::CaseInsensitive) == 0 ? QString() : name;
}

} // namespace

BleLink::BleLink(QObject *parent)
    : DeviceLink(parent)
    , m_handler(new DeviceHandler(this))
    , m_heartbeat(new QTimer(this))
    , m_reconnect(new QTimer(this))
{
    QSettings settings;
    m_boundId = settings.value(kSettingsId).toString();
    m_boundName = settings.value(kSettingsName).toString();
    qCInfo(lcBoard) << "bound board:" << (bound() ? m_boundName + QLatin1Char(' ') + m_boundId
                                                   : QStringLiteral("none"));

    connect(m_handler, &DeviceHandler::statusChanged, this, &BleLink::setDetail);

    // ---- 扫描 ----
    connect(m_handler, &DeviceHandler::deviceDiscovered, this,
            [this](const QBluetoothDeviceInfo &info) {
        // 外设板是 BLE 外设；而且只认广播里带串口透传服务的——附近的手环、耳机、
        // 还有雷达模块自带的调参蓝牙，都不该出现在列表里。
        if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
            || !advertisesBoard(info)) {
            return;
        }
        const QString id = deviceId(info);
        for (QBluetoothDeviceInfo &known : m_found) {
            if (deviceId(known) == id) {
                known = info;
                emit devicesChanged();
                return;
            }
        }
        m_found.append(info);
        emit devicesChanged();
    });
    connect(m_handler, &DeviceHandler::scanFinished, this, [this]() { setScanning(false); });
    connect(m_handler, &DeviceHandler::scanFailed, this, [this](const QString &reason) {
        if (!m_scanning)
            return;
        setScanning(false);
        emit notice(QStringLiteral("蓝牙扫描失败：%1").arg(reason));
    });

    // ---- 连接 ----
    connect(m_handler, &DeviceHandler::connected, this, [this]() {
        if (m_state == kConnecting)
            setState(kDiscovering);
    });
    // 串口透传服务自己的特征一发现完就配，不等其它服务：对端服务多的时候（拿笔记本
    // 冒充外设板联调时有十几个）全部发现完要几十秒，中途还可能有服务卡住，
    // 那样 serviceDetailsDiscoveryFinished 只会在超时那一刻发一次，而那时特征还没到。
    // 每条特征发一次信号，configure 自己防重入（配成 ready 之后直接返回）。
    connect(m_handler, &DeviceHandler::characteristicDiscovered, this,
            [this](const QString &serviceUuid) {
        static const QBluetoothUuid boardService{uuidText(BoardLinkProtocol::kServiceUuid)};
        if (QBluetoothUuid{serviceUuid} == boardService)
            configure();
    });
    // 全部服务发现完（或超时）：到这时还没有串口透传服务，就可以下结论说不是外设板。
    connect(m_handler, &DeviceHandler::serviceDetailsDiscoveryFinished,
            this, &BleLink::configure);
    connect(m_handler, &DeviceHandler::disconnected, this, &BleLink::onDisconnected);
    connect(m_handler, &DeviceHandler::connectionError, this, [this](const QString &reason) {
        // 已经连上之后的错误：链路若真断了，disconnected 会随后到，这里不重复处理。
        if (m_state != kConnecting)
            return;
        // 连接阶段失败不会有 disconnected，自己收尾。
        const QString name = m_peerName;
        const bool wasBoundPeer = bound() && m_peerId == m_boundId;
        qCInfo(lcBoard) << "connect to" << name << "failed:" << reason;
        m_handler->disconnectDevice();
        tearDown();
        if (wasBoundPeer) {
            // 绑定的板不在线（没通电、不在范围）是常态，不逐次打扰，按退避接着试。
            scheduleReconnect();
        } else {
            emit notice(QStringLiteral("连不上 %1：%2").arg(name, reason));
        }
    });

    // ---- 收发 ----
    connect(m_handler, &DeviceHandler::dataReceived, this, &BleLink::feed);
    connect(this, &DeviceLink::received, this, [this](const QString &topic) {
        qCDebug(lcBoard).noquote() << "rx" << topic;
        ++m_receivedCount;
        emit countersChanged();
    });
    connect(m_handler, &DeviceHandler::writeFinished, this, [this]() {
        if (!m_writing)
            return;
        m_writing = false;
        ++m_sentCount;
        emit countersChanged();
        pump();
    });
    connect(m_handler, &DeviceHandler::writeError, this, [this](const QString &error) {
        setDetail(error);
        if (!m_writing)
            return;
        // 这一条作废，接着发后面的。排队调用而不是当场调用：
        // writeData 可能在调用栈里同步报错，当场再发会递归。
        m_writing = false;
        QMetaObject::invokeMethod(this, &BleLink::pump, Qt::QueuedConnection);
    });

    m_heartbeat->setInterval(BoardLinkProtocol::kHeartbeatIntervalMs);
    connect(m_heartbeat, &QTimer::timeout, this, &BleLink::sendHeartbeat);

    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, &BleLink::attemptReconnect);
}

// ---------- DeviceLink ----------

bool BleLink::isUp() const
{
    return m_state == kReady;
}

bool BleLink::send(const QString &topic, const QJsonObject &payload)
{
    if (!isUp())
        return false;
    if (m_outbox.size() >= kMaxOutbox)
        m_outbox.removeFirst();
    m_outbox.append(encodeLine(topic, payload));
    pump();
    return true;
}

void BleLink::pump()
{
    if (m_writing || m_outbox.isEmpty() || !isUp())
        return;
    // 先置位再发：一行超过一个 BLE 包时 DeviceHandler 会按 MTU 拆包，
    // 整行发完才有 writeFinished。
    m_writing = true;
    m_handler->writeData(m_outbox.takeFirst());
}

void BleLink::sendHeartbeat()
{
    if (m_outbox.size() >= kHeartbeatSkipAbove)
        return;
    send(QString::fromLatin1(BoardLinkProtocol::kHeartbeatTopic),
         QJsonObject{{QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch()}});
}

// ---------- 读取 ----------

QVariantList BleLink::devices() const
{
    QList<QBluetoothDeviceInfo> sorted = m_found;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const QBluetoothDeviceInfo &a, const QBluetoothDeviceInfo &b) {
        // rssi 为 0 表示这次没量到（系统缓存里的设备），不是信号满格，排到最后。
        const int rssiA = a.rssi() == 0 ? -999 : a.rssi();
        const int rssiB = b.rssi() == 0 ? -999 : b.rssi();
        return rssiA > rssiB;
    });

    QVariantList list;
    for (const QBluetoothDeviceInfo &info : std::as_const(sorted)) {
        const QString id = deviceId(info);
        const QString name = advertisedName(info);
        list.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name.isEmpty() ? id : name},
            {QStringLiteral("rssi"), info.rssi()},
        });
    }
    return list;
}

QString BleLink::deviceName() const
{
    return m_peerName.isEmpty() ? m_boundName : m_peerName;
}

// ---------- 扫描 ----------

void BleLink::startScan()
{
    if (m_scanning)
        return;

    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        // 第一次用：让系统弹授权框，用户点完再回来。
        qApp->requestPermission(permission, this, [this](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted)
                beginScan();
            else
                emit rejected(QStringLiteral("bluetooth_denied"));
        });
        return;
    case Qt::PermissionStatus::Denied:
        emit rejected(QStringLiteral("bluetooth_denied"));
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
    beginScan();
}

void BleLink::beginScan()
{
    if (m_scanning)
        return;

    // 蓝牙关着时扫描只会以一条错误收场，先查清楚再给一句能照着做的话。
    const QBluetoothLocalDevice adapter;
    if (!adapter.isValid()) {
        emit rejected(QStringLiteral("bluetooth_unavailable"));
        return;
    }
    if (adapter.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        emit rejected(QStringLiteral("bluetooth_off"));
        return;
    }

    m_found.clear();
    emit devicesChanged();
    setScanning(true);
    m_handler->startScan();
}

void BleLink::stopScan()
{
    if (!m_scanning)
        return;
    // 主动停止时底层发的是 canceled 而不是 finished，状态由这里自己收。
    m_handler->stopScan();
    setScanning(false);
}

void BleLink::setScanning(bool on)
{
    if (m_scanning == on)
        return;
    m_scanning = on;
    emit scanningChanged();
}

// ---------- 连接 ----------

void BleLink::connectDevice(const QString &id)
{
    const auto found = std::find_if(m_found.cbegin(), m_found.cend(),
                                    [&id](const QBluetoothDeviceInfo &info) {
        return deviceId(info) == id;
    });
    if (found == m_found.cend()) {
        emit rejected(QStringLiteral("board_not_found"));
        return;
    }
    const QBluetoothDeviceInfo info = *found;
    const QString name = advertisedName(info);

    // Android 上边扫描边连接很容易失败，先停。
    stopScan();
    m_reconnect->stop();
    if (m_state != kIdle) {
        m_handler->disconnectDevice();
        tearDown();
    }
    connectTo(info, name.isEmpty() ? id : name);
}

void BleLink::connectTo(const QBluetoothDeviceInfo &info, const QString &name)
{
    m_peerId = deviceId(info);
    m_peerName = name;
    setState(kConnecting);
    m_handler->connectToDevice(info);
}

void BleLink::forget()
{
    m_reconnect->stop();
    m_backoffMs = 0;
    m_boundId.clear();
    m_boundName.clear();
    QSettings settings;
    settings.remove(kSettingsId);
    settings.remove(kSettingsName);

    if (m_state != kIdle) {
        m_handler->disconnectDevice();
        // 状态当场归位；底层稍后补发的 disconnected 到 onDisconnected 时已是 idle，不会重复处理。
        tearDown();
    }
    emit stateChanged();   // bound 变了，即使状态本来就是 idle 也要通知
}

void BleLink::resume()
{
    if (!bound() || m_state != kIdle)
        return;
    m_backoffMs = 0;
    scheduleReconnect();
}

void BleLink::scheduleReconnect()
{
    if (!bound())
        return;
    m_backoffMs = m_backoffMs == 0 ? kReconnectFirstMs
                                   : std::min(m_backoffMs * 2, kReconnectMaxMs);
    qCInfo(lcBoard) << "next reconnect in" << m_backoffMs << "ms";
    m_reconnect->start(m_backoffMs);
}

void BleLink::attemptReconnect()
{
    if (!bound() || m_state != kIdle)
        return;

    // 自动重连不弹授权框、不打断正在进行的扫描；条件不满足就过一会儿再看。
    if (m_scanning || !bluetoothGranted()) {
        qCInfo(lcBoard) << "reconnect postponed:"
                        << (m_scanning ? "scan in progress" : "bluetooth permission not granted");
        m_reconnect->start(kReconnectMaxMs);
        return;
    }
    const QBluetoothLocalDevice adapter;
    if (!adapter.isValid() || adapter.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        qCInfo(lcBoard) << "reconnect postponed: bluetooth is off or unavailable";
        m_reconnect->start(kReconnectMaxMs);
        return;
    }
    qCInfo(lcBoard) << "reconnecting to" << m_boundName << m_boundId;

    // 不重新扫描，直接按记住的地址连。Android / Linux 上 id 是 MAC；
    // 苹果系统不给 MAC，id 是系统分配的 UUID。
    const QBluetoothAddress address(m_boundId);
    QBluetoothDeviceInfo info = address.isNull()
        ? QBluetoothDeviceInfo(QBluetoothUuid(m_boundId), m_boundName, 0)
        : QBluetoothDeviceInfo(address, m_boundName, 0);
    info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    connectTo(info, m_boundName);
}

void BleLink::configure()
{
    // 只在「刚发现完」或「上次没配成」时配；ready 之后迟到的服务通知不重复配置，
    // 否则 setWriteTarget 会把正在发送的队列清掉。
    if (m_state != kDiscovering && m_state != kUnsupported)
        return;

    const QString service = uuidText(BoardLinkProtocol::kServiceUuid);
    const QString rx = uuidText(BoardLinkProtocol::kRxCharUuid);
    const QString tx = uuidText(BoardLinkProtocol::kTxCharUuid);

    if (!m_handler->servicePresent(service)) {
        setState(kUnsupported, QStringLiteral("没有找到串口透传服务"));
        return;
    }
    // 服务在不代表特征在：详情发现失败时服务对象还在，特征却是空的，要逐个核实。
    if (!m_handler->characteristicPresent(service, rx)
        || !m_handler->characteristicPresent(service, tx)) {
        setState(kUnsupported, QStringLiteral("串口透传服务里缺少收发特征，固件版本可能不对"));
        return;
    }
    if (!m_handler->enableCharacteristicNotification(service, tx, true)) {
        setState(kUnsupported, QStringLiteral("外设板的上报特征不能通知"));
        return;
    }
    if (!m_handler->setWriteTarget(service, rx)) {
        setState(kUnsupported, QStringLiteral("外设板的接收特征不可写"));
        return;
    }

    // 认出来了才记住：连错的设备不会被绑定，也就不会被自动重连。
    if (m_boundId != m_peerId || m_boundName != m_peerName) {
        m_boundId = m_peerId;
        m_boundName = m_peerName;
        QSettings settings;
        settings.setValue(kSettingsId, m_boundId);
        settings.setValue(kSettingsName, m_boundName);
    }
    m_backoffMs = 0;

    setState(kReady);
    // 立刻发一条心跳：外设板没有实时时钟，越早对上时越好。
    sendHeartbeat();
    m_heartbeat->start();
    emit notice(QStringLiteral("外设板 %1 已连接").arg(m_peerName));
}

void BleLink::onDisconnected()
{
    if (m_state == kIdle)
        return;   // 主动断开或连接失败，已经收过尾
    const bool wasUp = isUp();
    const QString name = m_peerName;
    tearDown();
    if (wasUp)
        emit notice(QStringLiteral("外设板 %1 的连接断开了").arg(name));
    scheduleReconnect();
}

void BleLink::tearDown()
{
    m_heartbeat->stop();
    m_outbox.clear();
    m_writing = false;
    resetFraming();
    m_peerId.clear();
    m_peerName.clear();
    setState(kIdle);
}

void BleLink::setState(const QString &state, const QString &problem)
{
    if (m_state == state && m_problem == problem)
        return;
    const bool wasUp = isUp();
    m_state = state;
    m_problem = problem;
    qCInfo(lcBoard).noquote() << "state:" << state << m_peerName << problem;
    emit stateChanged();
    if (wasUp != isUp())
        emit upChanged();
}

// ---------- 辅助 ----------

void BleLink::setDetail(const QString &text)
{
    if (m_detail == text)
        return;
    m_detail = text;
    emit detailChanged();
}

bool BleLink::bluetoothGranted()
{
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    return qApp->checkPermission(permission) == Qt::PermissionStatus::Granted;
}

QString BleLink::deviceId(const QBluetoothDeviceInfo &info)
{
    return info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
}

bool BleLink::bound() const { return !m_boundId.isEmpty(); }

QString BleLink::detail() const { return m_detail; }

int BleLink::sentCount() const { return m_sentCount; }

int BleLink::receivedCount() const { return m_receivedCount; }

bool BleLink::advertisesBoard(const QBluetoothDeviceInfo &info)
{
    static const QBluetoothUuid boardService{uuidText(BoardLinkProtocol::kServiceUuid)};
    return info.serviceUuids().contains(boardService);
}

#endif // !YIBAN_NO_BLUETOOTH
