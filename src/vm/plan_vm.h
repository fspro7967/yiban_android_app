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

    // plan: {title, dose, note, rule, windowMin?, escalateAfterMin?, allowLate?}
    //   allowLate（默认 true）：错过后是否还允许点"完成"补做；随餐药等对时间严格的设 false。
    Q_INVOKABLE bool createPlan(const QVariantMap &plan);
    Q_INVOKABLE bool updatePlan(qint64 planId, const QVariantMap &patch);
    Q_INVOKABLE bool deletePlan(qint64 planId);
    // 撤销删除（5 秒撤销条用）：恢复软删除的计划及今天的进度。
    Q_INVOKABLE bool restorePlan(qint64 planId);
    // 周期计划只跳过今天这一次（以后照常提醒）；unskipToday 给撤销条用。
    Q_INVOKABLE bool skipToday(qint64 planId);
    Q_INVOKABLE bool unskipToday(qint64 planId);

signals:
    void plansChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);
};
