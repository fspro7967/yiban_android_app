#include "sync_vm.h"

#include "app_core.h"
#include "app_settings.h"
#include "facade_errors.h"
#include "http_client.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QVariantMap>

namespace {

// 服务端 snake_case ↔ 端上 camelCase（规范 §2.3：不穿透到 QML）。
QVariantMap occurrenceFromServer(const QJsonObject &o)
{
    return {
        { QStringLiteral("occurrenceId"), static_cast<qint64>(o.value(QLatin1String("occurrence_id")).toDouble()) },
        { QStringLiteral("planId"), static_cast<qint64>(o.value(QLatin1String("plan_id")).toDouble()) },
        { QStringLiteral("title"), o.value(QLatin1String("title")).toString() },
        { QStringLiteral("dose"), o.value(QLatin1String("dose")).toString() },
        { QStringLiteral("note"), o.value(QLatin1String("note")).toString() },
        { QStringLiteral("dueTs"), static_cast<qint64>(o.value(QLatin1String("due_ts")).toDouble()) },
        { QStringLiteral("state"), o.value(QLatin1String("state")).toString() },
        { QStringLiteral("snoozeCount"), o.value(QLatin1String("snooze_count")).toInt() },
        { QStringLiteral("snoozeUntilTs"), static_cast<qint64>(o.value(QLatin1String("snooze_until_ts")).toDouble()) },
        { QStringLiteral("windowMin"), o.value(QLatin1String("window_min")).toInt() },
        { QStringLiteral("escalateAfterMin"), o.value(QLatin1String("escalate_after_min")).toInt() },
    };
}

QVariantMap planFromServer(const QJsonObject &o)
{
    QVariantMap rule;
    const QJsonObject ruleJson = o.value(QLatin1String("rule")).toObject();
    for (auto it = ruleJson.begin(); it != ruleJson.end(); ++it)
        rule.insert(it.key(), it.value().toVariant());

    QStringList skipDates;
    for (const auto &d : o.value(QLatin1String("skip_dates")).toArray())
        skipDates.append(d.toString());

    return {
        { QStringLiteral("planId"), static_cast<qint64>(o.value(QLatin1String("plan_id")).toDouble()) },
        { QStringLiteral("title"), o.value(QLatin1String("title")).toString() },
        { QStringLiteral("dose"), o.value(QLatin1String("dose")).toString() },
        { QStringLiteral("note"), o.value(QLatin1String("note")).toString() },
        { QStringLiteral("rule"), rule },
        { QStringLiteral("windowMin"), o.value(QLatin1String("window_min")).toInt() },
        { QStringLiteral("escalateAfterMin"), o.value(QLatin1String("escalate_after_min")).toInt() },
        { QStringLiteral("allowLate"), o.value(QLatin1String("allow_late")).toBool(true) },
        { QStringLiteral("skipDates"), skipDates },
        { QStringLiteral("version"), static_cast<qint64>(o.value(QLatin1String("version")).toDouble()) },
        { QStringLiteral("deleted"), o.value(QLatin1String("deleted")).toBool() },
    };
}

QJsonObject eventToServer(const QVariantMap &event)
{
    return {
        { QLatin1String("event_id"), event.value(QStringLiteral("eventId")).toString() },
        { QLatin1String("occurrence_id"), event.value(QStringLiteral("occurrenceId")).toLongLong() },
        { QLatin1String("action"), event.value(QStringLiteral("action")).toString() },
        { QLatin1String("ts"), event.value(QStringLiteral("ts")).toLongLong() },
        { QLatin1String("device_id"), event.value(QStringLiteral("deviceId")).toString() },
        { QLatin1String("reason"), event.value(QStringLiteral("reason")).toString() },
    };
}

// 记录：时间字段叫 at（毫秒），和契约 v0.1 及云端 v1_adapter._record_write 一致——
// 云端只认 at，发成 ts 会被当成缺省、落成上传时刻。
// client_id 供服务端在 POST 重试时去重（本地联调服务端支持，云端暂不认）。
QJsonObject recordToServer(const QVariantMap &rec, bool forPatch)
{
    QJsonObject body {
        { QLatin1String("value"), rec.value(QStringLiteral("value")).toDouble() },
        { QLatin1String("unit"), rec.value(QStringLiteral("unit")).toString() },
        { QLatin1String("at"), rec.value(QStringLiteral("at")).toLongLong() },
    };
    if (!forPatch) {
        body.insert(QLatin1String("type"), rec.value(QStringLiteral("type")).toString());
        body.insert(QLatin1String("client_id"), rec.value(QStringLiteral("clientId")).toString());
        body.insert(QLatin1String("device_id"), AppSettings::instance()->deviceId());
    }
    return body;
}

// 计划：端上 camelCase → 规范 §8 snake_case。planId 不发：新建由服务端分配，
// 改/删走路径里的 id。
QJsonObject planToServer(const QVariantMap &plan)
{
    return {
        { QLatin1String("title"), plan.value(QStringLiteral("title")).toString() },
        { QLatin1String("dose"), plan.value(QStringLiteral("dose")).toString() },
        { QLatin1String("note"), plan.value(QStringLiteral("note")).toString() },
        { QLatin1String("rule"),
          QJsonObject::fromVariantMap(plan.value(QStringLiteral("rule")).toMap()) },
        { QLatin1String("window_min"), plan.value(QStringLiteral("windowMin")).toInt() },
        { QLatin1String("escalate_after_min"),
          plan.value(QStringLiteral("escalateAfterMin")).toInt() },
        { QLatin1String("allow_late"), plan.value(QStringLiteral("allowLate"), true).toBool() },
    };
}

QVariantList ackedFromServer(const QJsonArray &accepted)
{
    QVariantList out;
    for (const auto &item : accepted) {
        const QJsonObject o = item.toObject();
        out.append(QVariantMap {
            { QStringLiteral("eventId"), o.value(QLatin1String("event_id")).toString() },
            { QStringLiteral("serverSeq"), static_cast<qint64>(o.value(QLatin1String("server_seq")).toDouble()) },
        });
    }
    return out;
}

constexpr int kMaxBackoffMs = 5 * 60 * 1000;

} // namespace

