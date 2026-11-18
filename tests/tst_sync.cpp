// SyncVM 同步链路的离线单测：假 HttpClient 回放 canned 响应，不联网。
//
// 锁的是规范 §8/§9 的同步语义：
//   * 事件队列上传 → ack 回写 serverSeq → pendingCount 归零
//   * 服务端拒绝（过时状态）的事件标记过期，不无限重放
//   * GET /state 增量合并 occurrence 状态（本地终态不被回退）
//   * GET /plans 增量合并计划（含 deleted）
//   * 游标持久化，下一轮带 ?since= 续传

#include <QtTest/QtTest>

#include "app_core.h"
#include "app_settings.h"
#include "http_client.h"
#include "sync_vm.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace {

class FakeHttpClient : public HttpClient
{
public:
    bool isOnline() const override { return online; }

    quint64 postJson(const QString &path, const QJsonObject &body) override
    {
        const quint64 id = ++nextId;
        posts.append(qMakePair(path, body));
        if (path == QLatin1String("/events") && eventsHandler) {
            if (failEvents)
                emit failed(id, QStringLiteral("network"));
            else
                emit reply(id, 200, eventsHandler(body));
        }
        return id;
    }

    quint64 getJson(const QString &path, const QUrlQuery &query) override
    {
        const quint64 id = ++nextId;
        if (path == QLatin1String("/state") && stateHandler)
            emit reply(id, 200, stateHandler(query));
        if (path == QLatin1String("/plans") && plansHandler)
            emit reply(id, 200, plansHandler(query));
        return id;
    }

    std::function<QJsonObject(const QJsonObject &)> eventsHandler;
    std::function<QJsonObject(const QUrlQuery &)> stateHandler;
    std::function<QJsonObject(const QUrlQuery &)> plansHandler;
    QVector<QPair<QString, QJsonObject>> posts;
    bool online = true;
    bool failEvents = false;
    quint64 nextId = 0;
};

QVariantList todayOccurrences()
{
    return AppCore::instance()->occurrencesToday();
}

// 演示种子数据也在今天清单里：按 planId 找自己造的那条 occurrence
qint64 occurrenceIdOfPlan(qint64 planId)
{
    for (const QVariant &o : todayOccurrences()) {
        const QVariantMap occ = o.toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() == planId)
            return occ.value(QStringLiteral("occurrenceId")).toLongLong();
    }
    return 0;
}

} // namespace

class TestSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase(); // QSettings 组织名必须在任何读写之前设好
    void init(); // 每个用例独立设置
    void uploadEventsAndAck();
    void rejectedEventsAreExpired();
    void pullStateAppliesOccurrence();
    void localFinalStateNotReverted();
    void pullPlansMergesAndDeletes();
    void cursorPersistedAcrossCycles();

private:
    SyncVM *makeSync(FakeHttpClient **fakeOut);
};

void TestSync::initTestCase()
{
    // 和 tst_platform 同理：QSettings 需要稳定的组织/应用名，
    // 否则 paired/游标这些键读写落不到同一处。
    QCoreApplication::setOrganizationName(QStringLiteral("YibanTest"));
    QCoreApplication::setApplicationName(QStringLiteral("YibanElderSyncTest"));
}

void TestSync::init()
{
    // 同步相关状态归零（QSettings 与 AppCore 都是进程级的，
    // 用例之间要互相擦干净）
    AppSettings *settings = AppSettings::instance();
    settings->setPairing(true, QStringLiteral("elder"));
    settings->setLastSyncTs(0);
    settings->setStateSeq(0);
    settings->setPlansSeq(0);
    AppCore::instance()->resetDemo();
}

namespace {
// SyncVM 的回执走队列投递（http_client.h 合约），测试里冲洗一轮事件循环
inline void flush()
{
    qApp->processEvents();
}
} // namespace

SyncVM *TestSync::makeSync(FakeHttpClient **fakeOut)
{
    auto *fake = new FakeHttpClient();
    // 默认拉取都回空集：没配 handler 的用例里 /state、/plans 也得有回执，
    // 否则请求一直挂在"syncing"
    fake->stateHandler = [](const QUrlQuery &) {
        return QJsonObject {
            { QLatin1String("next_seq"), 0.0 },
            { QLatin1String("occurrences"), QJsonArray() },
        };
    };
    fake->plansHandler = [](const QUrlQuery &) {
        return QJsonObject {
            { QLatin1String("next_seq"), 0.0 },
            { QLatin1String("plans"), QJsonArray() },
        };
    };
    auto *vm = new SyncVM(this);
    vm->setHttpClient(fake);
    *fakeOut = fake;
    return vm;
}

