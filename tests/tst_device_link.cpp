// 外设板链路的分行/解析：一行一条 JSON，BLE 上一行可能被拆成多个包，
// 一个包里也可能有多行。这里只测 DeviceLink 基类，不需要蓝牙。
#include <QtTest>

#include "device_link.h"

#include <QJsonDocument>
#include <QSignalSpy>

class LoopbackLink : public DeviceLink
{
    Q_OBJECT

public:
    bool isUp() const override { return true; }
    bool send(const QString &topic, const QJsonObject &payload) override
    {
        sent.append(encodeLine(topic, payload));
        return true;
    }
    using DeviceLink::feed;
    using DeviceLink::resetFraming;

    QList<QByteArray> sent;
};

class TestDeviceLink : public QObject
{
    Q_OBJECT

private slots:
    void encodeIsOneLine();
    void wholeLine();
    void splitAcrossPackets();
    void severalLinesInOnePacket();
    void crlfAndBlankLines();
    void malformedLinesDoNotStopParsing();
    void payloadMayBeOmittedButNotWrongType();
    void overlongLineIsDropped();
    void resetDropsHalfLine();
    void roundTripKeepsBigIds();
    void reminderFrameSurvivesChunking();
    void ackAndAlarmPayloadFields();
};

void TestDeviceLink::encodeIsOneLine()
{
    const QByteArray line = DeviceLink::encodeLine(
        QStringLiteral("home/reminder/term1"),
        QJsonObject{{QStringLiteral("title"), QStringLiteral("吃药\n饭后")}});
    QVERIFY(line.endsWith('\n'));
    QCOMPARE(line.count('\n'), 1);   // 字符串里的换行被转义，不会把一条消息切成两行
}

void TestDeviceLink::wholeLine()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    link.feed("{\"topic\":\"home/sensor/presence\",\"payload\":{\"room\":\"living\",\"present\":true}}\n");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("home/sensor/presence"));
    const QJsonObject payload = spy.at(0).at(1).toJsonObject();
    QCOMPARE(payload.value(QStringLiteral("room")).toString(), QStringLiteral("living"));
    QCOMPARE(payload.value(QStringLiteral("present")).toBool(), true);
}

void TestDeviceLink::splitAcrossPackets()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    const QByteArray line = DeviceLink::encodeLine(
        QStringLiteral("home/ack/term1"),
        QJsonObject{{QStringLiteral("action"), QStringLiteral("confirm")}});
    // 按 20 字节一包喂（没协商 MTU 时 BLE 的默认包长）
    for (int i = 0; i < line.size(); i += 20) {
        QCOMPARE(spy.count(), 0);
        link.feed(line.mid(i, 20));
    }
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("home/ack/term1"));
}

void TestDeviceLink::severalLinesInOnePacket()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    link.feed("{\"topic\":\"a\",\"payload\":{}}\n{\"topic\":\"b\",\"payload\":{}}\n{\"topic\":\"c\"");
    QCOMPARE(spy.count(), 2);
    link.feed(",\"payload\":{}}\n");
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("c"));
}

void TestDeviceLink::crlfAndBlankLines()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    QSignalSpy bad(&link, &DeviceLink::malformed);
    link.feed("\r\n\n{\"topic\":\"a\",\"payload\":{}}\r\n   \n");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(bad.count(), 0);
}

void TestDeviceLink::malformedLinesDoNotStopParsing()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    QSignalSpy bad(&link, &DeviceLink::malformed);
    link.feed("E (123) radar: frame error\n[1,2,3]\n{\"payload\":{}}\n{\"topic\":\"ok\",\"payload\":{}}\n");
    QCOMPARE(bad.count(), 3);   // 调试打印、不是对象、没有 topic
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("ok"));
}

void TestDeviceLink::payloadMayBeOmittedButNotWrongType()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    QSignalSpy bad(&link, &DeviceLink::malformed);
    link.feed("{\"topic\":\"ping\"}\n{\"topic\":\"ping\",\"payload\":5}\n");
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toJsonObject().isEmpty());
    QCOMPARE(bad.count(), 1);
}

void TestDeviceLink::overlongLineIsDropped()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    QSignalSpy bad(&link, &DeviceLink::malformed);
    // 一直没有换行，超过上限：整行丢弃，不无限攒内存
    link.feed(QByteArray(DeviceLink::kMaxLineBytes + 100, 'x'));
    link.feed(QByteArray(500, 'y'));
    link.feed("tail of the long line\n{\"topic\":\"after\",\"payload\":{}}\n");
    QCOMPARE(bad.count(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("after"));
}

