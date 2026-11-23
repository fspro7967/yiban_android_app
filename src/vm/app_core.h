#pragma once

#include <QDate>
#include <QHash>
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
    QVariantMap nextUpcomingOccurrence() const; // 今天结束后，下一条未来提醒（跨天）
    QVariantList upcomingOccurrences() const;   // 下一个有提醒的日期的完整清单
    QVariantList plans() const;
    QVariantList records() const;
    QVariantList stats(int days) const;
    QVariantList eventLog() const;
    QString eventLogCsv() const;

    // ---- 同步队列（B2 联调；B1 换 SQLite 实现时保持这三条语义）----
    // 待上传事件：serverSeq == 0 的（已上传的 >0）。
    QVariantList pendingEvents() const;
    // 按 /v1/events 的 ack 回写：[{eventId, serverSeq}]，重复 ack 幂等。
    void markEventsSynced(const QVariantList &acked);
    // 服务端明确拒绝的事件（如过时状态）标记过期（serverSeq = -1）：
    // 不再重传，但留在日志里可导出排查。
    void expireEvents(const QStringList &eventIds);

    // ---- 记录上传队列（规范 §8：POST / PATCH /v1/records，不进事件流）----
    // 每条记录自带同步字段：
    //   serverRecordId：0 = 还没上传，>0 = 服务端 record_id，-1 = 服务端拒收（不再重传）
    //   syncRev / syncedRev：本地每修正一次 syncRev+1；服务端已拿到的版本记在 syncedRev
    // 待上传 = serverRecordId == 0（要 POST），或 >0 且 syncRev > syncedRev（要 PATCH）。
    // 演示数据域一律不上传：演示记录不能混进真实家庭的服务端数据。
    // 返回的每项另带 clientId：按 recordId + at 派生的稳定 UUID，POST 重试时服务端据此去重。
    QVariantList pendingRecords() const;
    int pendingRecordCount() const;
    // 服务端已收到 rev 版本：首次 POST 回执带上 serverRecordId；PATCH 回执传 0 沿用原值。
    void markRecordSynced(qint64 recordId, qint64 serverRecordId, qint64 rev);
    // 服务端 4xx 明确拒收：标 -1，不再重传。
    void rejectRecord(qint64 recordId);
    // PATCH 回 404（服务端没有这条）：清掉 serverRecordId，下一轮重新 POST。
    void forgetServerRecord(qint64 recordId);
    // 服务端下发（snake→camel 已由 SyncVM 转好）：按 planId 合并，
    // 版本 >= 本地才接受（规范 §9：冲突以服务端为准）。
    void applyServerPlans(const QVariantList &serverPlans);
    // 按 occurrenceId 合并服务端状态；本地已终态（confirmed/escalated）不被回退。
    void applyServerOccurrences(const QVariantList &serverOccurrences);

    // ---- 操作日志（管理后台查看；和上面的同步事件队列是两回事）----
    // 每项 {seq, ts, category, text}，新的在前。只留最近 kMaxOperationLog 条，
    // 不分真实/演示数据域：切换数据域本身也是一条操作。
    QVariantList operationLog() const;
    int operationLogSize() const { return m_opLog.size(); }
    void logOperation(const QString &category, const QString &text);
    void clearOperationLog();

    int todayDone() const;
    int todayTotal() const;
    int pendingCount() const;
    qint64 nowMs() const { return m_nowMs; }

    bool autoReminders() const { return m_autoReminders; }
    void setAutoReminders(bool on);

    // 演示模式：真实数据与演示数据各自独立保存，切换只换当前数据域。
    bool demoMode() const { return m_demoMode; }
    void setDemoMode(bool on);

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
    // 撤销删除：软删除的计划恢复原样，今天已确认/推迟的进度一并恢复。
    bool restorePlan(qint64 planId, QString *error = nullptr);
    // 周期计划"只删今天这一次"：今天不再提醒，以后照常；撤销时恢复今天的进度。
    bool skipToday(qint64 planId, QString *error = nullptr);
    bool unskipToday(qint64 planId, QString *error = nullptr);

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

    // 清空本地数据库并回到"新装"状态：真实域清空，演示域重建。
    // 给单测隔离用，也可作为管理后台"清除本机数据"的落点。
    void resetStorage();

    // ---- 测试辅助：可注入时钟（改时间/跨天/升级链的用例用）----
    // 传 0 恢复系统时间。只影响本类的"现在"和"今天"，不碰系统时钟。
    void setClockForTests(qint64 nowMs);
    // 立刻跑一次 tick（等价于定时器到点），便于用例推进时间。
    void tickForTests();

