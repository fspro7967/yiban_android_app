// B1-1 统计趋势（I4）的回归用例：
//   * 7/30 天序列长度、空日补 0、最后一项是今天
//   * 睡眠按"起床那天"归日（跨零点的睡眠算在第二天）
//   * 跨天后序列整体后移一天
//   * 新增记录后 recordsChanged 触发、序列更新
#include <QtTest>

#include <QDate>
#include <QDateTime>
#include <QSignalSpy>
#include <QTime>
#include <QVariantMap>

#include "app_core.h"
#include "record_vm.h"

namespace {

const QDate kToday(2026, 12, 1);

qint64 msAt(const QDate &day, int hour, int minute)
{
    return QDateTime(day, QTime(hour, minute)).toMSecsSinceEpoch();
}

} // namespace

class TrendsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void seriesShapeAndFill();
    void sleepCrossesMidnight();
    void crossDayShiftsWindow();
    void newRecordUpdatesSeries();
    // review 第 8、9 条
    void dayRolloverRefreshesTrend();
    void orphanSleepEndIsNotPaired();
};

void TrendsTest::init()
{
    AppCore *core = AppCore::instance();
    core->setClockForTests(msAt(kToday, 9, 0));
    core->resetStorage();
    core->tickForTests();
}

void TrendsTest::seriesShapeAndFill()
{
    AppCore *core = AppCore::instance();
    // 30 天数据：每 3 天记 1 杯水，其余为空日
    int expectedTotal = 0;
    for (int i = 29; i >= 0; --i) {
        const QDate day = kToday.addDays(-i);
        core->setClockForTests(msAt(day, 8, 0));
        core->tickForTests();
        if (i % 3 == 0) {
            core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
            ++expectedTotal;
        }
    }
    core->setClockForTests(msAt(kToday, 9, 0));
    core->tickForTests();

    const QVariantList week = core->recordTrend(QStringLiteral("water"), 7);
    QCOMPARE(week.size(), 7);
    QCOMPARE(week.last().toMap().value(QStringLiteral("date")).toString(),
             kToday.toString(Qt::ISODate));

    const QVariantList month = core->recordTrend(QStringLiteral("water"), 30);
    QCOMPARE(month.size(), 30);
    QCOMPARE(month.first().toMap().value(QStringLiteral("date")).toString(),
             kToday.addDays(-29).toString(Qt::ISODate));

    int sum = 0;
    int zeroDays = 0;
    for (const QVariant &entry : month) {
        const QVariantMap item = entry.toMap();
        const double value = item.value(QStringLiteral("value")).toDouble();
        sum += int(value);
        if (value == 0)
            ++zeroDays;
    }
    QCOMPARE(sum, expectedTotal);
    QVERIFY(zeroDays > 0); // 空日必须是 0，不是缺项

    // RecordVM 的聚合属性：三条序列都在，长度一致
    RecordVM vm;
    const QVariantMap trends7 = vm.trends7();
    QCOMPARE(trends7.value(QStringLiteral("water")).toList().size(), 7);
    QCOMPARE(trends7.value(QStringLiteral("sleep")).toList().size(), 7);
    QCOMPARE(trends7.value(QStringLiteral("activity")).toList().size(), 7);
    QCOMPARE(vm.trends30().value(QStringLiteral("water")).toList().size(), 30);
}

void TrendsTest::sleepCrossesMidnight()
{
    AppCore *core = AppCore::instance();
    // 昨晚 22:40 入睡、今早 06:30 起床 → 470 分钟，记在"今天"
    core->setClockForTests(msAt(kToday.addDays(-1), 22, 40));
    core->tickForTests();
    core->addRecord(QStringLiteral("sleep_start"), 0, QString());

    core->setClockForTests(msAt(kToday, 6, 30));
    core->tickForTests();
    core->addRecord(QStringLiteral("sleep_end"), 0, QString());

    const QVariantList trend = core->recordTrend(QStringLiteral("sleep"), 7);
    QCOMPARE(trend.last().toMap().value(QStringLiteral("value")).toInt(), 470);
    // 前一天（入睡那天）没有"起床"，不该算睡眠
    QCOMPARE(trend.at(trend.size() - 2).toMap().value(QStringLiteral("value")).toInt(), 0);
}

