#include <QtTest>

#include <QDate>
#include <QTime>

#include "app_core.h"

// 这些用例锁的是 facade 契约（形状 + 状态机 + 错误码），不是像素。
// 后端替换 AppCore 内部实现（SQLite / 同步队列）后，必须原样通过。

namespace {

QVariantMap dailyRule(const QString &hhmm)
{
    QVariantMap rule;
    rule.insert(QStringLiteral("kind"), QStringLiteral("daily"));
    rule.insert(QStringLiteral("times"), QStringList{hhmm});
    return rule;
}

QVariantMap planMap(const QString &title, const QVariantMap &rule)
{
    QVariantMap plan;
    plan.insert(QStringLiteral("title"), title);
    plan.insert(QStringLiteral("rule"), rule);
    plan.insert(QStringLiteral("windowMin"), 30);
    plan.insert(QStringLiteral("escalateAfterMin"), 15);
    return plan;
}

} // namespace

class FacadeCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void seedShape();
    void confirmUndoTransition();
    void snoozeLimit();
    void escalateRequiresOpenState();
    void createPlanValidation();
    void createdOccurrenceIdEncodesPlan();
    void deletePlanClearsOccurrences();
    void recordLifecycle();
    void statsShape();
    void repeatedConfirmLogsSingleEvent();
    void unknownOccurrenceIsRejected();
    void addingPlanKeepsTodaysProgress();
    void nextDayPreviewAfterTodayDone();
    void demoDomainsAreIsolated();
    void restorePlanKeepsProgress();
    void statsTodayFollowsOccurrences();
    void realStatsStartFromFirstUse();

private:
    // 建一条 5~30 分钟之后的 daily 计划，返回 planId；临近午夜返回 0（调用方 QSKIP）。
    qint64 createPlanAhead(int minutesAhead, QString *hhmm = nullptr);
    QVariantMap occurrenceForPlan(qint64 planId) const;
};


void FacadeCoreTest::demoDomainsAreIsolated()
{
    AppCore *core = AppCore::instance();

    core->setDemoMode(false);
    QVERIFY(!core->demoMode());
    QCOMPARE(core->plans().size(), 0);
    QCOMPARE(core->todayTotal(), 0);
    QCOMPARE(core->waterCups(), 0);

    const qint64 realPlanId = createPlanAhead(20);
    if (realPlanId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");
    QCOMPARE(core->plans().size(), 1);
    QVERIFY(!occurrenceForPlan(realPlanId).isEmpty());

    core->setProfile(QStringLiteral("真实奶奶"), QStringLiteral("真实家人"),
                     QStringLiteral("139 0000 0000"));
    QCOMPARE(core->caregiverName(), QStringLiteral("真实家人"));

    core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    QCOMPARE(core->waterCups(), 1);

    core->setDemoMode(true);
    QVERIFY(core->demoMode());
    QCOMPARE(core->caregiverName(), QStringLiteral("女儿 陈晓"));
    QCOMPARE(core->todayTotal(), 4);
    QCOMPARE(core->plans().size(), 4);
    core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    QCOMPARE(core->waterCups(), 4);

    // 两个域的计划 id 都从 1 开始；确认演示 occurrence 后切回真实域，
    // 真实域的同 id occurrence 不能被演示状态污染。
    qint64 demoOccurrenceId = 0;
    for (const QVariant &entry : core->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("state")).toString() != QStringLiteral("confirmed")) {
            demoOccurrenceId = occ.value(QStringLiteral("occurrenceId")).toLongLong();
            break;
        }
    }
    QVERIFY(demoOccurrenceId > 0);
    QVERIFY(core->confirm(demoOccurrenceId));

    core->setDemoMode(false);
    QVERIFY(!core->demoMode());
    QCOMPARE(core->plans().size(), 1);
    QCOMPARE(core->waterCups(), 1);
    QCOMPARE(core->caregiverName(), QStringLiteral("真实家人"));
    const QVariantMap realOccurrence = occurrenceForPlan(realPlanId);
    QVERIFY(!realOccurrence.isEmpty());
    QCOMPARE(realOccurrence.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));

    core->setDemoMode(true);
    QCOMPARE(core->waterCups(), 4);
    QCOMPARE(core->plans().size(), 4);
    bool demoStillConfirmed = false;
    for (const QVariant &entry : core->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("occurrenceId")).toLongLong() == demoOccurrenceId
            && occ.value(QStringLiteral("state")).toString() == QStringLiteral("confirmed"))
            demoStillConfirmed = true;
    }
    QVERIFY(demoStillConfirmed);
}

