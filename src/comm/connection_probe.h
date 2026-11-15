#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// 连通性探测（管理后台"测试连接"用）。
//
// 对给定地址发一次 GET，只等到响应头就结束：能拿到任何 HTTP 状态码都说明
// "这台手机能连到那台服务器"，不依赖服务端提供专门的健康检查接口，也不带任何业务数据。
// 和 HttpClient 一样属于网络出口，所以放在 src/comm；上层只认 finished 信号。
class ConnectionProbe : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionProbe(QObject *parent = nullptr);
    ~ConnectionProbe() override;

    bool busy() const { return !m_reply.isNull(); }

    // 开始探测；上一次还没结束时返回 false。结果一定通过 finished 回来（最晚 timeoutMs 之后）。
    bool start(const QUrl &url, int timeoutMs = 8000);
    void cancel();

signals:
    void busyChanged();
    // reachable：服务器有正常响应（2xx–4xx）。httpStatus 没拿到响应时为 0。
    // code 是稳定的机器码：ok / redirect / auth_required / not_found / client_error /
    // server_error / host_not_found / refused / timeout / tls / no_network / network_error；
    // message 是给看护人看的一句话。
    void finished(bool reachable, int httpStatus, int elapsedMs,
                  const QString &code, const QString &message);

private:
    void onHeaders();
    void onFinished();
    void conclude(bool reachable, int httpStatus, const QString &code, const QString &message);

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QElapsedTimer m_clock;
};
