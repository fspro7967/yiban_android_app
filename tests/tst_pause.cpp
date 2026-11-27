// B1-2 暂停模式（R1-7）的回归用例：
//   * 暂停期内到点的提醒不弹、不升级、不产生 missed
//   * 暂停前已经在响的提醒，暂停后也要停下升级链并避免记漏
//   * 暂停结束后到点的提醒照常弹；到期要发信号并清值
//   * 缩短暂停要把不该暂停的标记去掉；暂停前就欠着的不算暂停
//   * 暂停中手动确认算完成；推迟推到暂停结束之后
//   * 恢复时按"有效窗口"判断，窗口还在就还能提醒
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
    // 以下对应用户 review 的第 1~7 条
    void notifiedReminderStopsEscalation();
    void currentCardSkipsPaused();
    void shorteningPauseUnmarksFuture();
    void pauseExpiryEmitsAndClears();
    void confirmDuringPauseCounts();
    void prePauseDebtNotPaused();
    void resumeWithinWindowStillFires();
    void snoozeDuringPauseFiresAfterPause();
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

    QCOMPARE(due.count(), 0);
    const QVariantMap occ = occurrenceFor(planId);
    QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
    QVERIFY(occ.value(QStringLiteral("paused")).toBool());
    QCOMPARE(core->pendingCount(), 0);
    QCOMPARE(core->todayTotal(), 0);
}

void PauseTest::reminderAfterPauseFires()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(10, 5));
    QVERIFY(planId > 0);
    QVERIFY(core->pauseUntil(at(10, 0)));

    core->setClockForTests(at(10, 5));
    core->tickForTests();
    QVERIFY(!core->paused());
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

    core->setClockForTests(at(11, 0));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("pending"));

    const QVariantMap day = core->stats(7).last().toMap();
    QCOMPARE(day.value(QStringLiteral("missed")).toInt(), 0);
    QCOMPARE(day.value(QStringLiteral("paused")).toInt(), 1);
    QCOMPARE(day.value(QStringLiteral("total")).toInt(), 0);
    QCOMPARE(day.value(QStringLiteral("rate")).toInt(), 0);
}

// 1. 暂停前已经在响的提醒：暂停后不升级、不记漏服
void PauseTest::notifiedReminderStopsEscalation()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);

    core->setClockForTests(at(9, 10));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));

    // 9:12 开暂停：已经在响的这条也要停下升级链
    core->setClockForTests(at(9, 12));
    QVERIFY(core->pauseUntil(at(23, 0)));
    const QVariantMap paused = occurrenceFor(planId);
    QVERIFY(paused.value(QStringLiteral("paused")).toBool());
    QCOMPARE(paused.value(QStringLiteral("escalationLevel")).toInt(), 0);

    // 9:25（本该升 2 级）、10:00（窗口早过、本该 missed）都不动
    core->setClockForTests(at(9, 25));
    core->tickForTests();
    core->setClockForTests(at(10, 0));
    core->tickForTests();

    const QVariantMap now = occurrenceFor(planId);
    QCOMPARE(now.value(QStringLiteral("state")).toString(), QStringLiteral("notified"));
    QCOMPARE(now.value(QStringLiteral("escalationLevel")).toInt(), 0);
    QVERIFY(now.value(QStringLiteral("paused")).toBool());

    bool escalated = false;
    for (const QVariant &entry : core->eventLog()) {
        const QVariantMap e = entry.toMap();
        if (e.value(QStringLiteral("action")).toString() == QStringLiteral("escalate"))
            escalated = true;
    }
    QVERIFY(!escalated);
    const QVariantMap day = core->stats(7).last().toMap();
    QCOMPARE(day.value(QStringLiteral("missed")).toInt(), 0);
    QCOMPARE(day.value(QStringLiteral("paused")).toInt(), 1);
}

// 2. 首页"下一条"不能是暂停中的提醒
void PauseTest::currentCardSkipsPaused()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);
    QVERIFY(core->pauseUntil(at(23, 0)));

    core->setClockForTests(at(9, 10));
    core->tickForTests();

    QVERIFY(core->currentOccurrence().isEmpty());
    const QVariantMap occ = occurrenceFor(planId);
    QVERIFY(occ.value(QStringLiteral("paused")).toBool());
    QCOMPARE(occ.value(QStringLiteral("canConfirm")).toBool(), false);
    QCOMPARE(occ.value(QStringLiteral("canSnooze")).toBool(), false);
}

