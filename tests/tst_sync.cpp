// SyncVM 同步链路的离线单测：假 HttpClient 回放 canned 响应，不联网。
//
// 锁的是规范 §8/§9 的同步语义：
//   * 事件队列上传 → ack 回写 serverSeq → pendingCount 归零
//   * 服务端拒绝（过时状态）的事件标记过期，不无限重放
//   * GET /state 增量合并 occurrence 状态（本地终态不被回退）
//   * GET /plans 增量合并计划（含 deleted）
//   * 游标持久化，下一轮带 ?since= 续传
//   * 记录走 POST/PATCH /records：一次一条，修正补 PATCH，演示域不上传
//   * 本机新建计划走 POST /plans 换成服务端号，改/删补 PATCH/DELETE

#include <QtTest/QtTest>

#include "app_core.h"
#include "app_settings.h"
#include "http_client.h"
#include "sync_vm.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
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
            else if (eventsStatus != 200)
                emit reply(id, eventsStatus, eventsErrorBody);
            else
                emit reply(id, 200, eventsHandler(body));
        }
        if (path == QLatin1String("/alarm") && alarmHandler)
            emit reply(id, 200, alarmHandler(body));
        if (path == QLatin1String("/records"))
            answerRecord(id, path, body);
        if (path == QLatin1String("/plans"))
            answerPlan(id, path, QByteArray("POST"));
        return id;
    }

    quint64 patchJson(const QString &path, const QJsonObject &body) override
    {
        const quint64 id = ++nextId;
        patches.append(qMakePair(path, body));
        if (path.startsWith(QLatin1String("/plans/")))
            answerPlan(id, path, QByteArray("PATCH"));
        else
            answerRecord(id, path, body);
        return id;
    }

    quint64 deleteJson(const QString &path) override
    {
        const quint64 id = ++nextId;
        deletes.append(path);
        answerPlan(id, path, QByteArray("DELETE"));
        return id;
    }

    // /plans：POST 按 serverPlanId 发号，版本从 1 起；PATCH/DELETE 版本 +1
    void answerPlan(quint64 id, const QString &path, const QByteArray &verb)
    {
        if (plansStatus != 200) {
            emit reply(id, plansStatus, QJsonObject {});
            return;
        }
        const qint64 pid = verb == "POST" ? ++serverPlanId
                                          : path.section(QLatin1Char('/'), -1).toLongLong();
        const qint64 version = ++planVersions[pid];
        emit reply(id, 200, QJsonObject {
            { QLatin1String("plan_id"), pid },
            { QLatin1String("version"), version },
            { QLatin1String("deleted"), verb == "DELETE" },
        });
    }

    // /records 与 /records/{id}：默认按 nextRecordId 发号回 200
    void answerRecord(quint64 id, const QString &path, const QJsonObject &body)
    {
        if (failRecords) {
            emit failed(id, QStringLiteral("network"));
            return;
        }
        const bool isPatch = path != QLatin1String("/records");
        const int status = isPatch ? patchStatus : recordsStatus;
        if (status != 200) {
            emit reply(id, status, QJsonObject {});
            return;
        }
        Q_UNUSED(body)
        const qint64 rid = path == QLatin1String("/records")
            ? ++serverRecordId
            : path.section(QLatin1Char('/'), -1).toLongLong();
        emit reply(id, 200, QJsonObject {
            { QLatin1String("record_id"), rid },
            { QLatin1String("server_seq"), rid + 100 },
        });
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
    int eventsStatus = 200;
    QJsonObject eventsErrorBody;
    std::function<QJsonObject(const QJsonObject &)> alarmHandler;
    std::function<QJsonObject(const QUrlQuery &)> stateHandler;
    std::function<QJsonObject(const QUrlQuery &)> plansHandler;
    QVector<QPair<QString, QJsonObject>> posts;
    QVector<QPair<QString, QJsonObject>> patches;
    QStringList deletes;
    int plansStatus = 200;   // /plans 写接口
    qint64 serverPlanId = 30;
    QHash<qint64, qint64> planVersions;
    int recordsStatus = 200; // POST /records
    int patchStatus = 200;   // PATCH /records/{id}
    bool failRecords = false;
    qint64 serverRecordId = 500;
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

// 造一条今天的计划并确认，产生一条待上传的 confirm 事件（带真实 occurrenceId）。
// 注意：Q_ASSERT 在 Release 下连同表达式一起被编译掉，confirm 必须无条件调用。
qint64 makeConfirmedOccurrence()
{
    QVariantMap plan;
    plan[QStringLiteral("title")] = QStringLiteral("同步测试提醒");
    QVariantMap rule;
    rule[QStringLiteral("kind")] = QStringLiteral("daily");
    rule[QStringLiteral("times")] = QStringList { QStringLiteral("23:59") };
    plan[QStringLiteral("rule")] = rule;
    const qint64 planId = AppCore::instance()->createPlan(plan);
    const qint64 occurrenceId = occurrenceIdOfPlan(planId);
    AppCore::instance()->confirm(occurrenceId);
    return occurrenceId;
}

int countPosts(const FakeHttpClient *client, const QString &path)
{
    int n = 0;
    for (const auto &p : client->posts)
        n += p.first == path ? 1 : 0;
    return n;
}

QVariantMap recordById(qint64 recordId)
{
    for (const QVariant &r : AppCore::instance()->records()) {
        if (r.toMap().value(QStringLiteral("recordId")).toLongLong() == recordId)
            return r.toMap();
    }
    return {};
}

} // namespace

class TestSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase(); // QSettings 组织名必须在任何读写之前设好
    void init(); // 每个用例独立设置
    void cleanup(); // 用例中途失败会跳过 delete vm：这里兜底，别让它在后面的用例里继续上传
    void uploadEventsAndAck();
    void ackIsPersisted();
    void sosGoesThroughAlarm();
    void rejectedEventsAreExpired();
    void topLevelConflictExpiresEvents();
    void pullStateAppliesOccurrence();
    void localFinalStateNotReverted();
    void pullPlansMergesAndDeletes();
    void cursorPersistedAcrossCycles();
    void syncRequiresToken();
    void serverUrlNormalized();
    void recordsUploadedOneByOne();
    void correctedRecordIsPatched();
    void correctionDuringUploadIsNotLost();
    void demoRecordsNotUploaded();
    void rejectedRecordNotRetried();
    void patch404ReuploadsRecord();
    void missingRecordsRouteKeepsQueue();
    void failedRecordUploadStaysPending();
    void pendingCountRefreshesWhileOffline();
    void reconnectFlushesQueue();
    void reconnectSkipsBackoff();
    void localPlanUploadedAndRenumbered();
    void planEditAndDeleteUploaded();
    void planDeletedBeforeUploadNotSent();
    void pullKeepsUnsyncedLocalEdit();
    void pulledCopyBeforeAckIsMerged();
    void missingPlansRouteKeepsQueue();
    void hardDeletedPlanRemovedOnManualSync();
    void emptyFullListDeletesNothing();

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
    settings->setToken(QStringLiteral("test-token-123456"));
    settings->setLastSyncTs(0);
    settings->setStateSeq(0);
    settings->setPlansSeq(0);
    AppCore::instance()->resetDemo();
}

void TestSync::cleanup()
{
    qDeleteAll(findChildren<SyncVM *>());
}