SyncVM::SyncVM(QObject *parent)
    : QObject(parent)
{
    m_http = m_network = new NetworkHttpClient(this);

    m_stateTimer.setInterval(15 * 1000);
    connect(&m_stateTimer, &QTimer::timeout, this, &SyncVM::heartbeat);
    m_planTimer.setInterval(60 * 1000);
    connect(&m_planTimer, &QTimer::timeout, this, &SyncVM::onPlanTimer);
    m_uploadBackoff.setSingleShot(true);
    connect(&m_uploadBackoff, &QTimer::timeout, this, &SyncVM::uploadPending);
    m_recordBackoff.setSingleShot(true);
    connect(&m_recordBackoff, &QTimer::timeout, this, &SyncVM::uploadRecords);
    m_planBackoff.setSingleShot(true);
    connect(&m_planBackoff, &QTimer::timeout, this, &SyncVM::uploadPlans);

    AppCore *core = AppCore::instance();
    // 队列投递：ack 回调里 markEventsSynced 会再发 eventsChanged，
    // 直连会在同一栈帧里递归发起下一轮上传。
    connect(core, &AppCore::eventsChanged, this, &SyncVM::uploadPending,
            Qt::QueuedConnection);
    connect(core, &AppCore::recordsChanged, this, &SyncVM::uploadRecords,
            Qt::QueuedConnection);
    // 撤销记录会发 recordsChanged（墓碑入队/清掉）：DELETE 也跟着冲一次。
    connect(core, &AppCore::recordsChanged, this, &SyncVM::uploadRecordDeletes,
            Qt::QueuedConnection);
    connect(core, &AppCore::plansChanged, this, &SyncVM::uploadPlans,
            Qt::QueuedConnection);
    connect(core, &AppCore::plansChanged, this, &SyncVM::syncChanged);
    // pendingCount 挂在 syncChanged 上：事件入队/ack、记录增改都要刷新，
    // 否则离线时按 SOS、记一杯水，管理页的"待上传"数字不动。
    connect(core, &AppCore::eventsChanged, this, &SyncVM::syncChanged);
    connect(core, &AppCore::recordsChanged, this, &SyncVM::syncChanged);
    connect(core, &AppCore::occurrencesChanged, this, &SyncVM::syncChanged);
    connect(core, &AppCore::toast, this, &SyncVM::toast);

    connectClient(m_network);
    m_stateTimer.start();
    m_planTimer.start();
}

