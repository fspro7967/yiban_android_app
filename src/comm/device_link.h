#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

// 主机到外设板（ESP32-S3：雷达 + 灯板 + 确认键）的链路抽象。
//
// 外设板不管链路是 BLE 还是 USB 串口，消息格式都一样——一行一条 JSON：
//   {"topic":"home/sensor/presence","payload":{"room":"living","present":true,...}}\n
// topic 和 payload 沿用 agent/接口冻结清单.md §4 的 MQTT 形状（允许多字段，不许少字段）。
// 设计见 caremate_hardware/agent/硬件设计路线.md §2.2–2.5。
//
// 上层（以后的 TerminalBridge）只认 send / received / isUp，不关心底下是哪条链路，
// 所以单测可以换成假链路，桌面联调可以换成串口或回放。
// 外设板**不负责音频**：声音由主机自己的喇叭或外接音箱出，这条链路上只有状态和事件。
class DeviceLink : public QObject
{
    Q_OBJECT

public:
    explicit DeviceLink(QObject *parent = nullptr) : QObject(parent) {}
    ~DeviceLink() override = default;

    // 链路是否已经可以收发。
    virtual bool isUp() const = 0;
    // 发一条消息。链路没通时返回 false，不替调用方缓存。
    virtual bool send(const QString &topic, const QJsonObject &payload) = 0;

    // 一条消息编码成一行（紧凑 JSON + '\n'）。
    static QByteArray encodeLine(const QString &topic, const QJsonObject &payload);

    // 单行上限。超过它还没等到换行，说明对端发的不是这个协议，整行丢弃。
    static constexpr int kMaxLineBytes = 4096;

signals:
    void upChanged();
    void received(const QString &topic, const QJsonObject &payload);
    // 收到一行但不是合法消息（不是 JSON、没有 topic）。只用于联调，不影响后续解析。
    void malformed(const QByteArray &line);

protected:
    // 子类把收到的原始字节喂进来。一行可能被拆成多个包，也可能一个包里有多行：
    // 这里攒到换行符再解析，解析出一条发一次 received。
    void feed(const QByteArray &bytes);
    // 链路断开时调用：丢掉攒了一半的行，免得和下次连接的数据拼在一起。
    void resetFraming();

private:
    void handleLine(const QByteArray &line);

    QByteArray m_rx;
    bool m_discarding = false;   // 当前这行已经超长，丢到下一个换行为止
};