namespace {
// SyncVM 的回执走队列投递（http_client.h 合约），测试里冲洗一轮事件循环
inline void flush()
{
    qApp->processEvents();
}

// 记录是一条接一条传的：回执 → 下一条请求 → 回执……多冲洗几轮
inline void flushAll()
{
    for (int i = 0; i < 20; ++i)
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
    makeConfirmedOccurrence(); // 带实例的 confirm 事件走 /events 批量上传
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

void TestSync::ackIsPersisted()
{
    // 真实数据域才落盘（演示域是内存快照）
    AppCore::instance()->resetStorage();
    makeConfirmedOccurrence();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    QCOMPARE(vm->pendingCount(), 2); // 真实域：新建的计划（待 POST）+ confirm 事件

    client->eventsHandler = [](const QJsonObject &body) {
        QJsonArray accepted;
        for (const auto &e : body.value(QLatin1String("events")).toArray()) {
            accepted.append(QJsonObject {
                { QLatin1String("event_id"), e.toObject().value(QLatin1String("event_id")) },
                { QLatin1String("server_seq"), 77 },
                { QLatin1String("applied"), true },
            });
        }
        return QJsonObject {
            { QLatin1String("accepted"), accepted },
            { QLatin1String("rejected"), QJsonArray() },
            { QLatin1String("next_seq"), 77 },
        };
    };
    vm->retrySync();
    flushAll(); // 计划回执换号 → 事件上传 → 回执 → schedulePersist
    QCOMPARE(vm->pendingCount(), 0);

    // 直接读库：ack 只改了事件的 serverSeq，也必须写进去，否则重启后会重传
    const QString path = qEnvironmentVariable("YIBAN_APP_DB");
    QVERIFY2(!path.isEmpty(), "YIBAN_APP_DB 未设置（见 tests/CMakeLists.txt）");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("tst_sync_probe"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("SELECT synced, json FROM events")));
        int rows = 0;
        while (query.next()) {
            ++rows;
            QCOMPARE(query.value(0).toInt(), 1);
            const QJsonObject event =
                QJsonDocument::fromJson(query.value(1).toString().toUtf8()).object();
            QCOMPARE(event.value(QLatin1String("serverSeq")).toInteger(), 77);
        }
        QCOMPARE(rows, 1);
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tst_sync_probe"));
    delete vm;
}

void TestSync::sosGoesThroughAlarm()
{
    // SOS 呼救不带 occurrenceId：契约上走 POST /v1/alarm，不能进 /events
    // （服务端会以 occurrence_not_found 拒绝）
    AppCore::instance()->sos();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    QCOMPARE(vm->pendingCount(), 1);

    QJsonObject alarmBody; // lambda 里不能用 QCOMPARE（宏含 return），先存后断言
    client->alarmHandler = [&alarmBody](const QJsonObject &body) {
        alarmBody = body;
        return QJsonObject {
            { QLatin1String("sent"), true },
            { QLatin1String("channel"), QStringLiteral("webhook") },
            { QLatin1String("server_seq"), 51.0 },
        };
    };

    vm->retrySync();
    flush();
    // 校验告警请求体形状（CareMate _alarm 的口径）
    QCOMPARE(alarmBody.value(QLatin1String("level")).toInt(), 3);
    QCOMPARE(alarmBody.value(QLatin1String("source")).toString(), QStringLiteral("manual"));
    QCOMPARE(alarmBody.value(QLatin1String("occurrence_id")).toInt(), 0);
    QCOMPARE(vm->pendingCount(), 0); // ack 后从待上传队列摘掉
    QCOMPARE(vm->syncState(), QStringLiteral("idle"));
    QVERIFY(client->posts.size() >= 1);
    QCOMPARE(client->posts.first().first, QStringLiteral("/alarm"));
    delete vm;
}

void TestSync::rejectedEventsAreExpired()
{
    makeConfirmedOccurrence();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    // 服务端全部拒绝：标记过期，不再重放，但日志里仍可导出
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
    QVERIFY(vm->eventLogCsv().contains(QStringLiteral("confirm"))); // 日志仍可导出
    delete vm;
}

void TestSync::topLevelConflictExpiresEvents()
{
    // 云端把状态冲突放在顶层 409（见 11/22 日志），不是 200 的 rejected 数组。
    // 端上必须按 occurrence_id 过期本批事件，不能退避重发造成 /events 409 风暴。
    const qint64 occurrenceId = makeConfirmedOccurrence();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    QCOMPARE(vm->pendingCount(), 1);
    client->eventsHandler = [](const QJsonObject &) { return QJsonObject(); };
    client->eventsStatus = 409;
    client->eventsErrorBody = QJsonObject {
        { QLatin1String("error"), QJsonObject {
            { QLatin1String("code"), QStringLiteral("state_conflict") },
            { QLatin1String("message"), QStringLiteral("撤销窗口已过（5 秒）") },
            { QLatin1String("occurrence_id"), static_cast<double>(occurrenceId) },
            { QLatin1String("server_state"), QStringLiteral("confirmed") },
            { QLatin1String("server_seq"), 166.0 },
        } },
    };

    vm->retrySync();
    flush();
    QCOMPARE(vm->pendingCount(), 0); // 冲突事件已从待上传队列摘掉
    QVERIFY(vm->eventLogCsv().contains(QStringLiteral("confirm")));
    QCOMPARE(client->posts.size(), 1); // 不再重发
    QCOMPARE(vm->syncState(), QStringLiteral("idle"));
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

void TestSync::syncRequiresToken()
{
    // 离线演示配对过但没拿令牌时，不能再发请求去撞 401。
    AppSettings::instance()->clearToken();
    makeConfirmedOccurrence();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    QCOMPARE(vm->syncState(), QStringLiteral("offline"));
    vm->retrySync();
    flush();
    QVERIFY(client->posts.isEmpty());
    QCOMPARE(vm->pendingCount(), 1); // 事件留在本地等令牌
    delete vm;
}

void TestSync::serverUrlNormalized()
{
    // 管理页填根地址也能用：统一补 /v1，否则 /events 会打到根路径 404。
    QCOMPARE(normalizeServerUrl(QStringLiteral("http://101.245.109.50:8000")),
             QStringLiteral("http://101.245.109.50:8000/v1"));
    QCOMPARE(normalizeServerUrl(QStringLiteral("http://101.245.109.50:8000/v1/")),
             QStringLiteral("http://101.245.109.50:8000/v1"));
    QCOMPARE(normalizeServerUrl(QStringLiteral("101.245.109.50:8000")), QString());
}

void TestSync::recordsUploadedOneByOne()
{
    AppCore::instance()->resetStorage(); // 真实数据域（演示域不上传）
    AppCore *core = AppCore::instance();
    const qint64 water = core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    const qint64 walk = core->addRecord(QStringLiteral("activity"), 20, QStringLiteral("分钟"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    QCOMPARE(vm->pendingCount(), 2);

    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 0);
    QCOMPARE(vm->syncState(), QStringLiteral("idle"));
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 2);

    // 请求体：type/value/unit/at + client_id（去重键）+ device_id。
    // 时间必须叫 at：云端只认 at，发 ts 会被落成上传时刻
    QJsonObject first;
    for (const auto &p : client->posts) {
        if (p.first == QLatin1String("/records")) {
            first = p.second;
            break;
        }
    }
    QCOMPARE(first.value(QLatin1String("type")).toString(), QStringLiteral("water"));
    QCOMPARE(first.value(QLatin1String("value")).toDouble(), 1.0);
    QCOMPARE(first.value(QLatin1String("unit")).toString(), QStringLiteral("杯"));
    QVERIFY(!first.contains(QLatin1String("ts")));
    QCOMPARE(first.value(QLatin1String("at")).toInteger(),
             recordById(water).value(QStringLiteral("at")).toLongLong());
    QVERIFY(!first.value(QLatin1String("client_id")).toString().isEmpty());
    QVERIFY(first.value(QLatin1String("device_id")).isString());

    QCOMPARE(recordById(water).value(QStringLiteral("serverRecordId")).toLongLong(), 501);
    QCOMPARE(recordById(walk).value(QStringLiteral("serverRecordId")).toLongLong(), 502);

    // 再来一轮不重复上传
    vm->retrySync();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 2);
    delete vm;
}

void TestSync::correctedRecordIsPatched()
{
    AppCore::instance()->resetStorage();
    AppCore *core = AppCore::instance();
    const qint64 walk = core->addRecord(QStringLiteral("activity"), 20, QStringLiteral("分钟"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 0);

    QVERIFY(core->correctRecord(walk, QVariantMap { { QStringLiteral("value"), 30 } }));
    QCOMPARE(vm->pendingCount(), 1);
    flushAll(); // recordsChanged 触发上传，不用手动重试
    QCOMPARE(vm->pendingCount(), 0);
    QCOMPARE(client->patches.size(), 1);
    QCOMPARE(client->patches.first().first, QStringLiteral("/records/501"));
    QCOMPARE(client->patches.first().second.value(QLatin1String("value")).toDouble(), 30.0);
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1); // 修正不新建
    delete vm;
}

void TestSync::correctionDuringUploadIsNotLost()
{
    // POST 还没回执时用户改了数值：POST 带的是旧值，回执后必须补一次 PATCH
    AppCore::instance()->resetStorage();
    AppCore *core = AppCore::instance();
    const qint64 walk = core->addRecord(QStringLiteral("activity"), 20, QStringLiteral("分钟"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    vm->retrySync(); // POST 已发出，回执还在事件队列里
    QVERIFY(core->correctRecord(walk, QVariantMap { { QStringLiteral("value"), 45 } }));
    flushAll();

    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1);
    QCOMPARE(client->posts.last().second.value(QLatin1String("value")).toDouble(), 20.0);
    QCOMPARE(client->patches.size(), 1);
    QCOMPARE(client->patches.first().second.value(QLatin1String("value")).toDouble(), 45.0);
    QCOMPARE(vm->pendingCount(), 0);
    delete vm;
}

void TestSync::demoRecordsNotUploaded()
{
    // init() 用的是演示域：演示记录不能传到真实家庭的服务端
    QVERIFY(AppCore::instance()->demoMode());
    AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    QCOMPARE(AppCore::instance()->pendingRecordCount(), 0);
    vm->retrySync();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 0);
    delete vm;
}

void TestSync::rejectedRecordNotRetried()
{
    AppCore::instance()->resetStorage();
    const qint64 id =
        AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->recordsStatus = 400;
    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 0);
    QCOMPARE(recordById(id).value(QStringLiteral("serverRecordId")).toLongLong(), -1);
    vm->retrySync();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1); // 不再重传
    delete vm;
}