void TrendsTest::crossDayShiftsWindow()
{
    AppCore *core = AppCore::instance();
    core->setClockForTests(msAt(kToday.addDays(-6), 9, 0));
    core->tickForTests();
    core->addRecord(QStringLiteral("activity"), 15, QStringLiteral("分钟"));
    core->setClockForTests(msAt(kToday, 9, 0));
    core->tickForTests();

    const QVariantList before = core->recordTrend(QStringLiteral("activity"), 7);
    QCOMPARE(before.first().toMap().value(QStringLiteral("value")).toInt(), 15);

    // 到第二天：窗口整体后移一天，原来第一天（15 分钟）移出 7 天窗口
    core->setClockForTests(msAt(kToday.addDays(1), 9, 0));
    core->tickForTests();
    const QVariantList after = core->recordTrend(QStringLiteral("activity"), 7);
    QCOMPARE(after.last().toMap().value(QStringLiteral("date")).toString(),
             kToday.addDays(1).toString(Qt::ISODate));
    int sum = 0;
    for (const QVariant &entry : after)
        sum += entry.toMap().value(QStringLiteral("value")).toInt();
    QCOMPARE(sum, 0);
}

void TrendsTest::newRecordUpdatesSeries()
{
    AppCore *core = AppCore::instance();
    QSignalSpy spy(core, &AppCore::recordsChanged);
    const int before = core->recordTrend(QStringLiteral("water"), 7).last()
                           .toMap().value(QStringLiteral("value")).toInt();
    core->addRecord(QStringLiteral("water"), 2, QStringLiteral("杯"));
    QVERIFY(spy.count() > 0);
    const int after = core->recordTrend(QStringLiteral("water"), 7).last()
                          .toMap().value(QStringLiteral("value")).toInt();
    QCOMPARE(after, before + 2);
}

void TrendsTest::dayRolloverRefreshesTrend()
{
    AppCore *core = AppCore::instance();
    QSignalSpy spy(core, &AppCore::recordsChanged);
    core->setClockForTests(msAt(kToday.addDays(1), 9, 0));
    core->tickForTests();

    // 跨天必须补发 recordsChanged，否则统计页的"今天"会停在昨天
    QVERIFY(spy.count() > 0);
    QCOMPARE(core->recordTrend(QStringLiteral("water"), 7).last()
                 .toMap().value(QStringLiteral("date")).toString(),
             kToday.addDays(1).toString(Qt::ISODate));
}

void TrendsTest::orphanSleepEndIsNotPaired()
{
    AppCore *core = AppCore::instance();
    // 第一天 22:00 入睡、第二天 06:00 起床 = 480 分钟
    core->setClockForTests(msAt(kToday.addDays(-2), 22, 0));
    core->tickForTests();
    core->addRecord(QStringLiteral("sleep_start"), 0, QString());
    core->setClockForTests(msAt(kToday.addDays(-1), 6, 0));
    core->tickForTests();
    core->addRecord(QStringLiteral("sleep_end"), 0, QString());

    // 第三天只点了"起床"（忘了点入睡）：不能复用第一天那条 start 算出几十小时
    core->setClockForTests(msAt(kToday, 7, 0));
    core->tickForTests();
    core->addRecord(QStringLiteral("sleep_end"), 0, QString());

    const QVariantList trend = core->recordTrend(QStringLiteral("sleep"), 7);
    QCOMPARE(trend.at(trend.size() - 2).toMap().value(QStringLiteral("value")).toInt(), 480);
    QCOMPARE(trend.last().toMap().value(QStringLiteral("value")).toInt(), 0);
}

QTEST_GUILESS_MAIN(TrendsTest)
#include "tst_trends.moc"