void TestDeviceLink::resetDropsHalfLine()
{
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    QSignalSpy bad(&link, &DeviceLink::malformed);
    link.feed("{\"topic\":\"half\",\"pay");
    link.resetFraming();   // 断线重连：半行不能和新连接的数据拼在一起
    link.feed("{\"topic\":\"fresh\",\"payload\":{}}\n");
    QCOMPARE(bad.count(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("fresh"));
}

void TestDeviceLink::roundTripKeepsBigIds()
{
    // occurrenceId = planId * 1e9 + ...，远超 32 位，但 < 2^53，过一遍 JSON 不能走样
    const qint64 occurrenceId = 12000000881LL;
    const qint64 ts = 1794800000000LL;
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    link.send(QStringLiteral("home/reminder/term1"),
              QJsonObject{{QStringLiteral("occurrence_id"), occurrenceId},
                          {QStringLiteral("due_ts"), ts}});
    QCOMPARE(link.sent.size(), 1);
    QVERIFY(link.sent.first().contains("12000000881"));   // 不是 1.2e+10 这种写法
    link.feed(link.sent.first());
    QCOMPARE(spy.count(), 1);
    const QJsonObject payload = spy.at(0).at(1).toJsonObject();
    QCOMPARE(payload.value(QStringLiteral("occurrence_id")).toInteger(), occurrenceId);
    QCOMPARE(payload.value(QStringLiteral("due_ts")).toInteger(), ts);
}

void TestDeviceLink::reminderFrameSurvivesChunking()
{
    // B2-5：home/reminder/<device> 的完整 payload，按 BLE 20 字节分包后能逐字还原。
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    const QJsonObject payload{
        {QStringLiteral("occurrence_id"), 12000000881LL},
        {QStringLiteral("title"), QStringLiteral("吃降压药，一次一片")},
        {QStringLiteral("state"), QStringLiteral("notified")},
        {QStringLiteral("light_color"), QStringLiteral("yellow")},
        {QStringLiteral("due_ts"), 1794800000000LL},
    };
    link.send(QStringLiteral("home/reminder/board1"), payload);
    const QByteArray frame = link.sent.first();
    for (int i = 0; i < frame.size(); i += 20)
        link.feed(frame.mid(i, 20));   // 一包 20 字节，模拟 Nordic UART 的 MTU
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("home/reminder/board1"));
    const QJsonObject got = spy.at(0).at(1).toJsonObject();
    QCOMPARE(got.value(QStringLiteral("occurrence_id")).toInteger(), 12000000881LL);
    QCOMPARE(got.value(QStringLiteral("title")).toString(), QStringLiteral("吃降压药，一次一片"));
    QCOMPARE(got.value(QStringLiteral("light_color")).toString(), QStringLiteral("yellow"));
    QCOMPARE(got.value(QStringLiteral("due_ts")).toInteger(), 1794800000000LL);
}

void TestDeviceLink::ackAndAlarmPayloadFields()
{
    // B2-6：板子按键上报的两种帧，字段不能丢（大 id 用 toInteger 拿）。
    LoopbackLink link;
    QSignalSpy spy(&link, &DeviceLink::received);
    link.send(QStringLiteral("home/ack/board1"),
              QJsonObject{{QStringLiteral("occurrence_id"), 12000000881LL},
                          {QStringLiteral("action"), QStringLiteral("confirm")},
                          {QStringLiteral("ts"), 1794800000000LL}});
    link.send(QStringLiteral("home/alarm"),
              QJsonObject{{QStringLiteral("level"), 3},
                          {QStringLiteral("occurrence_id"), 0},
                          {QStringLiteral("ts"), 1794800000000LL}});
    QCOMPARE(link.sent.size(), 2);
    link.feed(link.sent.at(0));
    link.feed(link.sent.at(1));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("home/ack/board1"));
    const QJsonObject ack = spy.at(0).at(1).toJsonObject();
    QCOMPARE(ack.value(QStringLiteral("occurrence_id")).toInteger(), 12000000881LL);
    QCOMPARE(ack.value(QStringLiteral("action")).toString(), QStringLiteral("confirm"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("home/alarm"));
    QCOMPARE(spy.at(1).at(1).toJsonObject().value(QStringLiteral("level")).toInt(), 3);
}

QTEST_GUILESS_MAIN(TestDeviceLink)
#include "tst_device_link.moc"