// 3. 缩短暂停时长后，落在新截止时间之后的提醒要恢复调度
void PauseTest::shorteningPauseUnmarksFuture()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(14, 0));
    QVERIFY(planId > 0);

    QVERIFY(core->pauseUntil(QDateTime(kDay.addDays(5), QTime(9, 0)).toMSecsSinceEpoch()));
    QVERIFY(occurrenceFor(planId).value(QStringLiteral("paused")).toBool());

    // 改成暂停到今天 12:00：14:00 的提醒不该再被暂停
    QVERIFY(core->pauseUntil(at(12, 0)));
    QVERIFY(!occurrenceFor(planId).value(QStringLiteral("paused")).toBool());

    core->setClockForTests(at(14, 0));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));
}

// 4. 暂停到期要发 pausedChanged、清值，之后新建的过期计划不被误标
void PauseTest::pauseExpiryEmitsAndClears()
{
    AppCore *core = AppCore::instance();
    QVERIFY(core->pauseUntil(at(10, 0)));

    QSignalSpy spy(core, &AppCore::pausedChanged);
    core->setClockForTests(at(10, 5));
    core->tickForTests();

    QVERIFY(spy.count() >= 1);
    QCOMPARE(core->pauseUntilMs(), 0LL);
    QVERIFY(!core->paused());

    const qint64 planId = core->createPlan(planAt(9, 30)); // 已过点的新计划
    QVERIFY(planId > 0);
    const QVariantMap occ = occurrenceFor(planId);
    QVERIFY(!occ.value(QStringLiteral("paused")).toBool());
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));
}

// 5. 暂停期间手动确认：算完成，不再算"已暂停"
void PauseTest::confirmDuringPauseCounts()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);
    QVERIFY(core->pauseUntil(at(23, 0)));

    core->setClockForTests(at(9, 10));
    core->tickForTests();
    const qint64 occId = occurrenceFor(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QVERIFY(core->confirm(occId));

    QCOMPARE(core->todayDone(), 1);
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("confirmed"));
    QVERIFY(!occurrenceFor(planId).value(QStringLiteral("paused")).toBool());

    const QVariantMap day = core->stats(7).last().toMap();
    QCOMPARE(day.value(QStringLiteral("done")).toInt(), 1);
    QCOMPARE(day.value(QStringLiteral("paused")).toInt(), 0);
}

// 6. 暂停开始前就欠着的 pending 不能被抹成"已暂停"
void PauseTest::prePauseDebtNotPaused()
{
    AppCore *core = AppCore::instance();
    core->setClockForTests(at(8, 0));
    core->tickForTests();
    const qint64 planId = core->createPlan(planAt(8, 0));
    QVERIFY(planId > 0);
    core->setAutoReminders(false); // 模拟"自动提醒关着"，8:00 的提醒一直 pending

    core->setClockForTests(at(21, 0));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("pending"));

    QVERIFY(core->pauseUntil(at(23, 0)));
    QVERIFY(!occurrenceFor(planId).value(QStringLiteral("paused")).toBool());
    core->setAutoReminders(true);
}

// 7. 恢复时看有效窗口：窗口还在就还能提醒
void PauseTest::resumeWithinWindowStillFires()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);

    core->setClockForTests(at(9, 10));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));

    core->setClockForTests(at(9, 12));
    QVERIFY(core->pauseUntil(at(23, 0)));   // 在响的提醒被暂停
    QVERIFY(occurrenceFor(planId).value(QStringLiteral("paused")).toBool());

    QVERIFY(core->resumeReminders());        // 9:12 恢复，窗口到 9:40 还没过
    const QVariantMap occ = occurrenceFor(planId);
    QVERIFY(!occ.value(QStringLiteral("paused")).toBool());
    QCOMPARE(occ.value(QStringLiteral("canConfirm")).toBool(), true);
}

// 追加：暂停中推迟 → 推到暂停结束之后，不会永远不响
void PauseTest::snoozeDuringPauseFiresAfterPause()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(9, 10));
    QVERIFY(planId > 0);
    QVERIFY(core->pauseUntil(at(10, 0)));

    core->setClockForTests(at(9, 10));
    core->tickForTests();
    const qint64 occId = occurrenceFor(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QVERIFY(core->snooze(occId));
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("snoozeUntilTs")).toLongLong(), at(10, 0));
    QVERIFY(!occurrenceFor(planId).value(QStringLiteral("paused")).toBool());

    core->setClockForTests(at(10, 0));
    core->tickForTests();
    QCOMPARE(occurrenceFor(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));
}

QTEST_GUILESS_MAIN(PauseTest)
#include "tst_pause.moc"
