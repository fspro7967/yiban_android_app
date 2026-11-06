#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// 同步 facade（B1 实现同步队列，B2 实现 HTTP 传输）。
// demo 固定 offline：pendingCount 是端上已记录、还没上传的事件数，
// retrySync 目前只回报错误，等 /v1/events 接上后改成真正的重放。
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

signals:
    void syncChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);
};
