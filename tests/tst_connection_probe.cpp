#include <QtTest>

#include <QTcpServer>
#include <QTcpSocket>

#include "connection_probe.h"

// "测试连接"的探测逻辑：用本机起的假服务器覆盖各种应答，不依赖外网。
class ConnectionProbeTest : public QObject
{
    Q_OBJECT

private slots:
    void httpStatusIsClassified_data();
    void httpStatusIsClassified();
    void refusedPortIsReported();
    void silentServerTimesOut();
    void secondStartWhileBusyIsRejected();

private:
    // 起一个只回固定状态行的服务器；statusLine 为空表示接了连接但一直不说话。
    static QTcpServer *serve(QObject *parent, const QByteArray &statusLine);
};

QTcpServer *ConnectionProbeTest::serve(QObject *parent, const QByteArray &statusLine)
{
    auto *server = new QTcpServer(parent);
    if (!server->listen(QHostAddress::LocalHost))
        return nullptr;
    connect(server, &QTcpServer::newConnection, server, [server, statusLine] {
        QTcpSocket *socket = server->nextPendingConnection();
        if (statusLine.isEmpty())
            return;
        connect(socket, &QTcpSocket::readyRead, socket, [socket, statusLine] {
            socket->write("HTTP/1.1 " + statusLine
                          + "\r\nContent-Length: 2\r\nLocation: /elsewhere\r\nConnection: close\r\n\r\nok");
            socket->disconnectFromHost();
        });
    });
    return server;
}

void ConnectionProbeTest::httpStatusIsClassified_data()
{
    QTest::addColumn<QByteArray>("statusLine");
    QTest::addColumn<int>("status");
    QTest::addColumn<bool>("reachable");
    QTest::addColumn<QString>("code");

    QTest::newRow("200") << QByteArray("200 OK") << 200 << true << QStringLiteral("ok");
    QTest::newRow("301") << QByteArray("301 Moved Permanently") << 301 << true << QStringLiteral("redirect");
    QTest::newRow("401") << QByteArray("401 Unauthorized") << 401 << true << QStringLiteral("auth_required");
    QTest::newRow("404") << QByteArray("404 Not Found") << 404 << true << QStringLiteral("not_found");
    QTest::newRow("405") << QByteArray("405 Method Not Allowed") << 405 << true << QStringLiteral("client_error");
    QTest::newRow("503") << QByteArray("503 Service Unavailable") << 503 << false << QStringLiteral("server_error");
}

void ConnectionProbeTest::httpStatusIsClassified()
{
    QFETCH(QByteArray, statusLine);
    QFETCH(int, status);
    QFETCH(bool, reachable);
    QFETCH(QString, code);

    QTcpServer *server = serve(this, statusLine);
    QVERIFY(server);
    ConnectionProbe probe;
    QSignalSpy done(&probe, &ConnectionProbe::finished);

    QVERIFY(probe.start(QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(server->serverPort()))));
    QVERIFY(probe.busy());
    QVERIFY(done.wait(5000));
    QCOMPARE(done.count(), 1); // 一次探测只回一次结果
    QVERIFY(!probe.busy());

    const QList<QVariant> args = done.first();
    QCOMPARE(args.at(0).toBool(), reachable);
    QCOMPARE(args.at(1).toInt(), status);
    QVERIFY(args.at(2).toInt() >= 0);
    QCOMPARE(args.at(3).toString(), code);
    QVERIFY(!args.at(4).toString().isEmpty());
    delete server;
}

void ConnectionProbeTest::refusedPortIsReported()
{
    // 先占一个端口再关掉，保证这个端口上没人在听。
    QTcpServer holder;
    QVERIFY(holder.listen(QHostAddress::LocalHost));
    const quint16 port = holder.serverPort();
    holder.close();

    ConnectionProbe probe;
    QSignalSpy done(&probe, &ConnectionProbe::finished);
    QVERIFY(probe.start(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port))));
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toBool(), false);
    QCOMPARE(done.first().at(1).toInt(), 0);
    QCOMPARE(done.first().at(3).toString(), QStringLiteral("refused"));
}

void ConnectionProbeTest::silentServerTimesOut()
{
    QTcpServer *server = serve(this, QByteArray());
    QVERIFY(server);
    ConnectionProbe probe;
    QSignalSpy done(&probe, &ConnectionProbe::finished);
    QVERIFY(probe.start(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server->serverPort())), 300));
    QVERIFY(done.wait(3000));
    QCOMPARE(done.count(), 1);
    QCOMPARE(done.first().at(0).toBool(), false);
    QCOMPARE(done.first().at(3).toString(), QStringLiteral("timeout"));
    QVERIFY(!probe.busy());
    delete server;
}

void ConnectionProbeTest::secondStartWhileBusyIsRejected()
{
    QTcpServer *server = serve(this, QByteArray());
    QVERIFY(server);
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/").arg(server->serverPort()));
    ConnectionProbe probe;
    QSignalSpy done(&probe, &ConnectionProbe::finished);
    QVERIFY(probe.start(url, 2000));
    QVERIFY(!probe.start(url, 2000));
    probe.cancel(); // 取消不回结果，之后可以重新开始
    QVERIFY(!probe.busy());
    QCOMPARE(done.count(), 0);
    QVERIFY(probe.start(url, 200));
    QVERIFY(done.wait(3000));
    delete server;
}

QTEST_GUILESS_MAIN(ConnectionProbeTest)
#include "tst_connection_probe.moc"
