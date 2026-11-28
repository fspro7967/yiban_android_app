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
    Q_PROPERTY(QVariantMap next READ next NOTIFY currentChanged)
    Q_PROPERTY(QVariantList upcoming READ upcoming NOTIFY currentChanged)
    Q_PROPERTY(int todayDone READ todayDone NOTIFY todayChanged)
    Q_PROPERTY(int todayTotal READ todayTotal NOTIFY todayChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY todayChanged)
    Q_PROPERTY(int snoozeRemaining READ snoozeRemaining NOTIFY currentChanged)
    Q_PROPERTY(qint64 nowMs READ nowMs NOTIFY nowChanged)
    Q_PROPERTY(bool autoReminders READ autoReminders WRITE setAutoReminders
                   NOTIFY autoRemindersChanged)
    // I5（一部分）：暂停模式。暂停期间到点的提醒不弹、不升级、不算漏服。
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(qint64 pauseUntilMs READ pauseUntilMs NOTIFY pausedChanged)
    Q_PROPERTY(QString pauseUntilText READ pauseUntilText NOTIFY pausedChanged)

public:
    explicit ReminderVM(QObject *parent = nullptr);

    QVariantList today() const;
    QVariantMap current() const;
    QVariantMap next() const;
    QVariantList upcoming() const;
    int todayDone() const;
    int todayTotal() const;
    int pendingCount() const;
    int snoozeRemaining() const;
    qint64 nowMs() const;
    bool autoReminders() const;
    void setAutoReminders(bool on);

    // ---- 暂停模式（R1-7）----
    bool paused() const;
    qint64 pauseUntilMs() const;
    QString pauseUntilText() const;      // "11-28 08:00"，未暂停为空
    Q_INVOKABLE bool pauseUntil(qint64 untilMs);
    Q_INVOKABLE bool pauseForMinutes(int minutes);
    Q_INVOKABLE bool resumeReminders();

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
    // 升级链（I2）：level 0=未到点，1=已提醒，2=语音加强，3=通知家人。
    // QML 用它替换 ReminderScreen 里写死的 120 秒计时器。
    void escalationChanged(qint64 occurrenceId, int level);
    void pausedChanged();
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    qint64 m_focusedId = 0;
};