void TestSync::patch404ReuploadsRecord()
{
    // 服务端库被清 / 换了服务器：PATCH 404 → 重新 POST 当前值
    AppCore::instance()->resetStorage();
    AppCore *core = AppCore::instance();
    const qint64 walk = core->addRecord(QStringLiteral("activity"), 20, QStringLiteral("分钟"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    vm->retrySync();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1);

    client->patchStatus = 404; // 只有 PATCH 404，重新 POST 照常成功
    QVERIFY(core->correctRecord(walk, QVariantMap { { QStringLiteral("value"), 30 } }));
    flushAll();
    QCOMPARE(client->patches.size(), 1);
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 2);
    QCOMPARE(client->posts.last().second.value(QLatin1String("value")).toDouble(), 30.0);
    QCOMPARE(vm->pendingCount(), 0);
    delete vm;
}

void TestSync::missingRecordsRouteKeepsQueue()
{
    // 服务端还没上 /records（POST 回 404）：记录不能被标成拒收丢掉
    AppCore::instance()->resetStorage();
    const qint64 id =
        AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->recordsStatus = 404;
    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 1);
    QCOMPARE(recordById(id).value(QStringLiteral("serverRecordId")).toLongLong(), 0);
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1); // 退避中，不连发
    delete vm;
}