void TestSync::uploadEventsAndAck()
{
    AppCore::instance()->sos(); // 产生一条待上传事件
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    QCOMPARE(vm->pendingCount(), 1);

    qint64 seq = 45;
    client->eventsHandler = [&seq](const QJsonObject &body) {
        QJsonArray accepted;
        const QJsonArray events = body.value(QLatin1String("events")).toArray();
        for (const auto &e : events) {
            accepted.append(QJsonObject {
                { QLatin1String("event_id"), e.toObject().value(QLatin1String("event_id")) },
                { QLatin1String("server_seq"), ++seq },
                { QLatin1String("applied"), true },
            });
        }
        return QJsonObject {
            { QLatin1String("accepted"), accepted },
            { QLatin1String("rejected"), QJsonArray() },
            { QLatin1String("next_seq"), seq },
        };
    };

    vm->retrySync();
    flush(); // 回执是队列投递的，冲洗一轮事件循环让它们落袋
    QCOMPARE(vm->pendingCount(), 0);
    QCOMPARE(vm->syncState(), QStringLiteral("idle"));
    QVERIFY(vm->lastSyncTs() > 0);
    QVERIFY(client->posts.size() >= 1);
    QCOMPARE(client->posts.first().first, QStringLiteral("/events"));
    delete vm;
}

void TestSync::rejectedEventsAreExpired()
{
    AppCore::instance()->sos();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    // 服务端全部拒绝：标记过期，不再重放，但日志里仍可导出
    client->eventsHandler = [](const QJsonObject &) {
        QJsonArray rejected;
        return QJsonObject {
            { QLatin1String("accepted"), QJsonArray() },
            { QLatin1String("rejected"), rejected }, // 真实形状见下方填充
            { QLatin1String("next_seq"), 9 },
        };
    };
    // 需要 eventId 才能填充 rejected —— 用处理器拿请求体再回
    client->eventsHandler = [](const QJsonObject &body) {
        QJsonArray rejected;
        for (const auto &e : body.value(QLatin1String("events")).toArray()) {
            rejected.append(QJsonObject {
                { QLatin1String("event_id"),
                  e.toObject().value(QLatin1String("event_id")) },
                { QLatin1String("reason"), QStringLiteral("stale_state") },
            });
        }
        return QJsonObject {
            { QLatin1String("accepted"), QJsonArray() },
            { QLatin1String("rejected"), rejected },
            { QLatin1String("next_seq"), 9 },
        };
    };

    vm->retrySync();
    flush(); // 回执是队列投递的，冲洗一轮事件循环让它们落袋
    QCOMPARE(vm->pendingCount(), 0);          // 不再出现在待上传里
    QVERIFY(vm->eventLogCsv().contains(QStringLiteral("sos"))); // 日志仍可导出
    delete vm;
}

void TestSync::pullStateAppliesOccurrence()
{
    // 造一条今天的提醒（23:59 保证不与执行时刻竞态）
    QVariantMap plan;
    plan[QStringLiteral("title")] = QStringLiteral("吃降压药");
    QVariantMap rule;
    rule[QStringLiteral("kind")] = QStringLiteral("daily");
    rule[QStringLiteral("times")] = QStringList { QStringLiteral("23:59") };
    plan[QStringLiteral("rule")] = rule;
    const qint64 planId = AppCore::instance()->createPlan(plan);
    QVERIFY(planId > 0);
    // 演示种子数据也有今天的 occurrence：按 planId 精确找自己那条
    const qint64 occurrenceId = occurrenceIdOfPlan(planId);
    QVERIFY(occurrenceId > 0);
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    QString seenSince;
    client->stateHandler = [occurrenceId, &seenSince](const QUrlQuery &query) {
        seenSince = query.queryItemValue(QStringLiteral("since"));
        return QJsonObject {
            { QLatin1String("next_seq"), 47 },
            { QLatin1String("server_time"), 1793981000000.0 },
            { QLatin1String("occurrences"), QJsonArray { QJsonObject {
                { QLatin1String("occurrence_id"), static_cast<double>(occurrenceId) },
                { QLatin1String("plan_id"), 1 },
                { QLatin1String("title"), QStringLiteral("吃降压药") },
                { QLatin1String("due_ts"), 1793980800000.0 },
                { QLatin1String("state"), QStringLiteral("notified") },
                { QLatin1String("snooze_count"), 1 },
                { QLatin1String("snooze_until_ts"), 1793981400000.0 },
                { QLatin1String("window_min"), 30 },
                { QLatin1String("escalate_after_min"), 15 },
                { QLatin1String("updated_seq"), 46 },
            } } },
        };
    };

    vm->retrySync();
    flush(); // 回执是队列投递的，冲洗一轮事件循环让它们落袋
    QCOMPARE(seenSince, QStringLiteral("0"));
    QVariantMap occ;
    for (const QVariant &o : todayOccurrences()) {
        if (o.toMap().value(QStringLiteral("occurrenceId")).toLongLong() == occurrenceId) {
            occ = o.toMap();
            break;
        }
    }
    QCOMPARE(occ.value(QStringLiteral("state")).toString(), QStringLiteral("notified"));
    QCOMPARE(occ.value(QStringLiteral("snoozeCount")).toInt(), 1);
    QCOMPARE(AppSettings::instance()->stateSeq(), qint64(47));
    delete vm;
}

