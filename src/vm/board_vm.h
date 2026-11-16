#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class BleLink;
class DeviceLink;
class QJSEngine;
class QQmlEngine;

// 外设板 facade：通过 BLE 找到、绑定并连上外设板（ESP32-S3：人体雷达 + 灯板 + 确认键）。
// 硬件设计见 caremate_hardware/agent/硬件设计路线.md。
//
// 外设板**不负责音频**——提醒的声音仍由手机自己的喇叭或外接音箱出；
// 这条链路上只有一行一条的 JSON 状态和事件（雷达上报、按键确认、提醒状态、主机心跳）。
//
// 链路本身在 src/comm/ble_link.*（实现 src/comm/device_link.h 的 DeviceLink），
// 这一层只把它的状态整理给管理后台看，外加记操作日志。
// 以后的 TerminalBridge（把提醒状态推给灯板、把按键确认交给 AppCore）是 C++ 侧的使用者，
// 从 instance()->link() 拿链路，只用 send / received / isUp，不需要改这里。
class BoardVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // 本次扫描到的外设板，每项 {id, name, rssi}；rssi 单位 dBm，0 表示没量到。
    // 只列广播里带串口透传服务的 BLE 设备，按信号强弱排。
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // idle（未连接）/ connecting / discovering（已连上，正在发现服务）
    // / ready（认出外设板，可以收发）/ unsupported（连上了，但不是外设板或特征不全）
    Q_PROPERTY(QString linkState READ linkState NOTIFY linkChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY linkChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY linkChanged)
    // 是否记着一块板。绑定后断线会自动重连，直到 forget()。
    Q_PROPERTY(bool bound READ bound NOTIFY linkChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY linkChanged)
    // 给人看的一句话状态；unsupported 时带原因。
    Q_PROPERTY(QString statusText READ statusText NOTIFY linkChanged)
    // 底层最近一条原始状态消息，联调时看。
    Q_PROPERTY(QString detail READ detail NOTIFY detailChanged)
    // 本次运行累计发出 / 收到的消息条数（发出的含主机心跳）。
    Q_PROPERTY(int sentCount READ sentCount NOTIFY countersChanged)
    Q_PROPERTY(int receivedCount READ receivedCount NOTIFY countersChanged)
    // 最近收到的一条消息，原样的一行 JSON；lastMessageTs 是收到时的本机时间（毫秒）。
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY messageReceived)
    Q_PROPERTY(qint64 lastMessageTs READ lastMessageTs NOTIFY messageReceived)
    // 最近一条 home/sensor/presence 的 payload（room / present / moving / still_s / dist_cm / ts），
    // 还没收到过为空 map。这里只是原样转述，「久未活动」之类的判断不在这一层。
    Q_PROPERTY(QVariantMap presence READ presence NOTIFY presenceChanged)

public:
    // C++ 侧与 QML 用的是同一个对象。
    static BoardVM *instance();
    static BoardVM *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool scanning() const;
    QVariantList devices() const;
    QString linkState() const;
    bool connected() const;
    bool ready() const;
    bool bound() const;
    QString deviceName() const;
    QString statusText() const;
    QString detail() const;
    int sentCount() const;
    int receivedCount() const;
    QString lastMessage() const { return m_lastMessage; }
    qint64 lastMessageTs() const { return m_lastMessageTs; }
    QVariantMap presence() const { return m_presence; }

    // 到外设板的链路。只给 C++ 侧用；QML 不碰。
    DeviceLink *link() const;

    // 需要蓝牙权限时会先向系统申请，授权后自动开始；被拒绝走 commandRejected。
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    // id 取自 devices 里的同名字段。连上并认出外设板后自动绑定。
    Q_INVOKABLE void connectDevice(const QString &id);
    // 断开并解除绑定，不再自动重连。
    Q_INVOKABLE void forget();

signals:
    void scanningChanged();
    void devicesChanged();
    void linkChanged();
    void detailChanged();
    void countersChanged();
    void messageReceived();
    void presenceChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    explicit BoardVM(QObject *parent = nullptr);

    BleLink *m_link = nullptr;
    bool m_wasReady = false;
    QString m_readyName;
    QString m_lastMessage;
    qint64 m_lastMessageTs = 0;
    QVariantMap m_presence;
};