void SyncVM::setHttpClient(HttpClient *client)
{
    if (!client || client == m_http)
        return;
    if (m_http && m_http != m_network)
        m_http->deleteLater();
    m_http = client;
    if (!qobject_cast<NetworkHttpClient *>(client))
        m_network = nullptr; // 假实现：token 读取交给假实现自己
    client->setParent(this);
    connectClient(client);
}

void SyncVM::connectClient(HttpClient *client)
{
    // 回执必须异步处理（Qt::QueuedConnection 强制）：HttpClient 合约规定
    // reply/failed 不得在 postJson/getJson 调用栈内同步发出——同步发出时
    // requestId 还没赋给 m_*Req，回执会被整个丢掉。
    connect(client, &HttpClient::reply, this, &SyncVM::onReply, Qt::QueuedConnection);
    connect(client, &HttpClient::failed, this, &SyncVM::onFailed, Qt::QueuedConnection);
    // 队列投递：系统网络回调里不直接发请求
    connect(client, &HttpClient::onlineChanged, this, &SyncVM::onOnlineChanged,
            Qt::QueuedConnection);
}

void SyncVM::onOnlineChanged()
{
    // 只认当前在用的客户端（测试换成假实现后，真实客户端的通知不算数）
    if (sender() && sender() != m_http)
        return;
    emit syncChanged(); // syncState 可能在 offline 与其他状态之间切换
    if (!canSync())
        return;
    // 刚恢复联网：断网期间攒下的退避没有意义，清掉立刻补传
    m_uploadBackoff.stop();
    m_recordBackoff.stop();
    m_planBackoff.stop();
    m_backoffMs = 0;
    m_recordBackoffMs = 0;
    m_planBackoffMs = 0;
    cycle();
}

void SyncVM::heartbeat()
{
    if (!m_planBackoff.isActive())
        uploadPlans();
    if (!m_uploadBackoff.isActive())
        uploadPending();
    if (!m_recordBackoff.isActive())
        uploadRecords();
    if (!m_recordBackoff.isActive())
        uploadRecordDeletes();
    pullState();
}

QString SyncVM::computeState() const
{
    if (!canSync())
        return QStringLiteral("offline");
    if (m_uploadReq || m_alarmReq || m_recordReq || m_recordDeleteReq
        || m_planReq || m_stateReq || m_plansReq)
        return QStringLiteral("syncing");
    if (m_failed)
        return QStringLiteral("error");
    return QStringLiteral("idle");
}

QString SyncVM::syncState() const
{
    return computeState();
}

qint64 SyncVM::lastSyncTs() const
{
    return AppSettings::instance()->lastSyncTs();
}

int SyncVM::pendingCount() const
{
    return AppCore::instance()->pendingEvents().size()
        + AppCore::instance()->pendingRecordCount()
        + AppCore::instance()->pendingDeleteRecordCount()
        + AppCore::instance()->pendingPlanCount();
}

bool SyncVM::canSync() const
{
    if (!AppSettings::instance()->paired())
        return false;
    // 离线演示配对时没有令牌；没有令牌就发请求只会换来 401，
    // 和"待同步"提示混在一起，看不出真正原因。
    if (AppSettings::instance()->token().trimmed().isEmpty())
        return false;
    return m_http && m_http->isOnline();
}

