#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

// 网络交换日志（环形缓冲）。
//
// 为什么要有它：后端联调时"到底发出去了没有、卡在哪一步"从界面上看不出来，
// 只能连 adb 抓 logcat。这里把每次**真实交换**记成一行文本，管理页直接显示：
//
//   12:31:04  → POST /pair
//   12:31:04  ← 200 /pair 138ms
//   12:31:09  → POST /events (第 2 次)
//   12:31:24  ↻ 超时，2s 后重试 (第 2 次)
//   12:31:26  ✗ offline /events 15021ms
//
// 上限 80 条，先进先出；进程内共享，不落盘（重启即清空，避免旧日志误导）。
namespace NetworkLog {

void append(const QString &line);
QStringList recent();
void clear();

} // namespace NetworkLog

// HTTP 传输层接口（B2 实现）。
//
// 这是整个仓库里唯一允许出现 QNetworkAccessManager / QNetworkRequest 的地方。
// 上层（SyncVM / 同步队列）只认 "path + JSON body → requestId → reply"，
// 因此单测可以把实现换成假实现，不联网也能跑。
//
// 请求/响应形状见 docs/端上接口规范_v1.1.md 第 4 节（与 agent/接口冻结清单.md 对齐）。
class HttpClient : public QObject
{
    Q_OBJECT

public:
    explicit HttpClient(QObject *parent = nullptr) : QObject(parent) {}
    ~HttpClient() override = default;

    virtual bool isOnline() const = 0;

    // 返回 requestId（>0），完成时发 reply 或 failed。
    virtual quint64 postJson(const QString &path, const QJsonObject &body) = 0;
    virtual quint64 getJson(const QString &path, const QUrlQuery &query) = 0;

signals:
    // 合约：reply/failed 不得在 postJson/getJson 的调用栈内**同步**发出
    // （上层在拿到返回的 requestId 之后才开始认领回执）。实现要么天然
    // 异步（QNetworkReply），要么用队列投递延迟一拍。
    void reply(quint64 requestId, int status, const QJsonObject &body);
    void failed(quint64 requestId, const QString &code);
};

// demo 用占位实现：所有请求立刻失败，保证离线也能跑。
class OfflineHttpClient : public HttpClient
{
    Q_OBJECT

public:
    explicit OfflineHttpClient(QObject *parent = nullptr);

    bool isOnline() const override { return false; }
    quint64 postJson(const QString &path, const QJsonObject &body) override;
    quint64 getJson(const QString &path, const QUrlQuery &query) override;

private:
    quint64 m_nextId = 1;
};

// 真实实现：QNetworkAccessManager。
//
// 设计要点：
//   * baseUrl / token / deviceId 全部从 AppSettings 取，所以"在管理后台改服务器地址"
//     不用重连也不用重建客户端，下一次请求自然生效；
//   * 同一个逻辑请求在重试时**复用 requestId**，上层只看到一次 reply/failed，
//     重试对 SyncVM 完全透明（规范 §9：传输层重试不重新生成 id）；
//   * 4xx 不重试（除了 408/429），409 原样交给上层做冲突回滚（规范 §9）；
//   * 没网时立刻发 failed(id, "offline")，不等 15 秒超时。
class NetworkHttpClient : public HttpClient
{
    Q_OBJECT

public:
    explicit NetworkHttpClient(QObject *parent = nullptr);
    ~NetworkHttpClient() override;

    bool isOnline() const override;
    quint64 postJson(const QString &path, const QJsonObject &body) override;
    quint64 getJson(const QString &path, const QUrlQuery &query) override;

    // 配对成功后由 DeviceVM 写入；同时落盘，重启后仍然带着
    void setToken(const QString &token);
    QString token() const;

private:
    struct Job {
        quint64 id = 0;
        QString path;
        QUrlQuery query;
        QByteArray body;
        bool isPost = true;
        int attempt = 0;
    };

    quint64 enqueue(const Job &job);
    void dispatch(const Job &job);
    void onFinished(QNetworkReply *reply);
    QString absoluteUrl(const QString &path) const;

    QNetworkAccessManager *m_net = nullptr;
    QHash<quint64, Job> m_jobs;              // requestId → 还没结束的请求
    QHash<QNetworkReply *, quint64> m_replyIds;
    QHash<quint64, qint64> m_startedAt;      // requestId → 本次 dispatch 的毫秒时刻
    quint64 m_nextId = 1;
};