void FacadeCoreTest::init()
{
    AppCore::instance()->resetDemo();
}

qint64 FacadeCoreTest::createPlanAhead(int minutesAhead, QString *hhmm)
{
    const QTime due = QTime::currentTime().addSecs(minutesAhead * 60);
    // 跨天就跳过：今天的清单里不会有这条 occurrence。
    if (QTime::currentTime().secsTo(due) < 0)
        return 0;

    const QString time = due.toString(QStringLiteral("HH:mm"));
    if (hhmm)
        *hhmm = time;
    return AppCore::instance()->createPlan(planMap(QStringLiteral("测试提醒"), dailyRule(time)));
}

QVariantMap FacadeCoreTest::occurrenceForPlan(qint64 planId) const
{
    for (const QVariant &entry : AppCore::instance()->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() == planId)
            return occ;
    }
    return {};
}

void FacadeCoreTest::seedShape()
{
    AppCore *core = AppCore::instance();

    QCOMPARE(core->todayTotal(), 4);
    QCOMPARE(core->todayDone(), 2);
    QCOMPARE(core->occurrencesToday().size(), 4);

    const QSet<QString> allowed{QStringLiteral("pending"), QStringLiteral("notified"),
                                QStringLiteral("confirmed"), QStringLiteral("snoozed"),
                                QStringLiteral("missed"), QStringLiteral("escalated")};
    for (const QVariant &entry : core->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        QVERIFY(occ.value(QStringLiteral("occurrenceId")).toLongLong() > 0);
        QVERIFY(occ.value(QStringLiteral("planId")).toLongLong() > 0);
        QVERIFY(!occ.value(QStringLiteral("title")).toString().isEmpty());
        QVERIFY(occ.value(QStringLiteral("dueTs")).toLongLong() > 0);
        QVERIFY(allowed.contains(occ.value(QStringLiteral("state")).toString()));
        QVERIFY(occ.contains(QStringLiteral("canConfirm")));
        QVERIFY(occ.contains(QStringLiteral("canSnooze")));
        QVERIFY(!occ.value(QStringLiteral("repeatText")).toString().isEmpty());
    }
}

