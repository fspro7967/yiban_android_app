// B1-2 暂停模式（R1-7）的回归用例：
//   * 暂停期内到点的提醒不弹、不升级、不产生 missed
//   * 暂停结束后到点的提醒照常弹
//   * 手动恢复后照常调度
//   * 统计不把"已暂停"算成漏服（也不进分母）
#include <QtTest>

#include <QDate>
#include <QDateTime>
#include <QSignalSpy>
#include <QTime>
#include <QVariantMap>

#include "app_core.h"

namespace {

const QDate kDay(2026, 12, 2);

qint64 at(int hour, int minute)
{
    return QDateTime(kDay, QTime(hour, minute)).toMSecsSinceEpoch();
}

QVariantMap planAt(int hour, int minute)
{
    QVariantMap rule;
    rule.insert(QStringLiteral("kind"), QStringLiteral("daily"));
    rule.insert(QStringLiteral("times"), QStringList{
        QStringLiteral("%1:%2").arg(hour, 2, 10, QLatin1Char('0'))
                               .arg(minute, 2, 10, QLatin1Char('0'))});
    QVariantMap plan;
    plan.insert(QStringLiteral("title"), QStringLiteral("吃药"));
    plan.insert(QStringLiteral("rule"), rule);
    plan.insert(QStringLiteral("windowMin"), 30);
    plan.insert(QStringLiteral("escalateAfterMin"), 15);
    return plan;
}

QVariantMap occurrenceFor(qint64 planId)
{
    for (const QVariant &entry : AppCore::instance()->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() == planId)
            return occ;
    }
    return {};
}

} // namespace

class PauseTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void pausedReminderDoesNotFire();
    void reminderAfterPauseFires();
    void manualResumeRestoresScheduling();
    void pausedNotCountedAsMissed();
};

void PauseTest::init()
{
    AppCore *core = AppCore::instance();
    core->setClockForTests(at(9, 0));
    core->resetStorage();
    core->tickForTests();
}

void PauseTest::pausedReminderDoesNotFire()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);

    QSignalSpy due(core, &AppCore::reminderDue);
    QVERIFY(core->pauseUntil(at(23, 0)));
    QVERIFY(core->paused());

    core->setClockForTests(at(9, 10));
    core->tickForTests();

    QCOMPARE(due.count(), 0); // 暂停期间不弹
    const QVariantMap occ = occurrenceFor(planId);
    QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
    QVERIFY(occ.value(QStringLiteral("paused")).toBool());
    // 不进待办、不进分母
    QCOMPARE(core->pendingCount(), 0);
    QCOMPARE(core->todayTotal(), 0);
}

void PauseTest::reminderAfterPauseFires()
{
    AppCore *core = AppCore::instance();
    // 暂停到 10:00，10:05 的提醒不在暂停窗口内，应该照常弹
    const qint64 planId = core->createPlan(planAt(10, 5));
    QVERIFY(planId > 0);
    QVERIFY(core->pauseUntil(at(10, 0)));

    core->setClockForTests(at(10, 5));
    core->tickForTests();
    QVERIFY(!core->paused()); // 已到期自动恢复
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));
}

void PauseTest::manualResumeRestoresScheduling()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(10, 30));
    QVERIFY(planId > 0);

    const qint64 until = at(23, 0);
    QVERIFY(core->pauseUntil(until));
    QCOMPARE(core->pauseUntilMs(), until);
    QVERIFY(core->resumeReminders());
    QVERIFY(!core->paused());
    QCOMPARE(core->pauseUntilMs(), 0LL);

    core->setClockForTests(at(10, 30));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));
}

void PauseTest::pausedNotCountedAsMissed()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);
    QVERIFY(core->pauseUntil(at(23, 0)));

    // 即使过了窗口也没有 missed、没有升级
    core->setClockForTests(at(11, 0));
    core->tickForTests();
    const QVariantMap occ = occurrenceFor(planId);
    QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));

    const QVariantMap day = core->stats(7).last().toMap();
    QCOMPARE(day.value(QStringLiteral("missed")).toInt(), 0);
    QCOMPARE(day.value(QStringLiteral("paused")).toInt(), 1);
    QCOMPARE(day.value(QStringLiteral("total")).toInt(), 0);
    QCOMPARE(day.value(QStringLiteral("rate")).toInt(), 0);
}

QTEST_GUILESS_MAIN(PauseTest)
#include "tst_pause.moc"