void SyncVM::retrySync()
{
    if (!AppSettings::instance()->paired()
        || AppSettings::instance()->token().trimmed().isEmpty()) {
        AppCore::instance()->logOperation(
            QStringLiteral("管理"),
            QStringLiteral("手动同步：没有服务器令牌，%1 条记录待上传").arg(pendingCount()));
        emit commandRejected(QStringLiteral("offline"),
                             QStringLiteral("还没有服务器令牌，请先配对或粘贴令牌"));
        return;
    }
    AppCore::instance()->logOperation(
        QStringLiteral("管理"),
        QStringLiteral("手动同步：%1 条记录待上传").arg(pendingCount()));
    // 手动同步是用户明确要求的重试：跳过上一轮 rejected[retryable:true]
    // 或网络失败留下的退避，立刻再试一次。
    m_uploadBackoff.stop();
    m_backoffMs = 0;
    m_failed = false;
    emit syncChanged();
    cycle();
}

void SyncVM::cycle()
{
    uploadPlans(); // 计划先走：新计划换号后，挂在它上面的事件才能传
    uploadPending();
    uploadRecords();
    uploadRecordDeletes();
    pullState();
    pullPlans(true); // 手动同步/重连：拉全量对账，云端删掉的计划这里能发现
}

void SyncVM::uploadPending()
{
    // 退避期间不重发：rejected[retryable:true] 和网络失败都走这条路，
    // 否则 eventsChanged 会在同一轮事件循环里把 busy 事件立刻打回去。
    if (m_uploadBackoff.isActive())
        return;
    if (m_uploadReq || m_alarmReq || !canSync())
        return;
    // 挂在还没上传的计划上的事件先留着（服务端没有那个 occurrence）
    const QVariantList pending = AppCore::instance()->uploadableEvents();
    if (pending.isEmpty())
        return;

    // SOS 呼救（occurrenceId=0 的 escalate）不带实例，/events 会因
    // occurrence_not_found 被拒；契约上它走 POST /v1/alarm。一次传一条，
    // ack 后 eventsChanged 会再次触发本函数处理剩余事件。
    for (const QVariant &entry : pending) {
        const QVariantMap event = entry.toMap();
        if (event.value(QStringLiteral("occurrenceId")).toLongLong() != 0)
            continue;
        if (event.value(QStringLiteral("action")).toString() != QLatin1String("escalate"))
            continue;

        QJsonObject body;
        body.insert(QLatin1String("level"), 3);
        body.insert(QLatin1String("source"), QStringLiteral("manual"));
        body.insert(QLatin1String("occurrence_id"), 0);
        body.insert(QLatin1String("reason"), event.value(QStringLiteral("reason")).toString());
        body.insert(QLatin1String("device_id"), AppSettings::instance()->deviceId());
        m_alarmEventId = event.value(QStringLiteral("eventId")).toString();
        m_alarmReq = m_http->postJson(QStringLiteral("/alarm"), body);
        emit syncChanged();
        return;
    }

    QJsonArray events;
    for (const QVariant &entry : pending)
        events.append(eventToServer(entry.toMap()));

    QJsonObject body;
    body.insert(QLatin1String("device_id"), AppSettings::instance()->deviceId());
    body.insert(QLatin1String("events"), events);
    m_uploadReq = m_http->postJson(QStringLiteral("/events"), body);
    emit syncChanged();
}

void SyncVM::uploadRecords()
{
    if (m_recordReq || !canSync())
        return;
    const QVariantList pending = AppCore::instance()->pendingRecords();
    if (pending.isEmpty())
        return;

    const QVariantMap rec = pending.first().toMap();
    const qint64 serverId = rec.value(QStringLiteral("serverRecordId")).toLongLong();
    m_recordLocalId = rec.value(QStringLiteral("recordId")).toLongLong();
    m_recordRev = rec.value(QStringLiteral("syncRev")).toLongLong();
    m_recordIsPatch = serverId > 0;
    m_recordReq = m_recordIsPatch
        ? m_http->patchJson(QStringLiteral("/records/%1").arg(serverId),
                            recordToServer(rec, true))
        : m_http->postJson(QStringLiteral("/records"), recordToServer(rec, false));
    emit syncChanged();
}

