#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// 记录 facade（B1 落库，B2 提供本地持久化路径）。
// 所有写操作返回新记录 id，UI 用它做撤销。
class RecordVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList records READ records NOTIFY recordsChanged)
    // 统计依赖今天的 occurrence（确认/新增/删除都会变），单独用 statsChanged 通知。
    Q_PROPERTY(QVariantList stats7 READ stats7 NOTIFY statsChanged)
    Q_PROPERTY(QVariantList stats30 READ stats30 NOTIFY statsChanged)
    Q_PROPERTY(int waterCups READ waterCups NOTIFY recordsChanged)
    Q_PROPERTY(int waterGoal READ waterGoal NOTIFY recordsChanged)
    Q_PROPERTY(QString sleepStart READ sleepStart NOTIFY recordsChanged)
    Q_PROPERTY(QString sleepEnd READ sleepEnd NOTIFY recordsChanged)
    Q_PROPERTY(int sleepMinutes READ sleepMinutes NOTIFY recordsChanged)
    Q_PROPERTY(int activityMinutes READ activityMinutes NOTIFY recordsChanged)

public:
    explicit RecordVM(QObject *parent = nullptr);

    QVariantList records() const;
    QVariantList stats7() const;
    QVariantList stats30() const;
    int waterCups() const;
    int waterGoal() const;
    QString sleepStart() const;
    QString sleepEnd() const;
    int sleepMinutes() const;
    int activityMinutes() const;

    Q_INVOKABLE qint64 addWater();
    Q_INVOKABLE qint64 markSleepStart();
    Q_INVOKABLE qint64 markSleepEnd();
    Q_INVOKABLE qint64 addActivity(const QString &kind); // 散步 / 家务 / 其他
    Q_INVOKABLE qint64 addRecord(const QString &type, double value, const QString &unit);
    Q_INVOKABLE bool correctRecord(qint64 recordId, const QVariantMap &patch);
    Q_INVOKABLE bool removeRecord(qint64 recordId);

signals:
    void recordsChanged();
    void statsChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);
};
