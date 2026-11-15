#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// 操作日志 facade：管理后台按时间倒序展示端上发生过的每一步操作。
// 日志本体在 AppCore（各 VM 的命令成功后各自记一条）；这里只读出来，
// 另外给 QML 留一个 note()，记录不经过 VM 的页面级操作（进出管理后台等）。
// 演示版只保存在内存里，重启后清空；和同步事件队列（SyncVM）互不影响。
class LogVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // 每项 {seq, ts, category, text}，新的在前。
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)

public:
    explicit LogVM(QObject *parent = nullptr);

    QVariantList entries() const;
    int count() const;

    Q_INVOKABLE void note(const QString &category, const QString &text);
    // 清空后会留下一条"清空了操作日志"。
    Q_INVOKABLE void clear();

signals:
    void entriesChanged();
};