void TestSync::localFinalStateNotReverted()
{
    QVariantMap plan;
    plan[QStringLiteral("title")] = QStringLiteral("喝水");
    QVariantMap rule;
    rule[QStringLiteral("kind")] = QStringLiteral("daily");
    rule[QStringLiteral("times")] = QStringList { QStringLiteral("23:59") };
    plan[QStringLiteral("rule")] = rule;
    const qint64 planId = AppCore::instance()->createPlan(plan);
    QVERIFY(planId > 0);
    const qint64 occurrenceId = occurrenceIdOfPlan(planId);
    QVERIFY(occurrenceId > 0);
    QVERIFY(AppCore::instance()->confirm(occurrenceId));

    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    // 服务端还停在旧快照（pending）：本地已 confirmed 不许被回退
    client->stateHandler = [occurrenceId](const QUrlQuery &) {
        return QJsonObject {
            { QLatin1String("next_seq"), 12 },
            { QLatin1String("occurrences"), QJsonArray { QJsonObject {
                { QLatin1String("occurrence_id"), static_cast<double>(occurrenceId) },
                { QLatin1String("state"), QStringLiteral("pending") },
                { QLatin1String("snooze_count"), 0 },
                { QLatin1String("snooze_until_ts"), 0.0 },
            } } },
        };
    };
    vm->retrySync();
    flush(); // 回执是队列投递的，冲洗一轮事件循环让它们落袋
    QVariantMap after;
    for (const QVariant &o : todayOccurrences()) {
        if (o.toMap().value(QStringLiteral("occurrenceId")).toLongLong() == occurrenceId) {
            after = o.toMap();
            break;
        }
    }
    QCOMPARE(after.value(QStringLiteral("state")).toString(),
             QStringLiteral("confirmed"));
    delete vm;
}

void TestSync::pullPlansMergesAndDeletes()
{
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    QString seenSince;
    client->plansHandler = [&seenSince](const QUrlQuery &query) {
        seenSince = query.queryItemValue(QStringLiteral("since"));
        return QJsonObject {
            { QLatin1String("next_seq"), 20 },
            { QLatin1String("plans"), QJsonArray {
                QJsonObject {
                    { QLatin1String("plan_id"), 3 },
                    { QLatin1String("title"), QStringLiteral("吃降糖药") },
                    { QLatin1String("rule"), QJsonObject {
                        { QLatin1String("kind"), QStringLiteral("weekly") },
                        { QLatin1String("weekdays"), QJsonArray { 1, 3, 5 } },
                        { QLatin1String("times"), QJsonArray { QStringLiteral("12:00") } },
                    } },
                    { QLatin1String("version"), 3 },
                    { QLatin1String("deleted"), false },
                    { QLatin1String("updated_seq"), 18 },
                },
                QJsonObject {
                    { QLatin1String("plan_id"), 4 },
                    { QLatin1String("title"), QStringLiteral("测血压") },
                    { QLatin1String("rule"), QJsonObject {
                        { QLatin1String("kind"), QStringLiteral("daily") },
                        { QLatin1String("times"), QJsonArray { QStringLiteral("07:30") } },
                    } },
                    { QLatin1String("version"), 1 },
                    { QLatin1String("deleted"), true }, // 服务端已删
                    { QLatin1String("updated_seq"), 19 },
                },
            } },
        };
    };

    // 演示种子计划混在列表里
    const int plansBefore = AppCore::instance()->plans().size();
    vm->retrySync();
    flush(); // 回执是队列投递的，冲洗一轮事件循环让它们落袋
    QCOMPARE(seenSince, QStringLiteral("0"));
    QCOMPARE(AppCore::instance()->plans().size(), plansBefore - 1);
    bool hasPlan3 = false;
    for (const QVariant &p : AppCore::instance()->plans()) {
        if (p.toMap().value(QStringLiteral("planId")).toLongLong() == 3)
            hasPlan3 = true;
    }
    QVERIFY(hasPlan3);
    QCOMPARE(AppSettings::instance()->plansSeq(), qint64(20));
    delete vm;
}

void TestSync::cursorPersistedAcrossCycles()
{
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    AppSettings::instance()->setStateSeq(41); // 上一轮已应用到 41

    QString seenSince;
    client->stateHandler = [&seenSince](const QUrlQuery &query) {
        seenSince = query.queryItemValue(QStringLiteral("since"));
        return QJsonObject {
            { QLatin1String("next_seq"), 47 },
            { QLatin1String("occurrences"), QJsonArray() },
        };
    };
    vm->retrySync();
    flush(); // 回执是队列投递的，冲洗一轮事件循环让它们落袋
    // 第二轮请求必须带增量游标，而不是从 0 重放
    QVERIFY(seenSince.toInt() >= 41);
    QCOMPARE(AppSettings::instance()->stateSeq(), qint64(47));
    delete vm;
}

QTEST_MAIN(TestSync)
#include "tst_sync.moc"
