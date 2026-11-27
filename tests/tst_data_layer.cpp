// B1 数据层新增行为的回归用例（与 tst_app_core 的 24 条契约分开）：
//   * I1 同步队列：pendingEvents / markEventsSynced / expireEvents 的语义与幂等
//   * I2 升级链：1 → 2 → 3 级推进、三级转 escalated、对外字段
//   * 跨天：archiveDay 归档后重建今天，统计里保留昨天
// 用可注入时钟跑固定时间，不依赖真实挂钟。
#include <QtTest>

#include <QDate>
#include <QDateTime>
#include <QSignalSpy>
#include <QTime>
#include <QVariantMap>

#include "app_core.h"

namespace {

const QDate kDay(2026, 11, 20);

qint64 at(int hour, int minute)
{
    return QDateTime(kDay, QTime(hour, minute)).toMSecsSinceEpoch();
}

QVariantMap planAt(const QString &title, int hour, int minute)
{
    QVariantMap rule;
    rule.insert(QStringLiteral("kind"), QStringLiteral("daily"));
    rule.insert(QStringLiteral("times"), QStringList{
        QStringLiteral("%1:%2").arg(hour, 2, 10, QLatin1Char('0'))
                               .arg(minute, 2, 10, QLatin1Char('0'))});

    QVariantMap plan;
    plan.insert(QStringLiteral("title"), title);
    plan.insert(QStringLiteral("rule"), rule);
    plan.insert(QStringLiteral("windowMin"), 30);
    plan.insert(QStringLiteral("escalateAfterMin"), 15);
    return plan;
}

QVariantMap todayFor(qint64 planId)
{
    for (const QVariant &entry : AppCore::instance()->occurrencesToday()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() == planId)
            return occ;
    }
    return {};
}

} // namespace

class DataLayerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void syncQueueLifecycle();
    void syncQueueIsIdempotent();
    void escalationChainAdvances();
    void escalationSurvivesRebuild();
    void crossDayRolloverArchivesYesterday();
};

void DataLayerTest::init()
{
    AppCore *core = AppCore::instance();
    // 先注入时钟再清库：startDate 要落在注入的"今天"，否则统计起点会漂到真实日期。
    core->setClockForTests(at(9, 0));
    core->resetStorage();
    // 第一次 tick 让 onTick 自己把 m_day 对齐到注入日期（归档的是空数据）。
    core->tickForTests();
}

void DataLayerTest::syncQueueLifecycle()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(QStringLiteral("吃药"), 9, 10));
    QVERIFY(planId > 0);
    const QVariantMap occ = todayFor(planId);
    QVERIFY(!occ.isEmpty());

    const int before = core->pendingEvents().size();
    QVERIFY(core->confirm(occ.value(QStringLiteral("occurrenceId")).toLongLong()));
    const QVariantList pending = core->pendingEvents();
    QCOMPARE(pending.size(), before + 1);
    const QVariantMap event = pending.first().toMap();
    QCOMPARE(event.value(QStringLiteral("serverSeq")).toLongLong(), 0LL);
    const QString eventId = event.value(QStringLiteral("eventId")).toString();
    QVERIFY(!eventId.isEmpty());

    // ack 之后不再待上传
    core->markEventsSynced(QVariantList{QVariantMap{
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("serverSeq"), 7},
    }});
    QCOMPARE(core->pendingEvents().size(), before);

    // 服务端拒绝的事件标记为 -1：不重传，但留在日志里
    QVERIFY(core->undoOccurrence(occ.value(QStringLiteral("occurrenceId")).toLongLong()));
    const QVariantList afterUndo = core->pendingEvents();
    QCOMPARE(afterUndo.size(), before + 1);
    const QString undoEventId = afterUndo.first().toMap().value(QStringLiteral("eventId")).toString();
    core->expireEvents(QStringList{undoEventId});
    QCOMPARE(core->pendingEvents().size(), before);

    bool expiredSeen = false;
    for (const QVariant &entry : core->eventLog()) {
        const QVariantMap e = entry.toMap();
        if (e.value(QStringLiteral("eventId")).toString() == undoEventId
            && e.value(QStringLiteral("serverSeq")).toLongLong() == -1) {
            expiredSeen = true;
        }
    }
    QVERIFY(expiredSeen);
}

void DataLayerTest::syncQueueIsIdempotent()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(QStringLiteral("喝水"), 9, 10));
    QVERIFY(planId > 0);
    QVERIFY(core->confirm(todayFor(planId).value(QStringLiteral("occurrenceId")).toLongLong()));
    const QString eventId = core->pendingEvents().first().toMap()
                                .value(QStringLiteral("eventId")).toString();

    QSignalSpy spy(core, &AppCore::eventsChanged);
    for (int i = 0; i < 3; ++i) {
        core->markEventsSynced(QVariantList{QVariantMap{
            {QStringLiteral("eventId"), eventId},
            {QStringLiteral("serverSeq"), 11},
        }});
    }
    // 第一次 ack 改状态，后两次是幂等空操作
    QCOMPARE(spy.count(), 1);
    QCOMPARE(core->pendingEvents().size(), 0);

    // 已 ack 的不接受更小的/重复的序号覆盖
    qint64 seq = 0;
    for (const QVariant &entry : core->eventLog()) {
        const QVariantMap e = entry.toMap();
        if (e.value(QStringLiteral("eventId")).toString() == eventId)
            seq = e.value(QStringLiteral("serverSeq")).toLongLong();
    }
    QCOMPARE(seq, 11LL);
}

