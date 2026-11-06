#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// 计划 facade（B1 实现）。QML 只读 plans/todaySummary，写操作全部是命令。
class PlanVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList plans READ plans NOTIFY plansChanged)
    Q_PROPERTY(QVariantMap todaySummary READ todaySummary NOTIFY plansChanged)

public:
    explicit PlanVM(QObject *parent = nullptr);

    QVariantList plans() const;
    QVariantMap todaySummary() const;

    // plan: {title, dose, note, rule, windowMin?, escalateAfterMin?}
    Q_INVOKABLE bool createPlan(const QVariantMap &plan);
    Q_INVOKABLE bool updatePlan(qint64 planId, const QVariantMap &patch);
    Q_INVOKABLE bool deletePlan(qint64 planId);

signals:
    void plansChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);
};