void TestSync::failedRecordUploadStaysPending()
{
    AppCore::instance()->resetStorage();
    AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->failRecords = true;
    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 1); // 网络失败：留在队列里等退避重传
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1); // 退避中，不连发
    delete vm;
}

void TestSync::pendingCountRefreshesWhileOffline()
{
    // 离线时 SOS / 记一杯水都要让管理页的"待上传"数字跟着变
    AppCore::instance()->resetStorage();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->online = false;
    QSignalSpy spy(vm, &SyncVM::syncChanged);

    AppCore::instance()->sos();
    QVERIFY(spy.count() >= 1);
    QCOMPARE(vm->pendingCount(), 1);

    spy.clear();
    AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    QVERIFY(spy.count() >= 1);
    QCOMPARE(vm->pendingCount(), 2);
    flushAll();
    QVERIFY(client->posts.isEmpty());
    delete vm;
}

void TestSync::reconnectFlushesQueue()
{
    // 断网期间攒下的事件和记录：网络恢复后不用手动点同步，自动传完
    AppCore::instance()->resetStorage();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->online = false;
    client->eventsHandler = [](const QJsonObject &body) {
        QJsonArray accepted;
        for (const auto &e : body.value(QLatin1String("events")).toArray()) {
            accepted.append(QJsonObject {
                { QLatin1String("event_id"), e.toObject().value(QLatin1String("event_id")) },
                { QLatin1String("server_seq"), 90 },
                { QLatin1String("applied"), true },
            });
        }
        return QJsonObject { { QLatin1String("accepted"), accepted },
                             { QLatin1String("rejected"), QJsonArray() } };
    };

    makeConfirmedOccurrence();
    AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    flushAll();
    QVERIFY(client->posts.isEmpty());
    QCOMPARE(vm->syncState(), QStringLiteral("offline"));
    QCOMPARE(vm->pendingCount(), 3); // 新建的计划 + confirm 事件 + 记录

    client->online = true;
    emit client->onlineChanged();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/plans")), 1);
    QCOMPARE(countPosts(client, QStringLiteral("/events")), 1);
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1);
    QCOMPARE(vm->pendingCount(), 0);
    delete vm;
}

void TestSync::reconnectSkipsBackoff()
{
    // 断网时上传失败进了退避（可能已经退到几分钟）：恢复联网立刻重传，不等退避到点
    AppCore::instance()->resetStorage();
    AppCore::instance()->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->failRecords = true;
    vm->retrySync();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 1);
    QCOMPARE(vm->pendingCount(), 1);

    client->failRecords = false;
    emit client->onlineChanged();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/records")), 2);
    QCOMPARE(vm->pendingCount(), 0);
    delete vm;
}

