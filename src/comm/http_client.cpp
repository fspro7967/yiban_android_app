#include "http_client.h"

#include "app_settings.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kTransferTimeoutMs = 15000;
constexpr int kMaxAttempts = 3;      // 首次 + 2 次重试
constexpr int kBaseBackoffMs = 1000; // 1s → 2s（规范 §9 的 1/2/4/…/5min，端上取前两档够用）
constexpr int kLogCapacity = 80;
} // namespace

// ---------------------------------------------------------------- 网络日志

namespace NetworkLog {
namespace {
QMutex g_mutex;
QStringList g_lines; // 只有 append/recent/clear 会碰它，都带锁
} // namespace

void append(const QString &line)
{
    QMutexLocker lock(&g_mutex);
    g_lines.append(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
                   + QStringLiteral("  ") + line);
    while (g_lines.size() > kLogCapacity)
        g_lines.removeFirst();
}

QStringList recent()
{
    QMutexLocker lock(&g_mutex);
    return g_lines;
}

void clear()
{
    QMutexLocker lock(&g_mutex);
    g_lines.clear();
}

} // namespace NetworkLog

namespace {
QString method(const QString &path, bool isPost)
{
    return (isPost ? QStringLiteral("POST ") : QStringLiteral("GET ")) + path;
}
} // namespace

// ---------------------------------------------------------------- 离线占位

OfflineHttpClient::OfflineHttpClient(QObject *parent)
    : HttpClient(parent)
{
}

quint64 OfflineHttpClient::postJson(const QString &path, const QJsonObject &body)
{
    Q_UNUSED(body)
    const quint64 id = m_nextId++;
    NetworkLog::append(QStringLiteral("× %1 未发出（离线占位客户端）").arg(method(path, true)));
    QTimer::singleShot(0, this, [this, id] {
        emit failed(id, QStringLiteral("offline"));
    });
    return id;
}

quint64 OfflineHttpClient::getJson(const QString &path, const QUrlQuery &query)
{
    Q_UNUSED(query)
    const quint64 id = m_nextId++;
    NetworkLog::append(QStringLiteral("× %1 未发出（离线占位客户端）").arg(method(path, false)));
    QTimer::singleShot(0, this, [this, id] {
        emit failed(id, QStringLiteral("offline"));
    });
    return id;
}

// ---------------------------------------------------------------- 真实实现

NetworkHttpClient::NetworkHttpClient(QObject *parent)
    : HttpClient(parent)
    , m_net(new QNetworkAccessManager(this))
{
    // 没有可用的 NetworkInformation 后端时（极少数桌面环境），
    // instance() 返回空，isOnline() 就退回"乐观在线"，让请求自己失败。
    QNetworkInformation::loadDefaultBackend();
}