void SyncVM::uploadRecordDeletes()
{
    if (m_recordDeleteReq || m_recordReq || !canSync())
        return;
    const QVariantList pending = AppCore::instance()->pendingRecordDeletes();
    if (pending.isEmpty())
        return;
    const QVariantMap item = pending.first().toMap();
    m_recordDeleteId = item.value(QStringLiteral("recordId")).toLongLong();
    const qint64 serverId = item.value(QStringLiteral("serverRecordId")).toLongLong();
    if (serverId <= 0) {
        // 脏墓碑（不该出现）：直接清掉，别卡住队列
        AppCore::instance()->markRecordDeleted(m_recordDeleteId);
        m_recordDeleteId = 0;
        return;
    }
    m_recordDeleteReq = m_http->deleteJson(QStringLiteral("/records/%1").arg(serverId));
    emit syncChanged();
}

void SyncVM::onRecordDeleteReply(int status, const QJsonObject &body)
{
    Q_UNUSED(body)
    const qint64 localId = m_recordDeleteId;
    m_recordDeleteReq = 0;
    m_recordDeleteId = 0;

    if ((status >= 200 && status < 300) || status == 404) {
        // 2xx 已删；404 服务端本来就没有这条，同样当成功（云组 C-4 契约）
        AppCore::instance()->markRecordDeleted(localId);
        m_recordBackoffMs = 0;
        noteSuccess();
    } else if (status == 401) {
        onUnauthorized();
        emit syncChanged();
        return;
    } else {
        m_failed = true;
        scheduleRecordRetry();
        emit syncChanged();
        return;
    }
    emit syncChanged();
    uploadRecordDeletes(); // 本地墓碑已更新，接着删下一条
}

void SyncVM::uploadPlans()
{
    if (m_planReq || !canSync())
        return;
    const QVariantList pending = AppCore::instance()->pendingPlans();
    if (pending.isEmpty())
        return;

    const QVariantMap plan = pending.first().toMap();
    m_planId = plan.value(QStringLiteral("planId")).toLongLong();
    m_planRev = plan.value(QStringLiteral("syncRev")).toLongLong();
    m_planOp = plan.value(QStringLiteral("op")).toString();
    const QString path = QStringLiteral("/plans/%1").arg(m_planId);
    if (m_planOp == QLatin1String("create"))
        m_planReq = m_http->postJson(QStringLiteral("/plans"), planToServer(plan));
    else if (m_planOp == QLatin1String("delete"))
        m_planReq = m_http->deleteJson(path);
    else
        m_planReq = m_http->patchJson(path, planToServer(plan));
    emit syncChanged();
}

void SyncVM::pullState()
{
    if (m_stateReq || !canSync())
        return;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("since"),
                       QString::number(AppSettings::instance()->stateSeq()));
    m_stateReq = m_http->getJson(QStringLiteral("/state"), query);
    emit syncChanged();
}

void SyncVM::onPlanTimer()
{
    // 增量拉取收不到云端硬删除：每 5 分钟（含启动后第一次）拉一次全量对账
    pullPlans(m_planTicks++ % 5 == 0);
}

void SyncVM::pullPlans(bool full)
{
    if (!canSync())
        return;
    if (m_plansReq) {
        if (full && !m_plansReqFull)
            m_fullPlansPending = true;
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("since"),
                       full ? QStringLiteral("0")
                            : QString::number(AppSettings::instance()->plansSeq()));
    m_plansReqFull = full;
    m_plansReq = m_http->getJson(QStringLiteral("/plans"), query);
    emit syncChanged();
}

void SyncVM::scheduleRecordRetry()
{
    m_recordBackoffMs =
        m_recordBackoffMs <= 0 ? 1000 : qMin(kMaxBackoffMs, m_recordBackoffMs * 2);
    m_recordBackoff.start(m_recordBackoffMs);
}

void SyncVM::onUnauthorized()
{
    m_failed = true;
    AppSettings::instance()->clearToken();
    AppSettings::instance()->setPairing(false, QString());
    emit commandRejected(QStringLiteral("offline"),
                         QStringLiteral("登录已失效，请重新配对"));
}

