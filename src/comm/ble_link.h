#pragma once

#include "device_link.h"

#ifndef YIBAN_NO_BLUETOOTH
#include <QBluetoothDeviceInfo>
#endif
#include <QList>
#include <QVariantList>

class DeviceHandler;
class QTimer;

// 外设板 BLE 链路上的约定。固件还没写，这里是主机一侧先定下来的值，
// **写固件时照这里来；要改就两边一起改**。
namespace BoardLinkProtocol {
// 串口透传服务（Nordic UART Service）：一个特征写、一个特征通知。
inline constexpr const char *kServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
// RX：主机 → 外设板（写）
inline constexpr const char *kRxCharUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
// TX：外设板 → 主机（通知）
inline constexpr const char *kTxCharUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
// 主机心跳：payload 为 {"ts": 毫秒时间戳}。外设板没有实时时钟，靠它对时；
// 15 秒收不到就按掉线降级（按缓存继续亮灯，角上亮灰点）。
inline constexpr const char *kHeartbeatTopic = "home/host/heartbeat";
inline constexpr int kHeartbeatIntervalMs = 5000;
} // namespace BoardLinkProtocol

// DeviceLink 的 BLE 实现：手机（或笔记本、树莓派）做中心设备，去连外设板的串口透传服务。
// 底下的扫描 / 连接 / GATT / 分包由 include/devicehandler.h 的 DeviceHandler 负责，
// 这里管四件事：
//   1. 蓝牙权限与开关检查，扫描结果按服务 UUID 过滤
//      （雷达模块自己也带调参用的蓝牙，不过滤容易连错）；
//   2. 连接生命周期，连上后核对并绑好收发两条特征；
//   3. 一行一条 JSON 的收发，外加 5 秒一次的主机心跳；
//   4. 记住连过的那块板（QSettings），断线后按退避间隔自动重连。
class BleLink : public DeviceLink
{
    Q_OBJECT

public:
    explicit BleLink(QObject *parent = nullptr);

    bool isUp() const override;
    bool send(const QString &topic, const QJsonObject &payload) override;

    bool scanning() const { return m_scanning; }
    // 每项 {id, name, rssi}，按信号强弱排；rssi 为 0 表示没量到。
    QVariantList devices() const;
    // idle / connecting / discovering（已连上，正在发现服务）/ ready / unsupported
    QString state() const { return m_state; }
    // unsupported 的原因，其余状态为空。
    QString problem() const { return m_problem; }
    // 当前连接（或正在连接）的设备名；idle 时是已绑定设备的名字，没绑定为空。
    QString deviceName() const;
    // 是否记着一块板。绑定后断线会自动重连，直到 forget()。
    bool bound() const;
    // DeviceHandler 最近一条原始状态消息，联调时看。
    QString detail() const;
    int sentCount() const;
    int receivedCount() const;

    // 需要蓝牙权限时会先向系统申请，授权后自动开始；被拒绝走 rejected。
    void startScan();
    void stopScan();
    // id 取自 devices。连上并认出外设板后自动绑定。
    void connectDevice(const QString &id);
    // 断开并解除绑定，不再自动重连。
    void forget();
    // App 启动时调用：之前绑过板、且蓝牙权限已经给过，就悄悄连回去（不弹授权框）。
    void resume();

signals:
    void scanningChanged();
    void devicesChanged();
    void stateChanged();
    void detailChanged();
    void countersChanged();
    // 稳定错误码（bluetooth_denied / bluetooth_off / bluetooth_unavailable / board_not_found）
    void rejected(const QString &code);
    // 给人看的一句话（连上了、断开了、连不上）
    void notice(const QString &text);

private:
#ifndef YIBAN_NO_BLUETOOTH
    void beginScan();
    void setScanning(bool on);
    void setState(const QString &state, const QString &problem = QString());
    void setDetail(const QString &text);
    void connectTo(const QBluetoothDeviceInfo &info, const QString &name);
    // 服务详情发现完后调用：认外设板、绑收发特征。可能被调用多次（迟到的服务）。
    void configure();
    // 连接没了（意外断开、连接失败、主动断开）之后的统一收尾。
    void tearDown();
    void onDisconnected();
    void pump();
    void sendHeartbeat();
    void scheduleReconnect();
    void attemptReconnect();
    static bool bluetoothGranted();
    static QString deviceId(const QBluetoothDeviceInfo &info);
    static bool advertisesBoard(const QBluetoothDeviceInfo &info);
#endif

#ifndef YIBAN_NO_BLUETOOTH
    DeviceHandler *m_handler = nullptr;
    QTimer *m_heartbeat = nullptr;
    QTimer *m_reconnect = nullptr;
    QList<QBluetoothDeviceInfo> m_found;
#endif
    bool m_scanning = false;
    QString m_state = QStringLiteral("idle");
    QString m_problem;
    QString m_detail;
#ifndef YIBAN_NO_BLUETOOTH
    QString m_peerId;        // 当前连接（或正在连接）的设备
    QString m_peerName;
    QString m_boundId;       // 记住的那块板
    QString m_boundName;
    int m_backoffMs = 0;

    // DeviceHandler::writeData 一次只收一条，上一条没发完再给会被拒绝，所以这里排队。
    QList<QByteArray> m_outbox;
    bool m_writing = false;
    int m_sentCount = 0;
    int m_receivedCount = 0;
#endif
};
