#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class HttpClient;
class NetworkHttpClient;

// 同步 facade（B2）：本地事件队列上传 + 服务端状态/计划增量拉取。
//
// 线路（规范 §8/§9）：
//   * 上传：AppCore.eventsChanged 触发 POST /v1/events，失败指数退避
//     （1s/2s/4s/…上限 5 分钟），retrySync() 立即重试；
//   * 拉取：GET /v1/state 每 15s、GET /v1/plans 每 60s，都带 since= 游标；
//   * 游标与上次同步时间落盘（AppSettings），重启接着传；
//   * 冲突以服务端为准：ack/下发直接进 AppCore 合并，端上终态不被回退。
//
// syncState：offline（未配对/断网）/ syncing / idle / error（规范 §4.4）。
// QML 只读；管理页显示状态 + 待上传条数 + 手动重试按钮。
class SyncVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString syncState READ syncState NOTIFY syncChanged)
    Q_PROPERTY(qint64 lastSyncTs READ lastSyncTs NOTIFY syncChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY syncChanged)

public:
    explicit SyncVM(QObject *parent = nullptr);

    QString syncState() const;
    qint64 lastSyncTs() const;
    int pendingCount() const;

    Q_INVOKABLE void retrySync();
    Q_INVOKABLE QString eventLogCsv() const; // 调试：导出待同步事件

    // 单测注入假客户端用（HttpClient 是抽象接口）。接管所有权。
    void setHttpClient(HttpClient *client);

signals:
    void syncChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    // 一个同步周期：先清事件队列，再增量拉状态与计划。
    void cycle();
    void uploadPending();
    void pullState();
    void pullPlans();
    void scheduleUploadRetry();
    bool canSync() const;                 // 已配对 + 有服务器地址
    void onReply(quint64 requestId, int status, const QJsonObject &body);
    void onFailed(quint64 requestId, const QString &code);
    void noteSuccess();
    QString computeState() const;

    HttpClient *m_http = nullptr;         // 测试时换成假实现
    NetworkHttpClient *m_network = nullptr;

    QTimer m_stateTimer;                  // 15s 拉 /state
    QTimer m_planTimer;                   // 60s 拉 /plans
    QTimer m_uploadBackoff;               // 上传失败退避

    quint64 m_uploadReq = 0;              // 在途请求 id（0 = 无）
    quint64 m_alarmReq = 0;               // 呼救告警上传（occurrenceId=0 的 SOS 走 /alarm）
    QString m_alarmEventId;               // 在途告警对应的本地事件 id
    quint64 m_stateReq = 0;
    quint64 m_plansReq = 0;
    int m_backoffMs = 0;                  // 下次退避时长；0 = 上传正常
    bool m_failed = false;                // 最近一轮是否出错（error 态）
};