void SyncVM::schedulePlanRetry()
{
    m_planBackoffMs = m_planBackoffMs <= 0 ? 1000 : qMin(kMaxBackoffMs, m_planBackoffMs * 2);
    m_planBackoff.start(m_planBackoffMs);
}

void SyncVM::onPlanReply(int status, const QJsonObject &body)
{
    const qint64 planId = m_planId;
    const QString op = m_planOp;
    m_planReq = 0;
    m_planId = 0;
    AppCore *core = AppCore::instance();
    const qint64 serverVersion =
        static_cast<qint64>(body.value(QLatin1String("version")).toDouble());

    auto backoff = [this] {
        m_failed = true;
        schedulePlanRetry();
        emit syncChanged();
    };

    if (status >= 200 && status < 300) {
        if (op == QLatin1String("create")) {
            const qint64 serverId =
                static_cast<qint64>(body.value(QLatin1String("plan_id")).toDouble());
            if (serverId <= 0)
                return backoff(); // 没有 plan_id 就没法换号，当失败重传
            core->markPlanCreated(planId, serverId, m_planRev, serverVersion);
        } else {
            core->markPlanSynced(planId, m_planRev, serverVersion);
        }
        m_planBackoffMs = 0;
        noteSuccess();
    } else if (status == 401) {
        onUnauthorized();
        emit syncChanged();
        return;
    } else if (status == 404 && op == QLatin1String("delete")) {
        core->markPlanSynced(planId, m_planRev, 0); // 服务端本来就没有：删除目的已达成
    } else if (status == 404 && op == QLatin1String("update")) {
        core->forgetServerPlan(planId); // 服务端没有这条：重新 POST（届时换号）
    } else if (op == QLatin1String("create") && (status == 404 || status == 405)) {
        return backoff(); // 服务端缺接口：留在队列里，别标拒收
    } else if (status >= 400 && status < 500 && status != 408 && status != 429) {
        core->rejectPlan(planId);
    } else {
        return backoff();
    }
    emit syncChanged();
    uploadPlans(); // 本地状态已同步更新，直接接着传下一条
}

void SyncVM::onRecordReply(int status, const QJsonObject &body)
{
    const qint64 localId = m_recordLocalId;
    const bool wasPatch = m_recordIsPatch;
    m_recordReq = 0;
    m_recordLocalId = 0;

    if (status >= 200 && status < 300) {
        const qint64 serverId =
            static_cast<qint64>(body.value(QLatin1String("record_id")).toDouble());
        if (!wasPatch && serverId <= 0) {
            // 新建回执没给 record_id：以后没法 PATCH，当失败退避重传
            // （POST 带 client_id，服务端去重，不会记两条）
            m_failed = true;
            scheduleRecordRetry();
            emit syncChanged();
            return;
        }
        AppCore::instance()->markRecordSynced(localId, wasPatch ? 0 : serverId, m_recordRev);
        m_recordBackoffMs = 0;
        noteSuccess();
    } else if (status == 401) {
        onUnauthorized();
        emit syncChanged();
        return;
    } else if (wasPatch && status == 404) {
        // 服务端没有这条（换了服务器/库被清）：改回重新 POST
        AppCore::instance()->forgetServerRecord(localId);
    } else if (!wasPatch && (status == 404 || status == 405)) {
        // POST 404/405 只能是服务端还没这个接口：不能当拒收（会把积压记录
        // 全标 -1 永久丢掉），留在队列里退避，等服务端补上
        m_failed = true;
        scheduleRecordRetry();
        emit syncChanged();
        return;
    } else if (status >= 400 && status < 500 && status != 408 && status != 429) {
        // 其余 4xx 是服务端明确拒收，重传不会变结果
        AppCore::instance()->rejectRecord(localId);
    } else {
        m_failed = true;
        scheduleRecordRetry();
        emit syncChanged();
        return;
    }
    emit syncChanged();
    uploadRecords(); // 本地状态已同步更新，直接接着传下一条
    uploadRecordDeletes(); // 上传途中被撤销的会在这里补出墓碑，顺手发 DELETE
}