void FacadeCoreTest::confirmUndoTransition()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = createPlanAhead(10);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    const QVariantMap occ = occurrenceForPlan(planId);
    QVERIFY(!occ.isEmpty());
    const qint64 id = occ.value(QStringLiteral("occurrenceId")).toLongLong();
    QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));

    QString error;
    QVERIFY(core->confirm(id, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(occurrenceForPlan(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("confirmed"));

    // 连点：状态机拒绝二次确认，而不是再记一笔。
    QVERIFY(!core->confirm(id, &error));
    QCOMPARE(error, QStringLiteral("already_confirmed"));

    QVERIFY(core->undoOccurrence(id, &error));
    QCOMPARE(occurrenceForPlan(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("notified"));
    QVERIFY(core->undoOccurrence(id, &error));
    QCOMPARE(occurrenceForPlan(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("pending"));

    QVERIFY(!core->undoOccurrence(id, &error));
    QCOMPARE(error, QStringLiteral("nothing_to_undo"));
}

void FacadeCoreTest::snoozeLimit()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = createPlanAhead(20);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    const qint64 id = occurrenceForPlan(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QVERIFY(id > 0);

    QString error;
    for (int i = 1; i <= 3; ++i) {
        QVERIFY2(core->snooze(id, &error), qPrintable(error));
        const QVariantMap occ = occurrenceForPlan(planId);
        QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("snoozed"));
        QCOMPARE(occ.value(QStringLiteral("snoozeCount")).toInt(), i);
        QCOMPARE(occ.value(QStringLiteral("snoozeRemaining")).toInt(), 3 - i);
    }

    QVERIFY(!core->snooze(id, &error));
    QCOMPARE(error, QStringLiteral("snooze_limit"));
    QCOMPARE(occurrenceForPlan(planId).value(QStringLiteral("snoozeCount")).toInt(), 3);
}

void FacadeCoreTest::escalateRequiresOpenState()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = createPlanAhead(25);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    const qint64 id = occurrenceForPlan(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QString error;

    // pending 还不能升级：必须先到点提醒/推迟/错过。
    QVERIFY(!core->escalate(id, &error));
    QCOMPARE(error, QStringLiteral("not_escalatable"));

    QVERIFY(core->snooze(id, &error));
    QVERIFY(core->escalate(id, &error));
    QCOMPARE(occurrenceForPlan(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("escalated"));
}

void FacadeCoreTest::createPlanValidation()
{
    AppCore *core = AppCore::instance();
    QString error;

    QCOMPARE(core->createPlan(planMap(QString(), dailyRule(QStringLiteral("09:00"))), &error), 0);
    QCOMPARE(error, QStringLiteral("empty_title"));

    QVariantMap bogusRule;
    bogusRule.insert(QStringLiteral("kind"), QStringLiteral("bogus"));
    QCOMPARE(core->createPlan(planMap(QStringLiteral("坏规则"), bogusRule), &error), 0);
    QCOMPARE(error, QStringLiteral("bad_rule_kind"));

    QVariantMap noTimes;
    noTimes.insert(QStringLiteral("kind"), QStringLiteral("daily"));
    noTimes.insert(QStringLiteral("times"), QStringList());
    QCOMPARE(core->createPlan(planMap(QStringLiteral("没时间"), noTimes), &error), 0);
    QCOMPARE(error, QStringLiteral("empty_times"));

    QVariantMap weekly;
    weekly.insert(QStringLiteral("kind"), QStringLiteral("weekly"));
    weekly.insert(QStringLiteral("weekdays"), QVariantList{1, 3, 5});
    weekly.insert(QStringLiteral("times"), QStringList{QStringLiteral("09:00")});
    const qint64 planId = core->createPlan(planMap(QStringLiteral("每周三次"), weekly), &error);
    QVERIFY2(planId > 0, qPrintable(error));

    QVariantMap stored;
    for (const QVariant &entry : core->plans()) {
        const QVariantMap plan = entry.toMap();
        if (plan.value(QStringLiteral("planId")).toLongLong() == planId)
            stored = plan;
    }
    QVERIFY(!stored.isEmpty());
    QCOMPARE(stored.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(stored.value(QStringLiteral("deleted")).toBool(), false);
    QCOMPARE(stored.value(QStringLiteral("repeatText")).toString(), QStringLiteral("周一、周三、周五"));
}

void FacadeCoreTest::createdOccurrenceIdEncodesPlan()
{
    const qint64 planId = createPlanAhead(15);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    const QVariantMap occ = occurrenceForPlan(planId);
    QVERIFY(!occ.isEmpty());
    const qint64 id = occ.value(QStringLiteral("occurrenceId")).toLongLong();

    // occurrence_id = plan_id * 10^9 + (julian_day % 10^7) * 100 + 当天序号
    QCOMPARE(id / 1000000000, planId);
    QCOMPARE((id % 1000000000) / 100, QDate::currentDate().toJulianDay() % 10000000);
}

void FacadeCoreTest::deletePlanClearsOccurrences()
{
    const qint64 planId = createPlanAhead(15);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    QVERIFY(!occurrenceForPlan(planId).isEmpty());

    QString error;
    QVERIFY2(AppCore::instance()->deletePlan(planId, &error), qPrintable(error));
    QVERIFY(occurrenceForPlan(planId).isEmpty());

    QVERIFY(!AppCore::instance()->deletePlan(planId, &error));
    QCOMPARE(error, QStringLiteral("plan_not_found"));
}

void FacadeCoreTest::recordLifecycle()
{
    AppCore *core = AppCore::instance();
    const int before = core->records().size();

    const qint64 recordId = core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    QVERIFY(recordId > 0);
    QCOMPARE(core->records().size(), before + 1);

    QString error;
    QVariantMap patch;
    patch.insert(QStringLiteral("value"), 2);
    QVERIFY2(core->correctRecord(recordId, patch, &error), qPrintable(error));
    QVERIFY(core->removeRecord(recordId));
    QCOMPARE(core->records().size(), before);
    QVERIFY(!core->removeRecord(recordId));
}

void FacadeCoreTest::statsShape()
{
    AppCore *core = AppCore::instance();

    const QVariantList week = core->stats(7);
    QCOMPARE(week.size(), 7);
    QCOMPARE(core->stats(30).size(), 30);

    for (const QVariant &entry : week) {
        const QVariantMap day = entry.toMap();
        QVERIFY(!day.value(QStringLiteral("date")).toString().isEmpty());
        QVERIFY(day.contains(QStringLiteral("done")));
        QVERIFY(day.contains(QStringLiteral("total")));
        QVERIFY(day.contains(QStringLiteral("rate")));
        QVERIFY(day.contains(QStringLiteral("expected")));
        QVERIFY(day.contains(QStringLiteral("confirmed")));
        QVERIFY(day.contains(QStringLiteral("missed")));
        QCOMPARE(day.value(QStringLiteral("expected")).toInt(),
                 day.value(QStringLiteral("total")).toInt());
        QCOMPARE(day.value(QStringLiteral("confirmed")).toInt(),
                 day.value(QStringLiteral("done")).toInt());
        QVERIFY(day.value(QStringLiteral("rate")).toInt() >= 0);
        QVERIFY(day.value(QStringLiteral("rate")).toInt() <= 100);
        QVERIFY(day.value(QStringLiteral("done")).toInt() <= day.value(QStringLiteral("total")).toInt());
    }
}

void FacadeCoreTest::repeatedConfirmLogsSingleEvent()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = createPlanAhead(15);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    const qint64 id = occurrenceForPlan(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QVERIFY(core->confirm(id));
    QVERIFY(!core->confirm(id)); // 连点第二下被拒绝

    int confirms = 0;
    for (const QVariant &entry : core->eventLog()) {
        const QVariantMap event = entry.toMap();
        if (event.value(QStringLiteral("occurrenceId")).toLongLong() == id
            && event.value(QStringLiteral("action")).toString() == QStringLiteral("confirm")) {
            ++confirms;
        }
    }
    QCOMPARE(confirms, 1);
}

void FacadeCoreTest::unknownOccurrenceIsRejected()
{
    QString error;
    QVERIFY(!AppCore::instance()->confirm(123456789, &error));
    QCOMPARE(error, QStringLiteral("occurrence_not_found"));
}

void FacadeCoreTest::nextDayPreviewAfterTodayDone()
{
    AppCore *core = AppCore::instance();

    for (const QVariant &entry : core->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("confirmed"))
            continue;
        QVERIFY(core->confirm(occ.value(QStringLiteral("occurrenceId")).toLongLong()));
    }
    QVERIFY(core->currentOccurrence().isEmpty());
    QCOMPARE(core->todayDone(), core->todayTotal());

    const QVariantMap next = core->nextUpcomingOccurrence();
    QVERIFY(!next.isEmpty());
    QCOMPARE(next.value(QStringLiteral("title")).toString(), QStringLiteral("晨起喝水"));
    QCOMPARE(next.value(QStringLiteral("dayLabel")).toString(), QStringLiteral("明天"));
    QVERIFY(next.value(QStringLiteral("isTomorrow")).toBool());
    QVERIFY(next.value(QStringLiteral("preview")).toBool());
    QVERIFY(next.value(QStringLiteral("dueTs")).toLongLong() > core->nowMs());
    QVERIFY(next.value(QStringLiteral("diffMinutes")).toInt() >= 0);
    QVERIFY(!next.value(QStringLiteral("canConfirm")).toBool());
    QVERIFY(!next.value(QStringLiteral("canSnooze")).toBool());

    const QVariantList upcoming = core->upcomingOccurrences();
    QCOMPARE(upcoming.size(), 4);
    for (const QVariant &entry : upcoming) {
        const QVariantMap occ = entry.toMap();
        QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        QCOMPARE(occ.value(QStringLiteral("preview")).toBool(), true);
        QVERIFY(occ.value(QStringLiteral("dueTs")).toLongLong() > core->nowMs());
    }
}

void FacadeCoreTest::addingPlanKeepsTodaysProgress()
{
    AppCore *core = AppCore::instance();

    QVariantMap target;
    for (const QVariant &entry : core->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("state")).toString() != QStringLiteral("confirmed")) {
            target = occ;
            break;
        }
    }
    if (target.isEmpty())
        QSKIP("今天已经全部确认，没有可改动的项");

    const qint64 id = target.value(QStringLiteral("occurrenceId")).toLongLong();
    const qint64 planId = target.value(QStringLiteral("planId")).toLongLong();
    QVERIFY(core->confirm(id));
    const int doneAfterConfirm = core->todayDone();

    if (createPlanAhead(12) == 0)
        QSKIP("临近午夜，今天不会产生新 occurrence");

    // 新建计划只重排清单，不能把今天已确认的进度洗掉。
    for (const QVariant &entry : core->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("occurrenceId")).toLongLong() == id) {
            QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("confirmed"));
            QCOMPARE(core->todayDone(), doneAfterConfirm);
            return;
        }
    }
    QFAIL("新建计划后原来的 occurrence 不见了");
}

