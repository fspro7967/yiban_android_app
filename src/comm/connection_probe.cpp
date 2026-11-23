#include "connection_probe.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

ConnectionProbe::ConnectionProbe(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        conclude(false, 0, QStringLiteral("timeout"),
                 QStringLiteral("连接超时：%1 秒内服务器没有回应，请检查地址和网络")
                     .arg(qRound(m_timeout.interval() / 1000.0)));
    });
}

ConnectionProbe::~ConnectionProbe()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

bool ConnectionProbe::start(const QUrl &url, int timeoutMs)
{
    if (busy())
        return false;
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest request(url);
    // 不跟随跳转：3xx 本身就说明服务器在，而且要让看护人知道地址被跳走了。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YibanElder/connection-test"));

    m_clock.start();
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, &ConnectionProbe::onHeaders);
    connect(m_reply, &QNetworkReply::finished, this, &ConnectionProbe::onFinished);
    m_timeout.start(timeoutMs);
    emit busyChanged();
    return true;
}

void ConnectionProbe::cancel()
{
    if (!busy())
        return;
    m_timeout.stop();
    QNetworkReply *reply = m_reply;
    m_reply.clear();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    emit busyChanged();
}

void ConnectionProbe::onHeaders()
{
    if (!m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status <= 0)
        return;

    if (status >= 500) {
        conclude(false, status, QStringLiteral("server_error"),
                 QStringLiteral("连上了服务器，但它自己出错了（HTTP %1），请稍后再试").arg(status));
    } else if (status == 401 || status == 403) {
        conclude(true, status, QStringLiteral("auth_required"),
                 QStringLiteral("连接正常：服务器有回应，接口要登录后才能访问（HTTP %1）").arg(status));
    } else if (status == 404) {
        conclude(true, status, QStringLiteral("not_found"),
                 QStringLiteral("能连上服务器，但这个地址下没有内容（HTTP 404），请核对地址后半段"));
    } else if (status >= 400) {
        conclude(true, status, QStringLiteral("client_error"),
                 QStringLiteral("能连上服务器，它没有接受这次访问（HTTP %1）").arg(status));
    } else if (status >= 300) {
        conclude(true, status, QStringLiteral("redirect"),
                 QStringLiteral("能连上服务器，它要求跳转到别的地址（HTTP %1），请确认地址写全了").arg(status));
    } else {
        conclude(true, status, QStringLiteral("ok"),
                 QStringLiteral("连接正常：服务器有回应（HTTP %1）").arg(status));
    }
}

void ConnectionProbe::onFinished()
{
    if (!m_reply)
        return;
    // 有状态码的情况已经在 onHeaders 里结束了；走到这里说明没拿到任何 HTTP 响应。
    QString code = QStringLiteral("network_error");
    QString message;
    switch (m_reply->error()) {
    case QNetworkReply::HostNotFoundError:
        code = QStringLiteral("host_not_found");
        message = QStringLiteral("找不到这台服务器，请检查地址有没有写错，或者手机有没有联网");
        break;
    case QNetworkReply::ConnectionRefusedError:
        code = QStringLiteral("refused");
        message = QStringLiteral("服务器拒绝连接，可能是端口不对或服务没有启动");
        break;
    case QNetworkReply::TimeoutError:
        code = QStringLiteral("timeout");
        message = QStringLiteral("连接超时，服务器没有回应，请检查地址和网络");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        code = QStringLiteral("tls");
        message = QStringLiteral("安全连接没有建立：服务器证书有问题，或这台手机不支持 https（%1）")
                      .arg(m_reply->errorString());
        break;
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        // Qt 把"没有路由 / 网卡没开 / TLS 后端起不来"都归到这里，原文留着方便排查。
        code = QStringLiteral("no_network");
        message = QStringLiteral("连不上网络，请检查手机的 Wi-Fi 或流量（%1）").arg(m_reply->errorString());
        break;
    default:
        message = QStringLiteral("连接失败：%1").arg(m_reply->errorString());
        break;
    }
    conclude(false, 0, code, message);
}

void ConnectionProbe::conclude(bool reachable, int httpStatus, const QString &code,
                               const QString &message)
{
    if (!m_reply)
        return;
    const int elapsed = int(m_clock.elapsed());
    m_timeout.stop();
    // 先摘掉 m_reply 再 abort：abort 会同步触发 finished，不能让它再进一次 conclude。
    QNetworkReply *reply = m_reply;
    m_reply.clear();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    emit busyChanged();
    emit finished(reachable, httpStatus, elapsed, code, message);
}