void SyncVM::scheduleUploadRetry()
{
    m_backoffMs = m_backoffMs <= 0 ? 1000 : qMin(kMaxBackoffMs, m_backoffMs * 2);
    m_uploadBackoff.start(m_backoffMs);
}

void SyncVM::noteSuccess()
{
    m_backoffMs = 0;
    m_failed = false;
    AppSettings::instance()->setLastSyncTs(QDateTime::currentMSecsSinceEpoch());
    emit syncChanged();
}

void SyncVM::onReply(quint64 requestId, int status, const QJsonObject &body)
{
    if (requestId == 0)
        return;
    const bool ok = status >= 200 && status < 300;

    if (requestId == m_recordReq) {
        onRecordReply(status, body);
        return;
    }
    if (requestId != 0 && requestId == m_recordDeleteReq) {
        onRecordDeleteReply(status, body);
        return;
    }
    if (requestId == m_planReq) {
        onPlanReply(status, body);
        return;
    }

    if (requestId == m_alarmReq) {
        m_alarmReq = 0;
        const QString eventId = m_alarmEventId;
        m_alarmEventId.clear();
        if (ok) {
            // 告警 ack：服务端一定回了 server_seq（契约 §8）；缺失时给 1 占位，
            // 只要不为 0 就能把事件从待上传队列里摘掉。
            const qint64 seq = static_cast<qint64>(
                body.value(QLatin1String("server_seq")).toDouble());
            AppCore::instance()->markEventsSynced(QVariantList {
                QVariantMap {
                    { QStringLiteral("eventId"), eventId },
                    { QStringLiteral("serverSeq"), seq > 0 ? seq : 1 },
                },
            });
            noteSuccess();
        } else if (status == 401) {
            onUnauthorized();
        } else {
            m_failed = true;
            scheduleUploadRetry();
        }
        emit syncChanged();
        return;
    }

    if (requestId == m_uploadReq) {
        m_uploadReq = 0;
        if (ok) {
            // accepted 里的都算已提交（applied=false 是服务端去重，同样
            // 不需要再传）；rejected 按 retryable 分流（1124TODO B2-1）：
            //   * false 或字段缺失（旧云端）→ 终态过期，不无限重放；
            //   * true（busy / internal_error）→ 留在队列，按退避下一轮重发。
            QVariantList accepted = ackedFromServer(
                body.value(QLatin1String("accepted")).toArray());
            QStringList expired;
            bool hasRetryable = false;
            for (const auto &item : body.value(QLatin1String("rejected")).toArray()) {
                const QJsonObject rejected = item.toObject();
                const QString eventId =
                    rejected.value(QLatin1String("event_id")).toString();
                if (eventId.isEmpty())
                    continue;
                if (rejected.value(QLatin1String("retryable")).toBool(false))
                    hasRetryable = true;
                else
                    expired.append(eventId);
            }
            AppCore::instance()->markEventsSynced(accepted);
            if (!expired.isEmpty())
                AppCore::instance()->expireEvents(expired);
            noteSuccess();
            if (hasRetryable) {
                // 留队的事件不能被 eventsChanged 触发的即时上传又原样发出去，
                // 起退避定时器；uploadPending() 在退避期间直接返回。
                scheduleUploadRetry();
            }
        } else if (status == 409) {
            // 服务端把状态冲突作为顶层 409 返回（不是 200 里的 rejected 数组）。
            // 这类事件重传永远不会成功：按 occurrence_id 找出本批事件并标记过期，
            // 不要走退避重发，否则会像 11/22 那样连续打十几次 /events 全 409。
            const QJsonObject err = body.value(QLatin1String("error")).toObject();
            const QString code = err.value(QLatin1String("code")).toString();
            const qlonglong conflictOccurrence =
                static_cast<qlonglong>(err.value(QLatin1String("occurrence_id")).toDouble());
            const QVariantList pending = AppCore::instance()->pendingEvents();
            QStringList stale;
            for (const QVariant &entry : pending) {
                const QVariantMap event = entry.toMap();
                if (code == QLatin1String("state_conflict") && conflictOccurrence != 0
                    && event.value(QLatin1String("occurrenceId")).toLongLong() != conflictOccurrence)
                    continue;
                const QString eventId = event.value(QLatin1String("eventId")).toString();
                if (!eventId.isEmpty())
                    stale.append(eventId);
            }
            if (!stale.isEmpty())
                AppCore::instance()->expireEvents(stale);
            // 不是网络失败：清掉退避，让剩余事件立刻继续上传。
            m_failed = false;
            m_backoffMs = 0;
        } else if (status == 401) {
            // token 失效：不再无限重试，等用户重新配对
            onUnauthorized();
        } else {
            m_failed = true;
            scheduleUploadRetry();
        }
        emit syncChanged();
        return;
    }

    if (requestId == m_stateReq) {
        m_stateReq = 0;
        if (ok) {
            QVariantList occurrences;
            for (const auto &item :
                 body.value(QLatin1String("occurrences")).toArray())
                occurrences.append(occurrenceFromServer(item.toObject()));
            AppCore::instance()->applyServerOccurrences(occurrences);
            AppSettings::instance()->setStateSeq(
                static_cast<qint64>(body.value(QLatin1String("next_seq")).toDouble()));
            noteSuccess();
        } else {
            m_failed = true;
        }
        emit syncChanged();
        return;
    }

    if (requestId == m_plansReq) {
        m_plansReq = 0;
        const bool full = m_plansReqFull;
        m_plansReqFull = false;
        if (ok) {
            QVariantList plans;
            QList<qint64> ids;
            for (const auto &item : body.value(QLatin1String("plans")).toArray()) {
                const QVariantMap plan = planFromServer(item.toObject());
                ids.append(plan.value(QStringLiteral("planId")).toLongLong());
                plans.append(plan);
            }
            AppCore::instance()->applyServerPlans(plans);
            if (full && body.value(QLatin1String("plans")).isArray())
                AppCore::instance()->reconcileServerPlans(ids);
            AppSettings::instance()->setPlansSeq(
                static_cast<qint64>(body.value(QLatin1String("next_seq")).toDouble()));
            noteSuccess();
        } else {
            m_failed = true;
        }
        emit syncChanged();
        if (m_fullPlansPending) {
            m_fullPlansPending = false;
            pullPlans(true);
        }
        return;
    }
}