NetworkHttpClient::~NetworkHttpClient()
{
    // 析构时还挂着的请求直接掐掉：回调里会访问 this，必须先断干净
    const auto replies = m_replyIds.keys();
    for (QNetworkReply *reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_replyIds.clear();
    m_jobs.clear();
    m_startedAt.clear();
}

bool NetworkHttpClient::isOnline() const
{
    auto *info = QNetworkInformation::instance();
    if (!info)
        return true;
    const auto reachability = info->reachability();
    // Unknown 在刚启动的两三秒里很常见，当成在线，别一上来就报 offline
    return reachability == QNetworkInformation::Reachability::Online
        || reachability == QNetworkInformation::Reachability::Unknown;
}

QString NetworkHttpClient::token() const
{
    return AppSettings::instance()->token();
}

void NetworkHttpClient::setToken(const QString &token)
{
    AppSettings::instance()->setToken(token);
}

QString NetworkHttpClient::absoluteUrl(const QString &path) const
{
    QString base = AppSettings::instance()->serverUrl().trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    QString suffix = path;
    if (!suffix.startsWith(QLatin1Char('/')))
        suffix.prepend(QLatin1Char('/'));

    // path 已经是完整地址时原样用（方便调试时直连某个环境）
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return path;

    return base + suffix;
}

quint64 NetworkHttpClient::postJson(const QString &path, const QJsonObject &body)
{
    Job job;
    job.id = m_nextId++;
    job.path = path;
    job.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    job.isPost = true;
    return enqueue(job);
}

quint64 NetworkHttpClient::getJson(const QString &path, const QUrlQuery &query)
{
    Job job;
    job.id = m_nextId++;
    job.path = path;
    job.query = query;
    job.isPost = false;
    return enqueue(job);
}

quint64 NetworkHttpClient::enqueue(const Job &job)
{
    if (!isOnline()) {
        // 没网就别发：立刻回 offline，上层显示"已记录，待同步"
        const quint64 id = job.id;
        NetworkLog::append(QStringLiteral("× %1 未发出（设备离线）").arg(method(job.path, job.isPost)));
        QTimer::singleShot(0, this, [this, id] {
            emit failed(id, QStringLiteral("offline"));
        });
        return id;
    }
    m_jobs.insert(job.id, job);
    dispatch(job);
    return job.id;
}

void NetworkHttpClient::dispatch(const Job &job)
{
    QUrl url(absoluteUrl(job.path));
    if (!job.query.isEmpty())
        url.setQuery(job.query);

    m_startedAt.insert(job.id, QDateTime::currentMSecsSinceEpoch());
    if (job.attempt == 0) {
        NetworkLog::append(QStringLiteral("→ %1").arg(method(job.path, job.isPost)));
    } else {
        NetworkLog::append(QStringLiteral("→ %1（第 %2 次）")
                               .arg(method(job.path, job.isPost))
                               .arg(job.attempt + 1));
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=utf-8"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    // 6.7 才有 setTransferTimeout；更早的 Qt 靠重试兜住长挂的连接
    request.setTransferTimeout(kTransferTimeoutMs);
#endif
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString bearer = token();
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());

    QNetworkReply *reply = job.isPost ? m_net->post(request, job.body) : m_net->get(request);
    m_replyIds.insert(reply, job.id);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onFinished(reply); });
}

void NetworkHttpClient::onFinished(QNetworkReply *netReply)
{
    const quint64 id = m_replyIds.take(netReply);

    const QVariant statusAttr = netReply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusAttr.isValid() ? statusAttr.toInt() : 0;
    const QByteArray payload = netReply->readAll();
    const QNetworkReply::NetworkError netError = netReply->error();
    netReply->deleteLater();

    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return; // 已经被别处清掉（析构/取消）

    Job job = it.value();
    const qint64 started = m_startedAt.take(id);
    const qint64 elapsed = started > 0 ? QDateTime::currentMSecsSinceEpoch() - started : 0;

    if (status >= 200 && status < 300) {
        m_jobs.remove(id);
        NetworkLog::append(QStringLiteral("← %1 %2 %3ms")
                               .arg(status)
                               .arg(job.path)
                               .arg(elapsed));
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        emit reply(id, status, doc.object());
        return;
    }

    if (status > 0) {
        // 服务端答了话：4xx/5xx 都不重试。
        // 409 冲突（另一台设备已处理这条）由上层按规范 §9 回滚，这里只原样交出。
        m_jobs.remove(id);
        NetworkLog::append(QStringLiteral("← %1 %2 %3ms")
                               .arg(status)
                               .arg(job.path)
                               .arg(elapsed));
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        emit reply(id, status, doc.object());
        return;
    }

    // status == 0：连不上/超时/DNS 失败，属于传输层错误，可以退避重试
    if (job.attempt + 1 < kMaxAttempts) {
        ++job.attempt;
        m_jobs.insert(id, job);
        const int delay = kBaseBackoffMs << (job.attempt - 1);
        NetworkLog::append(QStringLiteral("↻ %1 %2ms 无响应，%3ms 后重试（第 %4 次）")
                               .arg(method(job.path, job.isPost))
                               .arg(elapsed)
                               .arg(delay)
                               .arg(job.attempt + 1));
        QTimer::singleShot(delay, this, [this, job] {
            if (m_jobs.contains(job.id))
                dispatch(job);
        });
        return;
    }

    m_jobs.remove(id);
    const bool looksOffline = netError == QNetworkReply::HostNotFoundError
        || netError == QNetworkReply::NetworkSessionFailedError
        || netError == QNetworkReply::ConnectionRefusedError;
    NetworkLog::append(QStringLiteral("✗ %1 %2 %3ms")
                           .arg(looksOffline ? QStringLiteral("offline")
                                             : QStringLiteral("network"))
                           .arg(method(job.path, job.isPost))
                           .arg(elapsed));
    emit failed(id, looksOffline ? QStringLiteral("offline") : QStringLiteral("network"));
}
