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

QTEST_GUILESS_MAIN(TestDeviceLink)
#include "tst_device_link.moc"