void SyncVM::onFailed(quint64 requestId, const QString &code)
{
    Q_UNUSED(code)
    if (requestId != 0 && requestId == m_alarmReq) {
        m_alarmReq = 0;
        m_alarmEventId.clear();
        m_failed = true;
        scheduleUploadRetry();
        emit syncChanged();
        return;
    }
    if (requestId != 0 && requestId == m_planReq) {
        m_planReq = 0;
        m_planId = 0;
        m_failed = true;
        schedulePlanRetry();
        emit syncChanged();
        return;
    }
    if (requestId != 0 && requestId == m_recordReq) {
        m_recordReq = 0;
        m_recordLocalId = 0;
        m_failed = true;
        scheduleRecordRetry();
        emit syncChanged();
        return;
    }
    if (requestId != 0 && requestId == m_recordDeleteReq) {
        // 网络失败：墓碑留在队列里，等退避/恢复联网后补发 DELETE
        m_recordDeleteReq = 0;
        m_recordDeleteId = 0;
        m_failed = true;
        scheduleRecordRetry();
        emit syncChanged();
        return;
    }
    if (requestId != 0 && requestId == m_uploadReq) {
        m_uploadReq = 0;
        m_failed = true;
        scheduleUploadRetry();
        emit syncChanged();
        return;
    }
    if (requestId != 0 && (requestId == m_stateReq || requestId == m_plansReq)) {
        if (requestId == m_stateReq)
            m_stateReq = 0;
        if (requestId == m_plansReq) {
            m_plansReq = 0;
            m_plansReqFull = false;
        }
        m_failed = true;
        emit syncChanged();
    }
}

QString SyncVM::eventLogCsv() const
{
    return AppCore::instance()->eventLogCsv();
}
