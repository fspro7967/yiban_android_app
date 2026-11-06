#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// 提醒 facade（F1 只依赖这一个接口；B1 负责实现）。
// 数据形状见 docs/端上接口规范_v1.1.md。
class ReminderVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList today READ today NOTIFY todayChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(int todayDone READ todayDone NOTIFY todayChanged)
    Q_PROPERTY(int todayTotal READ todayTotal NOTIFY todayChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY todayChanged)
    Q_PROPERTY(int snoozeRemaining READ snoozeRemaining NOTIFY currentChanged)
    Q_PROPERTY(qint64 nowMs READ nowMs NOTIFY nowChanged)
    Q_PROPERTY(bool autoReminders READ autoReminders WRITE setAutoReminders
                   NOTIFY autoRemindersChanged)

public:
    explicit ReminderVM(QObject *parent = nullptr);

    QVariantList today() const;
    QVariantMap current() const;
    int todayDone() const;
    int todayTotal() const;
    int pendingCount() const;
    int snoozeRemaining() const;
    qint64 nowMs() const;
    bool autoReminders() const;
    void setAutoReminders(bool on);

    // 只发意图，不返回状态；结果通过 today/current 的变化与 commandRejected 通知。
    Q_INVOKABLE void confirm(qint64 occurrenceId);
    Q_INVOKABLE void snooze(qint64 occurrenceId);
    Q_INVOKABLE void undo(qint64 occurrenceId);
    Q_INVOKABLE void focus(qint64 occurrenceId);
    Q_INVOKABLE void escalate(qint64 occurrenceId);
    Q_INVOKABLE void sos();
    Q_INVOKABLE void triggerDemoReminder(); // 仅演示用：模拟一次到点

signals:
    void todayChanged();
    void currentChanged();
    void nowChanged();
    void autoRemindersChanged();
    void reminderDue(const QVariantMap &occurrence);
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    qint64 m_focusedId = 0;
};