namespace {
QVariantMap planFromServerForTest(qint64 id, const QString &title)
{
    return QVariantMap {
        { QStringLiteral("planId"), id },
        { QStringLiteral("title"), title },
        { QStringLiteral("rule"), QVariantMap {
              { QStringLiteral("kind"), QStringLiteral("daily") },
              { QStringLiteral("times"), QStringList { QStringLiteral("23:59") } } } },
        { QStringLiteral("version"), 1 },
        { QStringLiteral("deleted"), false },
    };
}

QVariantMap planById(qint64 planId)
{
    for (const QVariant &p : AppCore::instance()->plans()) {
        if (p.toMap().value(QStringLiteral("planId")).toLongLong() == planId)
            return p.toMap();
    }
    return {};
}

qint64 createTestPlan(const QString &title)
{
    QVariantMap plan;
    plan[QStringLiteral("title")] = title;
    plan[QStringLiteral("rule")] = QVariantMap {
        { QStringLiteral("kind"), QStringLiteral("daily") },
        { QStringLiteral("times"), QStringList { QStringLiteral("23:59") } },
    };
    return AppCore::instance()->createPlan(plan);
}

std::function<QJsonObject(const QJsonObject &)> acceptAllEvents()
{
    return [](const QJsonObject &body) {
        QJsonArray accepted;
        for (const auto &e : body.value(QLatin1String("events")).toArray()) {
            accepted.append(QJsonObject {
                { QLatin1String("event_id"), e.toObject().value(QLatin1String("event_id")) },
                { QLatin1String("server_seq"), 60 },
                { QLatin1String("applied"), true },
            });
        }
        return QJsonObject { { QLatin1String("accepted"), accepted },
                             { QLatin1String("rejected"), QJsonArray() } };
    };
}
} // namespace

void TestSync::localPlanUploadedAndRenumbered()
{
    // 本机新建的计划：先用本地号段，POST 回执后换成服务端号；
    // 今天的 occurrence 和挂在上面的事件一起换号，事件要等换号后才上传
    AppCore::instance()->resetStorage();
    const qint64 localId = createTestPlan(QStringLiteral("降压药"));
    QVERIFY(localId >= 5000000);
    const qint64 localOcc = occurrenceIdOfPlan(localId);
    QVERIFY(AppCore::instance()->confirm(localOcc));
    QVERIFY(AppCore::instance()->uploadableEvents().isEmpty()); // 计划没上传，事件先不传

    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->eventsHandler = acceptAllEvents();
    QCOMPARE(vm->pendingCount(), 2); // 1 个计划 + 1 个事件

    vm->retrySync();
    flushAll();

    QCOMPARE(countPosts(client, QStringLiteral("/plans")), 1);
    QJsonObject planBody;
    for (const auto &p : client->posts) {
        if (p.first == QLatin1String("/plans"))
            planBody = p.second;
    }
    QCOMPARE(planBody.value(QLatin1String("title")).toString(), QStringLiteral("降压药"));
    QCOMPARE(planBody.value(QLatin1String("rule")).toObject()
                 .value(QLatin1String("kind")).toString(), QStringLiteral("daily"));
    QVERIFY(!planBody.contains(QLatin1String("plan_id")));

    QVERIFY(planById(localId).isEmpty());
    QCOMPARE(planById(31).value(QStringLiteral("title")).toString(), QStringLiteral("降压药"));
    const qint64 serverOcc = occurrenceIdOfPlan(31);
    QCOMPARE(serverOcc, 31 * 1000000000LL + localOcc % 1000000000LL);
    QCOMPARE(todayOccurrences().isEmpty(), false);

    // 事件带的是换号后的 occurrence_id
    QCOMPARE(countPosts(client, QStringLiteral("/events")), 1);
    QJsonObject eventsBody;
    for (const auto &p : client->posts) {
        if (p.first == QLatin1String("/events"))
            eventsBody = p.second;
    }
    QCOMPARE(eventsBody.value(QLatin1String("events")).toArray().first().toObject()
                 .value(QLatin1String("occurrence_id")).toInteger(), serverOcc);
    QCOMPARE(vm->pendingCount(), 0);

    // 已确认的进度换号后不丢
    for (const QVariant &o : todayOccurrences()) {
        if (o.toMap().value(QStringLiteral("occurrenceId")).toLongLong() == serverOcc)
            QCOMPARE(o.toMap().value(QStringLiteral("state")).toString(), QStringLiteral("confirmed"));
    }
    delete vm;
}