void DataLayerTest::escalationChainAdvances()
{
    AppCore *core = AppCore::instance();
    // 09:10 的提醒；先把时钟拨到 09:10 让它到点
    const qint64 planId = core->createPlan(planAt(QStringLiteral("测血压"), 9, 10));
    QVERIFY(planId > 0);
    const qint64 occId = todayFor(planId).value(QStringLiteral("occurrenceId")).toLongLong();

    QSignalSpy escalation(core, &AppCore::escalationChanged);
    core->setClockForTests(at(9, 10));
    core->tickForTests();
    QCOMPARE(todayFor(planId).value(QStringLiteral("state")).toString(), QStringLiteral("notified"));
    QCOMPARE(todayFor(planId).value(QStringLiteral("escalationLevel")).toInt(), 1);
    QVERIFY(todayFor(planId).value(QStringLiteral("nextEscalationInSec")).toInt() > 0);

    core->setClockForTests(at(9, 12));
    core->tickForTests();
    QCOMPARE(todayFor(planId).value(QStringLiteral("escalationLevel")).toInt(), 2);

    core->setClockForTests(at(9, 25));
    core->tickForTests();
    const QVariantMap finalOcc = todayFor(planId);
    QCOMPARE(finalOcc.value(QStringLiteral("state")).toString(), QStringLiteral("escalated"));
    QCOMPARE(finalOcc.value(QStringLiteral("escalationLevel")).toInt(), 3);

    bool escalationEvent = false;
    for (const QVariant &entry : core->eventLog()) {
        const QVariantMap e = entry.toMap();
        if (e.value(QStringLiteral("occurrenceId")).toLongLong() == occId
            && e.value(QStringLiteral("action")).toString() == QStringLiteral("escalate")) {
            escalationEvent = true;
        }
    }
    QVERIFY(escalationEvent);
    QVERIFY(escalation.count() >= 3);
}

// 重建今天清单（启动读库、改计划、同步拉取都会走 rebuildTodayFrom）不能把升级链的基线丢掉：
// 否则 App 被杀再打开，已经等了 1 分钟的提醒又从 2 分钟重新倒计时（真机 11/26 复现）。
void DataLayerTest::escalationSurvivesRebuild()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(QStringLiteral("测血压"), 9, 10));
    QVERIFY(planId > 0);

    core->setClockForTests(at(9, 10));
    core->tickForTests();
    QCOMPARE(todayFor(planId).value(QStringLiteral("state")).toString(), QStringLiteral("notified"));
    const qint64 notifiedAt = todayFor(planId).value(QStringLiteral("notifiedAtTs")).toLongLong();
    QVERIFY(notifiedAt > 0);

    core->setClockForTests(at(9, 11));
    core->tickForTests();
    QVERIFY(core->createPlan(planAt(QStringLiteral("另一条"), 20, 0)) > 0); // 触发 rebuildToday
    core->tickForTests();

    const QVariantMap occ = todayFor(planId);
    QCOMPARE(occ.value(QStringLiteral("notifiedAtTs")).toLongLong(), notifiedAt);
    QCOMPARE(occ.value(QStringLiteral("escalationLevel")).toInt(), 1);
    QCOMPARE(occ.value(QStringLiteral("nextEscalationInSec")).toInt(), 60);

    core->setClockForTests(at(9, 12));
    core->tickForTests();
    QCOMPARE(todayFor(planId).value(QStringLiteral("escalationLevel")).toInt(), 2);
}

void DataLayerTest::crossDayRolloverArchivesYesterday()
{
    AppCore *core = AppCore::instance();
    const qint64 planId = core->createPlan(planAt(QStringLiteral("晨起喝水"), 9, 10));
    QVERIFY(planId > 0);
    QVERIFY(core->confirm(todayFor(planId).value(QStringLiteral("occurrenceId")).toLongLong()));
    QCOMPARE(core->todayDone(), 1);

    // 第二天早上 08:00：昨天的进度归档，今天重新生成待办
    const qint64 nextDay = QDateTime(kDay.addDays(1), QTime(8, 0)).toMSecsSinceEpoch();
    core->setClockForTests(nextDay);
    core->tickForTests();

    QCOMPARE(core->todayDone(), 0);
    QCOMPARE(core->todayTotal(), 1);
    QCOMPARE(todayFor(planId).value(QStringLiteral("state")).toString(), QStringLiteral("pending"));

    const QVariantList stats = core->stats(7);
    QVERIFY(stats.size() >= 2);
    bool yesterdaySeen = false;
    for (const QVariant &entry : stats) {
        const QVariantMap day = entry.toMap();
        if (day.value(QStringLiteral("date")).toString() == kDay.toString(Qt::ISODate)) {
            QCOMPARE(day.value(QStringLiteral("done")).toInt(), 1);
            yesterdaySeen = true;
        }
    }
    QVERIFY(yesterdaySeen);
}

QTEST_GUILESS_MAIN(DataLayerTest)
#include "tst_data_layer.moc"