QTEST_GUILESS_MAIN(FacadeCoreTest)
#include "tst_app_core.moc"

void FacadeCoreTest::restorePlanKeepsProgress()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = createPlanAhead(15);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");

    const qint64 id = occurrenceForPlan(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QVERIFY(core->confirm(id));
    const int doneBefore = core->todayDone();
    const int plansBefore = core->plans().size();

    QVERIFY(core->deletePlan(planId));
    QVERIFY(occurrenceForPlan(planId).isEmpty());
    QCOMPARE(core->todayDone(), doneBefore - 1);

    // 撤销删除：计划回来，今天已确认的进度也回来。
    QString error;
    QVERIFY2(core->restorePlan(planId, &error), qPrintable(error));
    QCOMPARE(core->plans().size(), plansBefore);
    QCOMPARE(occurrenceForPlan(planId).value(QStringLiteral("state")).toString(),
             QStringLiteral("confirmed"));
    QCOMPARE(core->todayDone(), doneBefore);

    QVERIFY(!core->restorePlan(planId, &error));
    QCOMPARE(error, QStringLiteral("plan_not_found"));
}

void FacadeCoreTest::statsTodayFollowsOccurrences()
{
    // 统计的"今天"必须和首页的 todayDone / todayTotal 一致，并随确认/新增变化。
    AppCore *core = AppCore::instance();
    QSignalSpy spy(core, &AppCore::statsChanged);

    auto today = [core] { return core->stats(7).last().toMap(); };
    QCOMPARE(today().value(QStringLiteral("total")).toInt(), core->todayTotal());
    QCOMPARE(today().value(QStringLiteral("done")).toInt(), core->todayDone());

    const qint64 planId = createPlanAhead(15);
    if (planId == 0)
        QSKIP("临近午夜，今天不会产生 occurrence");
    QVERIFY(spy.count() > 0);
    QCOMPARE(today().value(QStringLiteral("total")).toInt(), core->todayTotal());

    spy.clear();
    const qint64 id = occurrenceForPlan(planId).value(QStringLiteral("occurrenceId")).toLongLong();
    QVERIFY(core->confirm(id));
    QVERIFY(spy.count() > 0);
    QCOMPARE(today().value(QStringLiteral("done")).toInt(), core->todayDone());
    // 还没到点的提醒不算漏做
    QCOMPARE(today().value(QStringLiteral("missed")).toInt(), 0);
}

void FacadeCoreTest::realStatsStartFromFirstUse()
{
    AppCore *core = AppCore::instance();
    core->setDemoMode(false);

    // 真实数据域今天才开始用：统计只有今天一格，没有演示用的历史曲线。
    QCOMPARE(core->stats(7).size(), 1);
    QCOMPARE(core->stats(30).size(), 1);
    const QVariantMap day = core->stats(7).first().toMap();
    QVERIFY(day.value(QStringLiteral("isToday")).toBool());
    QCOMPARE(day.value(QStringLiteral("date")).toString(),
             QDate::currentDate().toString(Qt::ISODate));
    QCOMPARE(day.value(QStringLiteral("total")).toInt(), core->todayTotal());

    core->setDemoMode(true);
    QCOMPARE(core->stats(7).size(), 7);
}