void TestSync::planEditAndDeleteUploaded()
{
    AppCore::instance()->resetStorage();
    createTestPlan(QStringLiteral("喝水"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 0);

    QVERIFY(AppCore::instance()->updatePlan(31, QVariantMap {
        { QStringLiteral("title"), QStringLiteral("喝温水") } }));
    QCOMPARE(vm->pendingCount(), 1);
    flushAll();
    QCOMPARE(client->patches.size(), 1);
    QCOMPARE(client->patches.first().first, QStringLiteral("/plans/31"));
    QCOMPARE(client->patches.first().second.value(QLatin1String("title")).toString(),
             QStringLiteral("喝温水"));

    QVERIFY(AppCore::instance()->deletePlan(31));
    flushAll();
    QCOMPARE(client->deletes, QStringList { QStringLiteral("/plans/31") });

    QVERIFY(AppCore::instance()->restorePlan(31)); // 撤销删除 = PATCH（云端 PATCH 会重新启用）
    flushAll();
    QCOMPARE(client->patches.size(), 2);
    QCOMPARE(vm->pendingCount(), 0);
    QCOMPARE(countPosts(client, QStringLiteral("/plans")), 1); // 一直是同一条，不重复新建
    delete vm;
}

void TestSync::planDeletedBeforeUploadNotSent()
{
    AppCore::instance()->resetStorage();
    const qint64 localId = createTestPlan(QStringLiteral("建错了"));
    QVERIFY(AppCore::instance()->deletePlan(localId));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    QCOMPARE(vm->pendingCount(), 0);
    vm->retrySync();
    flushAll();
    QCOMPARE(countPosts(client, QStringLiteral("/plans")), 0);
    QVERIFY(client->deletes.isEmpty());
    delete vm;
}

void TestSync::pullKeepsUnsyncedLocalEdit()
{
    AppCore::instance()->resetStorage();
    createTestPlan(QStringLiteral("钙片"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    vm->retrySync();
    flushAll();

    auto serverPlan = [](qint64 version, const QString &title) {
        return QVariantMap {
            { QStringLiteral("planId"), 31 },
            { QStringLiteral("title"), title },
            { QStringLiteral("rule"), QVariantMap {
                  { QStringLiteral("kind"), QStringLiteral("daily") },
                  { QStringLiteral("times"), QStringList { QStringLiteral("08:00") } } } },
            { QStringLiteral("version"), version },
            { QStringLiteral("deleted"), false },
        };
    };

    // 同版本回放（云端会把规则/窗口改成它的口径）：不覆盖本机设定
    AppCore::instance()->applyServerPlans({ serverPlan(1, QStringLiteral("云端口径")) });
    QCOMPARE(planById(31).value(QStringLiteral("title")).toString(), QStringLiteral("钙片"));

    // 本地改了还没传：服务端更新的版本也先不盖
    client->online = false;
    QVERIFY(AppCore::instance()->updatePlan(31, QVariantMap {
        { QStringLiteral("title"), QStringLiteral("钙片 2 片") } }));
    AppCore::instance()->applyServerPlans({ serverPlan(2, QStringLiteral("旧标题")) });
    QCOMPARE(planById(31).value(QStringLiteral("title")).toString(), QStringLiteral("钙片 2 片"));

    // 传上去之后，服务端更新的版本照常生效（以服务端为准）
    client->online = true;
    emit client->onlineChanged();
    flushAll();
    QCOMPARE(vm->pendingCount(), 0);
    AppCore::instance()->applyServerPlans({ serverPlan(5, QStringLiteral("家人改的")) });
    QCOMPARE(planById(31).value(QStringLiteral("title")).toString(), QStringLiteral("家人改的"));
    delete vm;
}

void TestSync::pulledCopyBeforeAckIsMerged()
{
    // 拉取抢在 POST 回执前，把这条新计划当"服务端新计划"拉了下来：回执换号时合并，不能变两条
    AppCore::instance()->resetStorage();
    const qint64 localId = createTestPlan(QStringLiteral("叶酸"));
    AppCore::instance()->applyServerPlans({ QVariantMap {
        { QStringLiteral("planId"), 31 },
        { QStringLiteral("title"), QStringLiteral("叶酸") },
        { QStringLiteral("rule"), QVariantMap {
              { QStringLiteral("kind"), QStringLiteral("daily") },
              { QStringLiteral("times"), QStringList { QStringLiteral("23:59") } } } },
        { QStringLiteral("version"), 1 },
        { QStringLiteral("deleted"), false },
    } });
    AppCore::instance()->markPlanCreated(localId, 31, 0, 1);

    int copies = 0;
    for (const QVariant &p : AppCore::instance()->plans())
        copies += p.toMap().value(QStringLiteral("title")).toString() == QStringLiteral("叶酸");
    QCOMPARE(copies, 1);
    int occs = 0;
    for (const QVariant &o : todayOccurrences())
        occs += o.toMap().value(QStringLiteral("planId")).toLongLong() == 31;
    QCOMPARE(occs, 1);
    QCOMPARE(AppCore::instance()->pendingPlanCount(), 0);
}

void TestSync::missingPlansRouteKeepsQueue()
{
    AppCore::instance()->resetStorage();
    const qint64 localId = createTestPlan(QStringLiteral("维生素"));
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);
    client->plansStatus = 404;
    vm->retrySync();
    flushAll();
    QCOMPARE(vm->pendingCount(), 1); // 留在队列里退避，不标拒收
    QVERIFY(!planById(localId).value(QStringLiteral("syncRejected")).toBool());
    delete vm;
}

namespace {
QJsonObject serverPlanJson(qint64 id, const QString &title)
{
    return QJsonObject {
        { QLatin1String("plan_id"), id },
        { QLatin1String("title"), title },
        { QLatin1String("rule"), QJsonObject {
              { QLatin1String("kind"), QStringLiteral("daily") },
              { QLatin1String("times"), QJsonArray { QStringLiteral("23:59") } } } },
        { QLatin1String("version"), 1 },
        { QLatin1String("deleted"), false },
    };
}
} // namespace

void TestSync::hardDeletedPlanRemovedOnManualSync()
{
    // 云端看板删计划是整行 DELETE：增量拉取收不到删除标记。
    // 手动同步拉全量（since=0），名单里没有的服务端计划在本机标删除。
    AppCore::instance()->resetStorage();
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client);

    QJsonArray serverPlans { serverPlanJson(2, QStringLiteral("喝水")),
                             serverPlanJson(3, QStringLiteral("钙片")) };
    QString lastSince;
    client->plansHandler = [&serverPlans, &lastSince](const QUrlQuery &query) {
        lastSince = query.queryItemValue(QStringLiteral("since"));
        return QJsonObject { { QLatin1String("next_seq"), 300 },
                             { QLatin1String("plans"), serverPlans } };
    };
    vm->retrySync();
    flushAll();
    QCOMPARE(lastSince, QStringLiteral("0"));
    QVERIFY(occurrenceIdOfPlan(3) != 0);

    const qint64 localId = createTestPlan(QStringLiteral("本机新建")); // 还没上传，不能被对账删掉
    client->online = false; // 先别让它上传
    flushAll();

    serverPlans = QJsonArray { serverPlanJson(2, QStringLiteral("喝水")) }; // 3 号在云端被硬删
    client->online = true;
    client->plansStatus = 404; // 让本机新建的计划留在 localOnly，专门验证对账不碰它
    vm->retrySync();
    flushAll();

    // plans() 只列未删除的计划
    QVERIFY(planById(3).isEmpty());
    QCOMPARE(occurrenceIdOfPlan(3), 0); // 今天的提醒也撤掉
    QVERIFY(!planById(2).isEmpty());
    QVERIFY(!planById(localId).isEmpty());
    delete vm;
}

void TestSync::emptyFullListDeletesNothing()
{
    // 全量名单为空（服务端出错/换了账号）时不对账，免得把本机计划全删光
    AppCore::instance()->resetStorage();
    AppCore::instance()->applyServerPlans({ planFromServerForTest(2, QStringLiteral("喝水")) });
    SyncVM *vm = nullptr;
    FakeHttpClient *client = nullptr;
    vm = makeSync(&client); // 默认 /plans 回空列表
    vm->retrySync();
    flushAll();
    QVERIFY(!planById(2).isEmpty());
    delete vm;
}

QTEST_MAIN(TestSync)
#include "tst_sync.moc"
