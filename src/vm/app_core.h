#pragma once

#include <QDate>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// 端上数据核心（demo 实现）。
//
// 这一层是 B1（何奕辰）的落点：SQLite + 状态机 + 同步队列都放在这里，
// 对外的 QVariantMap 形状由 docs/端上接口规范_v1.1.md 冻结。
// 当前实现全部在内存里，只为了让 QML 在真实后端接入前仍然可跑；
// 后端替换本文件的内部实现时，facade（ReminderVM 等）不需要改动。
//
// 约定：
//   * 时间一律 int64 毫秒时间戳；
//   * occurrenceId / planId / recordId 为正整数，< 2^53（QML double 安全）；
//   * 状态只有 pending / notified / confirmed / snoozed / missed / escalated；
//   * 只有本类能改状态，QML 不允许直接写 state。
class AppCore : public QObject
{
    Q_OBJECT

public:
    static AppCore *instance();

    // ---- 读取（VM 用，不直接暴露给 QML）----
    QVariantList occurrencesToday() const;      // 已按 dueTs 排序、含派生字段
    QVariantMap currentOccurrence() const;      // 首页"下一条"，空 map 表示全部完成
    QVariantList plans() const;
    QVariantList records() const;
    QVariantList stats(int days) const;
    QVariantList eventLog() const;
    QString eventLogCsv() const;

    int todayDone() const;
    int todayTotal() const;
    int pendingCount() const;
    qint64 nowMs() const { return m_nowMs; }

    bool autoReminders() const { return m_autoReminders; }
    void setAutoReminders(bool on);

    // ---- 提醒动作（事件溯源入口）----
    bool confirm(qint64 occurrenceId, QString *error = nullptr);
    bool snooze(qint64 occurrenceId, QString *error = nullptr);
    bool undoOccurrence(qint64 occurrenceId, QString *error = nullptr);
    bool escalate(qint64 occurrenceId, QString *error = nullptr);
    void triggerDemoReminder();
    void sos();

    // ---- 计划 ----
    qint64 createPlan(const QVariantMap &plan, QString *error = nullptr);
    bool updatePlan(qint64 planId, const QVariantMap &plan, QString *error = nullptr);
    bool deletePlan(qint64 planId, QString *error = nullptr);

    // ---- 记录 ----
    qint64 addRecord(const QString &type, double value, const QString &unit);
    bool correctRecord(qint64 recordId, const QVariantMap &patch, QString *error = nullptr);
    bool removeRecord(qint64 recordId);
    int waterCups() const;
    int waterGoal() const { return 8; }
    QString sleepStartText() const;
    QString sleepEndText() const;
    int sleepMinutes() const;
    int activityMinutes() const;

    // ---- 档案（配对接口返回后由 B2 写入）----
    QString elderName() const { return m_elderName; }
    QString caregiverName() const { return m_caregiverName; }
    QString caregiverPhone() const { return m_caregiverPhone; }
    void setProfile(const QString &elderName, const QString &caregiverName,
                    const QString &caregiverPhone);

    void resetDemo();

signals:
    void occurrencesChanged();
    void currentChanged();
    void plansChanged();
    void recordsChanged();
    void profileChanged();
    void nowChanged();
    void reminderDue(const QVariantMap &occurrence);
    void toast(const QString &text);

private slots:
    void onTick();

private:
    explicit AppCore(QObject *parent = nullptr);
    Q_DISABLE_COPY(AppCore)

    void seedDemo();
    void rebuildToday();
    void fireDue(qint64 occurrenceId);
    void appendEvent(qint64 occurrenceId, const QString &action, const QString &reason);

    int indexOfOccurrence(qint64 id) const;
    int indexOfPlan(qint64 id) const;
    QVariantMap occurrenceById(qint64 id) const;

    static bool planFiresOn(const QVariantMap &plan, const QDate &day);
    static QVariantMap publicOccurrence(const QVariantMap &plan, const QVariantMap &occ);
    static QString repeatTextFor(const QVariantMap &rule);
    static QList<int> dueMinutesFor(const QVariantMap &rule, const QDate &day);

    QVariantList m_plans;
    QVariantList m_occurrences;
    QVariantList m_records;
    QVariantList m_events;

    QTimer m_tick;
    QDate m_day;
    qint64 m_nowMs = 0;
    qint64 m_nextPlanId = 1;
    qint64 m_nextRecordId = 1;
    qint64 m_nextEventSeq = 1;

    bool m_autoReminders = true;

    QString m_elderName;
    QString m_caregiverName;
    QString m_caregiverPhone;
};
