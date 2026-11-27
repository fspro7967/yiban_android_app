// 跨进程重启验证：同一个库文件，连续 10 次"启动 → 校验 → 写一条 → 退出"。
// 每次子进程都重新构造 AppCore（等价于真实重启：新进程、新内存、老 SQLite）。
// 同时验证暂停状态跨重启保留（B1-2 验收：暂停期间重启仍处于暂停）。
#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>

#include "app_core.h"

namespace {

int runChild(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int round = argc > 2 ? QByteArray(argv[2]).toInt() : 0;
    AppCore *core = AppCore::instance();

    const int cups = core->waterCups();
    if (cups != round) {
        fprintf(stderr, "CHILD mismatch: got %d expected %d\n", cups, round);
        return 3;
    }

    if (round == 0) {
        if (core->pauseUntilMs() != 0) {
            fprintf(stderr, "CHILD pause should start empty\n");
            return 4;
        }
        core->pauseUntil(QDateTime::currentMSecsSinceEpoch() + 7LL * 24 * 3600 * 1000);
    } else if (core->pauseUntilMs() <= 0) {
        fprintf(stderr, "CHILD pause lost after restart\n");
        return 5;
    }

    core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    // schedulePersist 是 singleShot(0)，给它几轮事件循环把库写完再退出
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
    printf("CHILD ok cups=%d pause=%lld\n", core->waterCups(),
           static_cast<long long>(core->pauseUntilMs()));
    return 0;
}

} // namespace

class RestartTest : public QObject
{
    Q_OBJECT

private slots:
    void tenRestartsKeepData();
};

void RestartTest::tenRestartsKeepData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("app_core.sqlite"));

    for (int round = 0; round < 10; ++round) {
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("YIBAN_APP_DB"), dbPath);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--child"), QString::number(round)});
        QVERIFY2(child.waitForFinished(60000),
                 qPrintable(QStringLiteral("第 %1 次子进程超时").arg(round)));
        const QByteArray stderrText = child.readAllStandardError();
        QVERIFY2(child.exitCode() == 0,
                 qPrintable(QStringLiteral("第 %1 次重启后数据不对：%2")
                                .arg(round)
                                .arg(QString::fromLocal8Bit(stderrText))));
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && qstrcmp(argv[1], "--child") == 0)
        return runChild(argc, argv);

    QCoreApplication app(argc, argv);
    RestartTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_restart.moc"
