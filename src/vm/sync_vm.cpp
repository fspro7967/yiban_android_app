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
    connect(&m_stateTimer, &QTimer::timeout, this, &SyncVM::pullState);
    m_planTimer.setInterval(60 * 1000);
    connect(&m_planTimer, &QTimer::timeout, this, &SyncVM::pullPlans);
    m_uploadBackoff.setSingleShot(true);
    connect(&m_uploadBackoff, &QTimer::timeout, this, &SyncVM::uploadPending);

    AppCore *core = AppCore::instance();
    // 队列投递：ack 回调里 markEventsSynced 会再发 eventsChanged，
    // 直连会在同一栈帧里递归发起下一轮上传。
    connect(core, &AppCore::eventsChanged, this, &SyncVM::uploadPending,
            Qt::QueuedConnection);
    connect(core, &AppCore::occurrencesChanged, this, &SyncVM::syncChanged);
    connect(core, &AppCore::toast, this, &SyncVM::toast);

    // 回执必须异步处理（Qt::QueuedConnection 强制）：HttpClient 合约规定
    // reply/failed 不得在 postJson/getJson 调用栈内同步发出——同步发出时
    // requestId 还没赋给 m_*Req，回执会被整个丢掉。
    if (HttpClient *client = qobject_cast<HttpClient *>(m_network)) {
        connect(client, &HttpClient::reply, this, &SyncVM::onReply, Qt::QueuedConnection);
        connect(client, &HttpClient::failed, this, &SyncVM::onFailed, Qt::QueuedConnection);
    }
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
    connect(client, &HttpClient::reply, this, &SyncVM::onReply, Qt::QueuedConnection);
    connect(client, &HttpClient::failed, this, &SyncVM::onFailed, Qt::QueuedConnection);
}

QString SyncVM::computeState() const
{
    if (!canSync())
        return QStringLiteral("offline");
    if (m_uploadReq || m_stateReq || m_plansReq)
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
    return AppCore::instance()->pendingEvents().size();
}

bool SyncVM::canSync() const
{
    if (!AppSettings::instance()->paired())
        return false;
    return m_http && m_http->isOnline();
}

void SyncVM::retrySync()
{
    if (!AppSettings::instance()->paired()) {
        emit commandRejected(QStringLiteral("offline"),
                             facadeErrorText(QStringLiteral("offline")));
        return;
    }
    m_failed = false;
    emit syncChanged();
    cycle();
}

void SyncVM::cycle()
{
    uploadPending();
    pullState();
    pullPlans();
}

void SyncVM::uploadPending()
{
    if (m_uploadReq || !canSync())
        return;
    const QVariantList pending = AppCore::instance()->pendingEvents();
    if (pending.isEmpty())
        return;

    QJsonArray events;
    for (const QVariant &entry : pending)
        events.append(eventToServer(entry.toMap()));

    QJsonObject body;
    body.insert(QLatin1String("device_id"), AppSettings::instance()->deviceId());
    body.insert(QLatin1String("events"), events);
    m_uploadReq = m_http->postJson(QStringLiteral("/events"), body);
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

void SyncVM::pullPlans()
{
    if (m_plansReq || !canSync())
        return;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("since"),
                       QString::number(AppSettings::instance()->plansSeq()));
    m_plansReq = m_http->getJson(QStringLiteral("/plans"), query);
    emit syncChanged();
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

    if (requestId == m_uploadReq) {
        m_uploadReq = 0;
        if (ok) {
            // accepted 里的都算已提交（applied=false 是服务端去重，同样
            // 不需要再传）；rejected 是服务端明确拒绝（过时状态、找不到
            // occurrence），重传也不会变结果，标记过期防止无限重放。
            QVariantList accepted = ackedFromServer(
                body.value(QLatin1String("accepted")).toArray());
            QStringList rejected;
            for (const auto &item : body.value(QLatin1String("rejected")).toArray())
                rejected.append(item.toObject().value(QLatin1String("event_id")).toString());
            AppCore::instance()->markEventsSynced(accepted);
            if (!rejected.isEmpty())
                AppCore::instance()->expireEvents(rejected);
            noteSuccess();
        } else if (status == 401) {
            // token 失效：不再无限重试，等用户重新配对
            m_failed = true;
            AppSettings::instance()->clearToken();
            AppSettings::instance()->setPairing(false, QString());
            emit commandRejected(QStringLiteral("offline"),
                                 QStringLiteral("登录已失效，请重新配对"));
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
        if (ok) {
            QVariantList plans;
            for (const auto &item : body.value(QLatin1String("plans")).toArray())
                plans.append(planFromServer(item.toObject()));
            AppCore::instance()->applyServerPlans(plans);
            AppSettings::instance()->setPlansSeq(
                static_cast<qint64>(body.value(QLatin1String("next_seq")).toDouble()));
            noteSuccess();
        } else {
            m_failed = true;
        }
        emit syncChanged();
        return;
    }
}

void SyncVM::onFailed(quint64 requestId, const QString &code)
{
    Q_UNUSED(code)
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
        if (requestId == m_plansReq)
            m_plansReq = 0;
        m_failed = true;
        emit syncChanged();
    }
}

QString SyncVM::eventLogCsv() const
{
    return AppCore::instance()->eventLogCsv();
}
