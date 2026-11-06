#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrlQuery>

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