signals:
    void occurrencesChanged();
    void currentChanged();
    void plansChanged();
    void recordsChanged();
    void statsChanged();
    void profileChanged();
    void nowChanged();
    void reminderDue(const QVariantMap &occurrence);
    void demoModeChanged();
    void operationLogChanged();
    void toast(const QString &text);
    // 升级等级变化（I2：0=未到点，1=已提醒，2=语音加强，3=通知家人）。
    void escalationChanged(qint64 occurrenceId, int level);
    // 事件日志变化（新事件入队 / 事件被 ack）：SyncVM 据此触发上传
    void eventsChanged();

private slots:
    void onTick();

private:
    explicit AppCore(QObject *parent = nullptr);
    Q_DISABLE_COPY(AppCore)

    struct DataSnapshot {
        QVariantList plans;
        QVariantList occurrences;
        QVariantList records;
        QVariantList events;
        qint64 nextPlanId = 1;
        qint64 nextRecordId = 1;
        qint64 nextEventSeq = 1;
        QString elderName;
        QString caregiverName;
        QString caregiverPhone;
        QDate day;                                  // occurrences 所属日期
        QDate startDate;                            // 真实数据域：开始使用的日期
        QVariantMap history;                        // "yyyy-MM-dd" → {total, done, missed}
        QHash<qint64, QVariantList> deletedOccurrences; // planId → 删除前今天的 occurrence
        QHash<qint64, QVariantList> skippedOccurrences; // planId → 跳过前今天的 occurrence
    };

    void seedDemo();
    DataSnapshot snapshot() const;
    void applySnapshot(const DataSnapshot &data);
    void clearActive();
    void rebuildToday();
    void rebuildTodayFrom(const QVariantList &source);
    // 把某天 occurrence 的完成情况归档进 m_history（跨天或切换数据域时调用）。
    void archiveDay(const QDate &day, const QVariantList &occurrences);
    static QVariantMap tally(const QVariantList &occurrences, bool dayClosed);
    QVariantList occurrencesForDay(const QDate &day) const;
    void fireDue(qint64 occurrenceId);
    void appendEvent(qint64 occurrenceId, const QString &action, const QString &reason);

    // ---- 时间源：默认系统时钟，用例可注入 ----
    qint64 clockNowMs() const;
    QDate clockToday() const;
    // 把一条 notified 的提醒按"提醒已过多长时间"推进升级等级；返回新等级。
    int advanceEscalation(int index);
    // 给发生记录补上升级等级与"下一级还有多少秒"（I2 对外字段）。
    QVariantMap withEscalationDeadline(QVariantMap occ) const;

    // ---- 本地持久化（SQLite）----
    // 只落真实数据域的整份快照；演示域仍是内存快照（重启即重置，符合 README 的约定）。
    void openDatabase();
    void ensureSchema();
    void schedulePersist();                 // 合并同一轮事件循环里的多次写入
    void persistNow();
    void clearDatabase();
    bool loadRealData(DataSnapshot *data);

    bool m_dbReady = false;
    bool m_persistPending = false;
    // 0 = 用系统时间；>0 = 用例注入的"现在"（毫秒时间戳）。
    qint64 m_clockOverrideMs = 0;
    QString m_dbPath;

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
    QVariantList m_opLog; // 旧的在前；对外输出时倒过来
    qint64 m_nextOpSeq = 1;

    QTimer m_tick;
    QDate m_day;
    qint64 m_nowMs = 0;
    qint64 m_nextPlanId = 1;
    qint64 m_nextRecordId = 1;
    qint64 m_nextEventSeq = 1;

    bool m_autoReminders = true;
    bool m_demoMode = false;
    DataSnapshot m_realData;
    DataSnapshot m_demoData;

    QString m_elderName;
    QString m_caregiverName;
    QString m_caregiverPhone;

    QDate m_startDate;
    QVariantMap m_history;
    QHash<qint64, QVariantList> m_deletedOccurrences;
    QHash<qint64, QVariantList> m_skippedOccurrences;
};
