#include "app_core.h"

#include <QDateTime>
#include <QTime>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QtMath>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <algorithm>
#include <climits>
#include <utility>

namespace {

constexpr qint64 kOccurrenceIdBase = 1000000000LL;
// 本机新建、还没上传的计划用这个号段起的 planId：服务端 plan_id 是从 1 自增的，
// 不会追到这里。occurrenceId = planId*1e9 + …，5e6*1e9 = 5e15 < 2^53（QML double 安全）。
constexpr qint64 kLocalPlanIdBase = 5000000LL;
constexpr qint64 kSnoozeMinutes = 10;
constexpr int kMaxSnooze = 3;
constexpr int kDefaultWindowMin = 30;
constexpr int kDefaultEscalateMin = 15;
// 升级链第二级（语音加强）的固定阀值：到点后 2 分钟仍未确认。
constexpr int kEscalationVoiceMin = 2;
// 已 ack / 被拒的事件只保留最近这么多条；待上传（serverSeq == 0）一条都不丢。
constexpr int kMaxSyncedEvents = 800;
// 内存里也设上限，防止长时间运行把 m_events 撑爆。
constexpr int kMaxEventsInMemory = 2000;
constexpr int kMaxOperationLog = 200;

// 本地库连接名（同一个进程里只开一条连接）。
const QString kDbConnection = QStringLiteral("yiban_app_core");

QByteArray variantMapToJson(const QVariantMap &map)
{
    return QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact);
}

QVariantMap jsonToVariantMap(const QByteArray &blob)
{
    return QJsonDocument::fromJson(blob).object().toVariantMap();
}

QByteArray variantListHashToJson(const QHash<qint64, QVariantList> &hash)
{
    QJsonObject object;
    for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
        object.insert(QString::number(it.key()), QJsonArray::fromVariantList(it.value()));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QHash<qint64, QVariantList> jsonToVariantListHash(const QByteArray &blob)
{
    QHash<qint64, QVariantList> hash;
    const QJsonObject object = QJsonDocument::fromJson(blob).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        hash.insert(it.key().toLongLong(), it.value().toArray().toVariantList());
    return hash;
}

// event_id 的命名空间固定，保证同样的 (occurrence, action, ts) 只会产生一个 id。
const QUuid kEventNamespace(QStringLiteral("{7b0f7d3e-3c2a-4a55-9f0e-6b1a2c8d4e11}"));

QString hhmmFromMs(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm"));
}

QString hhmmFromMinutes(int minutes)
{
    const int m = (minutes % (24 * 60) + 24 * 60) % (24 * 60);
    return QStringLiteral("%1:%2")
        .arg(m / 60, 2, 10, QLatin1Char('0'))
        .arg(m % 60, 2, 10, QLatin1Char('0'));
}

int minutesOfTime(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() < 2)
        return -1;
    bool okH = false;
    bool okM = false;
    const int h = parts.at(0).toInt(&okH);
    const int m = parts.at(1).toInt(&okM);
    if (!okH || !okM || h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return h * 60 + m;
}

QString weekdayShort(int dayOfWeek)
{
    static const char *names[] = {"一", "二", "三", "四", "五", "六", "日"};
    return QString::fromUtf8(names[qBound(1, dayOfWeek, 7) - 1]);
}

qint64 midnightMs(const QDate &day)
{
    return QDateTime(day, QTime(0, 0)).toMSecsSinceEpoch();
}

QString dayLabelFor(const QDate &day, const QDate &today)
{
    const int offset = today.daysTo(day);
    if (offset == 1)
        return QStringLiteral("明天");
    if (offset == 2)
        return QStringLiteral("后天");
    return QStringLiteral("%1月%2日 周%3")
        .arg(day.month())
        .arg(day.day())
        .arg(weekdayShort(day.dayOfWeek()));
}

QString normalizeKind(const QVariantMap &rule)
{
    const QString kind = rule.value(QStringLiteral("kind")).toString();
    static const QStringList known = {
        QStringLiteral("daily"),      QStringLiteral("weekly"),
        QStringLiteral("monthly"),    QStringLiteral("interval"),
        QStringLiteral("course"),     QStringLiteral("meal_offset"),
        QStringLiteral("once"),
    };
    return known.contains(kind) ? kind : QString();
}

// 操作日志里的称呼："08:00 吃降压药" / "「吃降压药」" / "喝水 1 杯"。
QString occurrenceLabel(const QVariantMap &occ)
{
    return QStringLiteral("%1 %2").arg(hhmmFromMs(occ.value(QStringLiteral("dueTs")).toLongLong()),
                                       occ.value(QStringLiteral("title")).toString());
}

QString planLabel(const QVariantMap &plan)
{
    return QStringLiteral("「%1」").arg(plan.value(QStringLiteral("title")).toString().trimmed());
}

QString recordLabel(const QString &type, double value, const QString &unit)
{
    const QString amount = QString::number(value) + (unit.isEmpty() ? QString() : QLatin1Char(' ') + unit);
    if (type == QStringLiteral("water"))
        return QStringLiteral("喝水 %1").arg(amount);
    if (type == QStringLiteral("sleep_start"))
        return QStringLiteral("入睡时间");
    if (type == QStringLiteral("sleep_end"))
        return QStringLiteral("起床时间");
    if (type == QStringLiteral("activity"))
        return QStringLiteral("活动 %1").arg(amount);
    return QStringLiteral("%1 %2").arg(type, amount);
}

bool planAllowsLate(const QVariantMap &plan)
{
    // 旧数据没有这个键：默认允许过时后补做。
    return plan.value(QStringLiteral("allowLate"), true).toBool();
}

} // namespace

AppCore::AppCore(QObject *parent)
    : QObject(parent)
{
    m_day = clockToday();
    m_nowMs = QDateTime::currentMSecsSinceEpoch();

    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &AppCore::onTick);
    m_tick.start();

    // 统计里"今天"那一格直接取今天的 occurrence，确认/新增/删除都要让统计重新取值。
    connect(this, &AppCore::occurrencesChanged, this, &AppCore::statsChanged);

    seedDemo();
    m_demoData = snapshot();

    // 真实数据域从本地 SQLite 恢复；没有库（首次安装）就从空数据开始。
    openDatabase();
    ensureSchema();
    DataSnapshot saved;
    if (loadRealData(&saved)) {
        m_realData = saved;
        applySnapshot(m_realData);
    } else {
        clearActive();
    }

    // 真实域的任何变更都落到本地库；演示域是临时快照，不写盘（schedulePersist 里判断）。
    connect(this, &AppCore::occurrencesChanged, this, &AppCore::schedulePersist);
    connect(this, &AppCore::plansChanged, this, &AppCore::schedulePersist);
    connect(this, &AppCore::recordsChanged, this, &AppCore::schedulePersist);
    connect(this, &AppCore::profileChanged, this, &AppCore::schedulePersist);
    // 同步 ack / 拒绝只改事件的 serverSeq，不发上面几个信号；不单独接上的话
    // 重启后已上传的事件会被当成待上传再发一遍。
    connect(this, &AppCore::eventsChanged, this, &AppCore::schedulePersist);
}

AppCore *AppCore::instance()
{
    static AppCore core;
    return &core;
}

void AppCore::seedDemo()
{
    m_plans.clear();
    m_occurrences.clear();
    m_records.clear();
    m_events.clear();
    m_nextPlanId = 1;
    m_nextRecordId = 1;
    m_nextEventSeq = 1;
    m_autoReminders = true;
    m_history.clear();
    m_deletedOccurrences.clear();
    m_skippedOccurrences.clear();

    m_elderName = QStringLiteral("陈奶奶");
    m_caregiverName = QStringLiteral("女儿 陈晓");
    m_caregiverPhone = QStringLiteral("138 0000 5678");

    struct SeedPlan {
        const char *title;
        const char *dose;
        const char *note;
        const char *time;
        bool allowLate;
    };
    const SeedPlan seeds[] = {
        {"晨起喝水", "温水 1 杯", "小口慢慢喝", "07:00", true},
        {"吃降压药", "苯磺酸氨氯地平 1 片", "早饭后，温水送服", "08:00", true},
        // 随餐药：错过了就不能补吃，过时后不允许再点完成。
        {"吃降糖药", "二甲双胍 1 片", "随午餐服用", "12:00", false},
        {"测血压", "坐稳后测量，记录数值", "测量前安静休息 5 分钟", "18:00", true},
    };
    for (const SeedPlan &seed : seeds) {
        QVariantMap rule;
        rule[QStringLiteral("kind")] = QStringLiteral("daily");
        rule[QStringLiteral("times")] = QStringList{QString::fromUtf8(seed.time)};

        QVariantMap plan;
        plan[QStringLiteral("planId")] = m_nextPlanId++;
        plan[QStringLiteral("title")] = QString::fromUtf8(seed.title);
        plan[QStringLiteral("dose")] = QString::fromUtf8(seed.dose);
        plan[QStringLiteral("note")] = QString::fromUtf8(seed.note);
        plan[QStringLiteral("rule")] = rule;
        plan[QStringLiteral("windowMin")] = kDefaultWindowMin;
        plan[QStringLiteral("escalateAfterMin")] = kDefaultEscalateMin;
        plan[QStringLiteral("allowLate")] = seed.allowLate;
        plan[QStringLiteral("version")] = 1;
        plan[QStringLiteral("deleted")] = false;
        m_plans.append(plan);
    }

    rebuildToday();

    // 演示数据：最早的两条（07:00 喝水、08:00 降压药）已经完成，其余保持待办。
    for (int i = 0; i < m_occurrences.size(); ++i) {
        const QVariantMap occ = m_occurrences.at(i).toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() > 2)
            continue;
        QVariantMap updated = occ;
        updated[QStringLiteral("state")] = QStringLiteral("confirmed");
        m_occurrences[i] = updated;
    }

    // 记录：3 杯水、昨晚 22:40 入睡今早 06:30 起床、散步 15 + 家务 10 分钟。
    const qint64 today0 = midnightMs(m_day);
    for (int i = 0; i < 3; ++i) {
        QVariantMap rec;
        rec[QStringLiteral("recordId")] = m_nextRecordId++;
        rec[QStringLiteral("type")] = QStringLiteral("water");
        rec[QStringLiteral("value")] = 1;
        rec[QStringLiteral("unit")] = QStringLiteral("杯");
        rec[QStringLiteral("at")] = today0 + (8 * 60 + i * 90) * 60 * 1000;
        rec[QStringLiteral("correctedFrom")] = 0;
        m_records.append(rec);
    }
    {
        QVariantMap start;
        start[QStringLiteral("recordId")] = m_nextRecordId++;
        start[QStringLiteral("type")] = QStringLiteral("sleep_start");
        start[QStringLiteral("value")] = 0;
        start[QStringLiteral("unit")] = QString();
        start[QStringLiteral("at")] = today0 - (1 * 60 + 20) * 60 * 1000; // 昨天 22:40
        start[QStringLiteral("correctedFrom")] = 0;
        m_records.append(start);

        QVariantMap end;
        end[QStringLiteral("recordId")] = m_nextRecordId++;
        end[QStringLiteral("type")] = QStringLiteral("sleep_end");
        end[QStringLiteral("value")] = 0;
        end[QStringLiteral("unit")] = QString();
        end[QStringLiteral("at")] = today0 + (6 * 60 + 30) * 60 * 1000;
        end[QStringLiteral("correctedFrom")] = 0;
        m_records.append(end);
    }
    for (const auto &activity : {qMakePair(QStringLiteral("散步"), 15.0),
                                 qMakePair(QStringLiteral("家务"), 10.0)}) {
        QVariantMap rec;
        rec[QStringLiteral("recordId")] = m_nextRecordId++;
        rec[QStringLiteral("type")] = QStringLiteral("activity");
        rec[QStringLiteral("value")] = activity.second;
        rec[QStringLiteral("unit")] = QStringLiteral("分钟");
        rec[QStringLiteral("at")] = today0 + 9 * 60 * 60 * 1000;
        rec[QStringLiteral("correctedFrom")] = 0;
        m_records.append(rec);
    }

    emit plansChanged();
    emit occurrencesChanged();
    emit currentChanged();
    emit recordsChanged();
    emit profileChanged();
}

AppCore::DataSnapshot AppCore::snapshot() const
{
    DataSnapshot data;
    data.plans = m_plans;
    data.occurrences = m_occurrences;
    data.records = m_records;
    data.events = m_events;
    data.nextPlanId = m_nextPlanId;
    data.nextRecordId = m_nextRecordId;
    data.nextEventSeq = m_nextEventSeq;
    data.elderName = m_elderName;
    data.caregiverName = m_caregiverName;
    data.caregiverPhone = m_caregiverPhone;
    data.day = m_day;
    data.startDate = m_startDate;
    data.history = m_history;
    data.deletedOccurrences = m_deletedOccurrences;
    data.skippedOccurrences = m_skippedOccurrences;
    return data;
}

void AppCore::clearActive()
{
    m_plans.clear();
    m_occurrences.clear();
    m_records.clear();
    m_events.clear();
    m_nextPlanId = 1;
    m_nextRecordId = 1;
    m_nextEventSeq = 1;
    m_elderName.clear();
    m_caregiverName.clear();
    m_caregiverPhone.clear();
    // 真实数据从第一次进入真实域的这一天开始统计，之前的日期不出现在统计里。
    m_startDate = clockToday();
    m_history.clear();
    m_deletedOccurrences.clear();
    m_skippedOccurrences.clear();
}

void AppCore::applySnapshot(const DataSnapshot &data)
{
    m_plans = data.plans;
    m_records = data.records;
    m_events = data.events;
    m_nextPlanId = data.nextPlanId;
    m_nextRecordId = data.nextRecordId;
    m_nextEventSeq = data.nextEventSeq;
    m_elderName = data.elderName;
    m_caregiverName = data.caregiverName;
    m_caregiverPhone = data.caregiverPhone;
    m_startDate = data.startDate;
    m_history = data.history;
    m_deletedOccurrences = data.deletedOccurrences;
    m_skippedOccurrences = data.skippedOccurrences;

    // 离开这个数据域期间跨了天：先把旧那天的进度归档，再按今天重建。
    if (data.day.isValid() && data.day != m_day) {
        archiveDay(data.day, data.occurrences);
        m_deletedOccurrences.clear();
        m_skippedOccurrences.clear();
    }

    // 从目标数据域的 occurrence 重建今天清单，避免两个域 id 相同导致状态串味。
    m_occurrences.clear();
    rebuildTodayFrom(data.occurrences);

    emit plansChanged();
    emit recordsChanged();
    emit profileChanged();
}

void AppCore::setDemoMode(bool on)
{
    if (m_demoMode == on)
        return;

    if (m_demoMode)
        m_demoData = snapshot();
    else
        m_realData = snapshot();

    m_demoMode = on;
    m_day = clockToday();
    m_nowMs = QDateTime::currentMSecsSinceEpoch();
    applySnapshot(on ? m_demoData : m_realData);

    logOperation(QStringLiteral("设置"),
                 on ? QStringLiteral("切换到演示数据") : QStringLiteral("切换到真实数据"));
    emit demoModeChanged();
}

void AppCore::setProfile(const QString &elderName, const QString &caregiverName,
                         const QString &caregiverPhone)
{
    m_elderName = elderName;
    m_caregiverName = caregiverName;
    m_caregiverPhone = caregiverPhone;
    logOperation(QStringLiteral("设置"),
                 QStringLiteral("保存紧急联系人：%1 %2").arg(caregiverName, caregiverPhone));
    emit profileChanged();
}

void AppCore::onTick()
{
    const QDate today = clockToday();
    if (today != m_day) {
        archiveDay(m_day, m_occurrences);
        m_deletedOccurrences.clear();
        m_skippedOccurrences.clear();
        m_day = today;
        rebuildToday();
    }

    m_nowMs = clockNowMs();
    emit nowChanged();

    bool changed = false;
    QList<qint64> toFire;
    QList<qint64> toMiss;

    const QTime nowTime = QDateTime::fromMSecsSinceEpoch(m_nowMs).time();
    const int nowMinutes = nowTime.hour() * 60 + nowTime.minute();
    for (const QVariant &entry : std::as_const(m_occurrences)) {
        const QVariantMap occ = entry.toMap();
        // 暂停期间到点的提醒：不弹、不升级、不算漏服。
        if (occ.value(QStringLiteral("paused")).toBool())
            continue;
        const QString state = occ.value(QStringLiteral("state")).toString();
        const qint64 id = occ.value(QStringLiteral("occurrenceId")).toLongLong();

        // 需按时完成的提醒：App 没在运行（被杀/挂起/时间被往后调）期间整个有效期已经过去，
        // 回来时直接记为错过，不再弹"该做了"——它已经不能补做了（bug 14）。
        if (!occ.value(QStringLiteral("allowLate"), true).toBool()
            && (state == QStringLiteral("pending") || state == QStringLiteral("snoozed"))) {
            const qint64 until = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong();
            const qint64 base = state == QStringLiteral("snoozed") && until > 0
                ? until
                : occ.value(QStringLiteral("dueTs")).toLongLong();
            if (m_nowMs > base + occ.value(QStringLiteral("windowMin")).toInt() * 60 * 1000) {
                toMiss.append(id);
                continue;
            }
        }

        if (m_autoReminders && state == QStringLiteral("pending")) {
            const int due = minutesOfTime(hhmmFromMs(occ.value(QStringLiteral("dueTs")).toLongLong()));
            if (due >= 0 && due <= nowMinutes)
                toFire.append(id);
        } else if (m_autoReminders && state == QStringLiteral("snoozed")) {
            const qint64 until = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong();
            if (until > 0 && until <= m_nowMs)
                toFire.append(id);
        } else if (state == QStringLiteral("notified")) {
            const qint64 deadline = occ.value(QStringLiteral("dueTs")).toLongLong()
                + occ.value(QStringLiteral("windowMin")).toInt() * 60 * 1000;
            if (m_nowMs > deadline)
                toMiss.append(id);
        } else if (state == QStringLiteral("snoozed")) {
            const qint64 deadline = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong()
                + occ.value(QStringLiteral("windowMin")).toInt() * 60 * 1000;
            if (m_nowMs > deadline)
                toMiss.append(id);
        }
    }

    for (qint64 id : toFire) {
        fireDue(id);
        changed = true;
    }
    for (qint64 id : toMiss) {
        if (toFire.contains(id))
            continue;
        const int index = indexOfOccurrence(id);
        if (index < 0)
            continue;
        QVariantMap occ = m_occurrences.at(index).toMap();
        occ[QStringLiteral("state")] = QStringLiteral("missed");
        m_occurrences[index] = occ;
        appendEvent(id, QStringLiteral("miss"), QStringLiteral("window_expired"));
        logOperation(QStringLiteral("提醒"), QStringLiteral("过时未确认：%1").arg(occurrenceLabel(occ)));
        changed = true;
    }

    // 升级链（R1-3 / I2）：提醒发出后按"已过多长时间"逐级推进，到顶就通知家人。
    QList<qint64> toEscalate;
    for (int i = 0; i < m_occurrences.size(); ++i) {
        const int level = advanceEscalation(i);
        if (level < 3)
            continue;
        const QVariantMap occ = m_occurrences.at(i).toMap();
        if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("notified"))
            toEscalate.append(occ.value(QStringLiteral("occurrenceId")).toLongLong());
    }
    for (qint64 id : toEscalate) {
        if (escalate(id))
            changed = true;
    }

    if (changed) {
        emit occurrencesChanged();
        emit currentChanged();
    }
}

void AppCore::rebuildToday()
{
    rebuildTodayFrom(m_occurrences);
}

QVariantMap AppCore::tally(const QVariantList &occurrences, bool dayClosed)
{
    int total = 0;
    int done = 0;
    int missed = 0;
    int paused = 0;
    for (const QVariant &entry : occurrences) {
        const QVariantMap occ = entry.toMap();
        const QString state = occ.value(QStringLiteral("state")).toString();
        // 暂停的提醒不算进分母，也不算漏服：住院/外出期间不该拉低完成率。
        if (occ.value(QStringLiteral("paused")).toBool()) {
            ++paused;
            continue;
        }
        ++total;
        if (state == QStringLiteral("confirmed"))
            ++done;
        else if (dayClosed || state == QStringLiteral("missed"))
            ++missed; // 一天结束时还没确认的都算漏做
    }
    QVariantMap out;
    out[QStringLiteral("total")] = total;
    out[QStringLiteral("done")] = done;
    out[QStringLiteral("missed")] = missed;
    out[QStringLiteral("paused")] = paused;
    return out;
}

void AppCore::archiveDay(const QDate &day, const QVariantList &occurrences)
{
    if (!day.isValid())
        return;
    m_history[day.toString(Qt::ISODate)] = tally(occurrences, true);
}

void AppCore::rebuildTodayFrom(const QVariantList &source)
{
    // 保留同 id occurrence 的既有状态：新建/删除计划只重排清单，不重置今天的进度。
    QHash<qint64, QVariantMap> previous;
    for (const QVariant &entry : source)
        previous.insert(entry.toMap().value(QStringLiteral("occurrenceId")).toLongLong(),
                        entry.toMap());

    m_occurrences.clear();
    for (const QVariant &entry : std::as_const(m_plans)) {
        const QVariantMap plan = entry.toMap();
        if (plan.value(QStringLiteral("deleted")).toBool())
            continue;
        if (!planFiresOn(plan, m_day))
            continue;

        const QVariantMap rule = plan.value(QStringLiteral("rule")).toMap();
        const QList<int> minutes = dueMinutesFor(rule, m_day);
        for (int seq = 0; seq < minutes.size(); ++seq) {
            const qint64 occurrenceId = plan.value(QStringLiteral("planId")).toLongLong()
                    * kOccurrenceIdBase
                + (m_day.toJulianDay() % 10000000LL) * 100 + seq;
            QVariantMap occ;
            occ[QStringLiteral("occurrenceId")] = occurrenceId;
            occ[QStringLiteral("planId")] = plan.value(QStringLiteral("planId"));
            occ[QStringLiteral("title")] = plan.value(QStringLiteral("title"));
            occ[QStringLiteral("dose")] = plan.value(QStringLiteral("dose"));
            occ[QStringLiteral("note")] = plan.value(QStringLiteral("note"));
            occ[QStringLiteral("dueTs")] = midnightMs(m_day) + minutes.at(seq) * 60 * 1000;
            occ[QStringLiteral("state")] = QStringLiteral("pending");
            occ[QStringLiteral("snoozeCount")] = 0;
            occ[QStringLiteral("snoozeUntilTs")] = 0;
            occ[QStringLiteral("windowMin")] = plan.value(QStringLiteral("windowMin"));
            occ[QStringLiteral("escalateAfterMin")] =
                plan.value(QStringLiteral("escalateAfterMin"));
            occ[QStringLiteral("allowLate")] = planAllowsLate(plan);
            if (const auto it = previous.constFind(occurrenceId); it != previous.constEnd()) {
                occ[QStringLiteral("state")] = it->value(QStringLiteral("state"));
                occ[QStringLiteral("snoozeCount")] = it->value(QStringLiteral("snoozeCount"));
                occ[QStringLiteral("snoozeUntilTs")] = it->value(QStringLiteral("snoozeUntilTs"));
                // 升级链基线也要带过来：启动读库、改计划、同步拉取都走这里，
                // 丢了 notifiedAtTs 就会从"现在"重新计时（被杀再打开又从 2 分钟倒数）。
                occ[QStringLiteral("notifiedAtTs")] = it->value(QStringLiteral("notifiedAtTs"));
                occ[QStringLiteral("escalationLevel")] = it->value(QStringLiteral("escalationLevel"));
                // 暂停标记也要带过来，否则改一次计划就把"已暂停"重新变回待提醒。
                occ[QStringLiteral("paused")] = it->value(QStringLiteral("paused"));
            }
            m_occurrences.append(occ);
        }
    }
    // 暂停期内新建/重排出来的提醒同样要打 paused，不然会照常弹。
    markPausedOccurrences();
    emit occurrencesChanged();
    emit currentChanged();
}

bool AppCore::planFiresOn(const QVariantMap &plan, const QDate &day)
{
    const QVariantMap rule = plan.value(QStringLiteral("rule")).toMap();
    const QString kind = normalizeKind(rule);
    if (kind.isEmpty())
        return false;
    if (plan.value(QStringLiteral("skipDates")).toStringList().contains(day.toString(Qt::ISODate)))
        return false;

    if (kind == QStringLiteral("daily") || kind == QStringLiteral("meal_offset"))
        return true;

    if (kind == QStringLiteral("weekly")) {
        const QVariantList weekdays = rule.value(QStringLiteral("weekdays")).toList();
        return weekdays.contains(day.dayOfWeek());
    }
    if (kind == QStringLiteral("monthly"))
        return day.day() == rule.value(QStringLiteral("day"), 1).toInt();

    if (kind == QStringLiteral("interval")) {
        const QDate start = QDate::fromString(
            rule.value(QStringLiteral("start")).toString(), Qt::ISODate);
        const int n = rule.value(QStringLiteral("n"), 1).toInt();
        if (!start.isValid() || n <= 0 || day < start)
            return false;
        return start.daysTo(day) % n == 0;
    }
    if (kind == QStringLiteral("course")) {
        const QDate start = QDate::fromString(
            rule.value(QStringLiteral("start")).toString(), Qt::ISODate);
        const int days = rule.value(QStringLiteral("days"), 0).toInt();
        if (!start.isValid() || days <= 0)
            return false;
        return start.daysTo(day) >= 0 && start.daysTo(day) < days;
    }
    if (kind == QStringLiteral("once")) {
        const QDateTime at = QDateTime::fromString(
            rule.value(QStringLiteral("at")).toString(), Qt::ISODate);
        return at.isValid() && at.date() == day;
    }
    return false;
}

QList<int> AppCore::dueMinutesFor(const QVariantMap &rule, const QDate &day)
{
    Q_UNUSED(day)
    const QString kind = normalizeKind(rule);
    QList<int> out;

    if (kind == QStringLiteral("once")) {
        const QDateTime at = QDateTime::fromString(
            rule.value(QStringLiteral("at")).toString(), Qt::ISODate);
        if (at.isValid())
            out.append(at.time().hour() * 60 + at.time().minute());
        return out;
    }

    const int offset = rule.value(QStringLiteral("offset_minutes"), 0).toInt();
    QStringList times = rule.value(QStringLiteral("times")).toStringList();
    if (kind == QStringLiteral("meal_offset"))
        times = rule.value(QStringLiteral("anchor")).toStringList();

    for (const QString &text : std::as_const(times)) {
        const int minutes = minutesOfTime(text);
        if (minutes >= 0)
            out.append((minutes + offset + 24 * 60) % (24 * 60));
    }
    std::sort(out.begin(), out.end());
    return out;
}

QString AppCore::repeatTextFor(const QVariantMap &rule)
{
    const QString kind = normalizeKind(rule);
    if (kind == QStringLiteral("daily"))
        return QStringLiteral("每天");
    if (kind == QStringLiteral("weekly")) {
        QStringList names;
        for (const QVariant &day : rule.value(QStringLiteral("weekdays")).toList())
            names.append(QStringLiteral("周") + weekdayShort(day.toInt()));
        return names.isEmpty() ? QStringLiteral("每周") : names.join(QStringLiteral("、"));
    }
    if (kind == QStringLiteral("monthly"))
        return QStringLiteral("每月 %1 日").arg(rule.value(QStringLiteral("day"), 1).toInt());
    if (kind == QStringLiteral("interval"))
        return QStringLiteral("每 %1 天").arg(rule.value(QStringLiteral("n"), 1).toInt());
    if (kind == QStringLiteral("course"))
        return QStringLiteral("连续 %1 天").arg(rule.value(QStringLiteral("days"), 0).toInt());
    if (kind == QStringLiteral("meal_offset")) {
        const int offset = rule.value(QStringLiteral("offset_minutes"), 0).toInt();
        return offset < 0 ? QStringLiteral("饭前 %1 分钟").arg(-offset)
                          : QStringLiteral("饭后 %1 分钟").arg(offset);
    }
    if (kind == QStringLiteral("once"))
        return QStringLiteral("仅一次");
    return QStringLiteral("每天");
}

QVariantMap AppCore::publicOccurrence(const QVariantMap &plan, const QVariantMap &occ)
{
    QVariantMap out = occ;
    const int snoozeCount = occ.value(QStringLiteral("snoozeCount")).toInt();
    const QString state = occ.value(QStringLiteral("state")).toString();
    out[QStringLiteral("repeatText")] = repeatTextFor(plan.value(QStringLiteral("rule")).toMap());
    out[QStringLiteral("snoozeRemaining")] = qMax(0, kMaxSnooze - snoozeCount);
    const bool allowLate = planAllowsLate(plan);
    // 过时 = 已经错过（窗口结束）。不允许过时完成的计划（如随餐药）错过后不能再补点完成。
    const bool late = state == QStringLiteral("missed");
    out[QStringLiteral("allowLate")] = allowLate;
    out[QStringLiteral("isLate")] = late;
    out[QStringLiteral("canConfirm")] = state != QStringLiteral("confirmed") && (allowLate || !late);
    out[QStringLiteral("canSnooze")] = state != QStringLiteral("confirmed")
        && snoozeCount < kMaxSnooze;
    // 升级链对外字段（I2）：0=未到点，1=已提醒，2=语音加强，3=通知家人。
    out[QStringLiteral("escalationLevel")] = occ.value(QStringLiteral("escalationLevel")).toInt();
    return out;
}

QVariantList AppCore::occurrencesToday() const
{
    QVariantList out;
    for (const QVariant &entry : m_occurrences) {
        const QVariantMap occ = entry.toMap();
        const int planIndex = indexOfPlan(occ.value(QStringLiteral("planId")).toLongLong());
        if (planIndex < 0)
            continue;
        out.append(withEscalationDeadline(publicOccurrence(m_plans.at(planIndex).toMap(), occ)));
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("dueTs")).toLongLong()
            < b.toMap().value(QStringLiteral("dueTs")).toLongLong();
    });
    return out;
}

QVariantList AppCore::occurrencesForDay(const QDate &day) const
{
    QVariantList out;
    for (const QVariant &entry : m_plans) {
        const QVariantMap plan = entry.toMap();
        if (plan.value(QStringLiteral("deleted")).toBool())
            continue;
        if (!planFiresOn(plan, day))
            continue;

        const QVariantMap rule = plan.value(QStringLiteral("rule")).toMap();
        const QList<int> minutes = dueMinutesFor(rule, day);
        for (int seq = 0; seq < minutes.size(); ++seq) {
            const qint64 occurrenceId = plan.value(QStringLiteral("planId")).toLongLong()
                    * kOccurrenceIdBase
                + (day.toJulianDay() % 10000000LL) * 100 + seq;
            QVariantMap occ;
            occ[QStringLiteral("occurrenceId")] = occurrenceId;
            occ[QStringLiteral("planId")] = plan.value(QStringLiteral("planId"));
            occ[QStringLiteral("title")] = plan.value(QStringLiteral("title"));
            occ[QStringLiteral("dose")] = plan.value(QStringLiteral("dose"));
            occ[QStringLiteral("note")] = plan.value(QStringLiteral("note"));
            occ[QStringLiteral("dueTs")] = midnightMs(day) + minutes.at(seq) * 60 * 1000;
            occ[QStringLiteral("state")] = QStringLiteral("pending");
            occ[QStringLiteral("snoozeCount")] = 0;
            occ[QStringLiteral("snoozeUntilTs")] = 0;
            occ[QStringLiteral("windowMin")] = plan.value(QStringLiteral("windowMin"));
            occ[QStringLiteral("escalateAfterMin")] =
                plan.value(QStringLiteral("escalateAfterMin"));
            occ[QStringLiteral("allowLate")] = planAllowsLate(plan);

            occ = publicOccurrence(plan, occ);
            occ[QStringLiteral("canConfirm")] = false;
            occ[QStringLiteral("canSnooze")] = false;
            occ[QStringLiteral("preview")] = true;
            occ[QStringLiteral("dayLabel")] = dayLabelFor(day, m_day);
            occ[QStringLiteral("isTomorrow")] = (day == m_day.addDays(1));
            out.append(occ);
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("dueTs")).toLongLong()
            < b.toMap().value(QStringLiteral("dueTs")).toLongLong();
    });
    return out;
}

QVariantList AppCore::upcomingOccurrences() const
{
    // 找到下一个有提醒的日期，返回当天完整清单。扫一年，覆盖“每月/仅一次”等规则。
    for (int offset = 1; offset <= 366; ++offset) {
        const QVariantList list = occurrencesForDay(m_day.addDays(offset));
        if (!list.isEmpty())
            return list;
    }
    return {};
}

QVariantMap AppCore::nextUpcomingOccurrence() const
{
    const QVariantList list = upcomingOccurrences();
    if (list.isEmpty())
        return {};

    QVariantMap out = list.first().toMap();
    const qint64 dueTs = out.value(QStringLiteral("dueTs")).toLongLong();
    out[QStringLiteral("diffMinutes")] =
        qMax(0, static_cast<int>(qCeil((dueTs - m_nowMs) / 60000.0)));
    out[QStringLiteral("displayTime")] = hhmmFromMs(dueTs);
    out[QStringLiteral("dueNow")] = false;
    return out;
}

QVariantMap AppCore::currentOccurrence() const
{
    if (m_occurrences.isEmpty())
        return {};

    const QTime clockTime = QDateTime::fromMSecsSinceEpoch(clockNowMs()).time();
    const int nowMinutes = clockTime.hour() * 60 + clockTime.minute();
    QVariantMap best;
    int bestDiff = INT_MAX;

    for (const QVariant &entry : m_occurrences) {
        QVariantMap occ = entry.toMap();
        const QString state = occ.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("confirmed") || state == QStringLiteral("missed"))
            continue;

        int diff = 0;
        QString display;
        const qint64 dueTs = occ.value(QStringLiteral("dueTs")).toLongLong();
        const qint64 snoozeUntil = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong();

        if (state == QStringLiteral("snoozed") && snoozeUntil > m_nowMs) {
            diff = static_cast<int>(qCeil((snoozeUntil - m_nowMs) / 60000.0));
            display = hhmmFromMs(snoozeUntil);
        } else if (state == QStringLiteral("notified")
                   || state == QStringLiteral("escalated")
                   || state == QStringLiteral("snoozed")) {
            display = snoozeUntil > 0 ? hhmmFromMs(snoozeUntil) : hhmmFromMs(dueTs);
        } else {
            diff = qMax(0, minutesOfTime(hhmmFromMs(dueTs)) - nowMinutes);
            display = hhmmFromMs(dueTs);
        }

        if (diff < bestDiff) {
            const int planIndex = indexOfPlan(occ.value(QStringLiteral("planId")).toLongLong());
            best = planIndex >= 0
                ? withEscalationDeadline(publicOccurrence(m_plans.at(planIndex).toMap(), occ))
                : withEscalationDeadline(occ);
            best[QStringLiteral("diffMinutes")] = diff;
            best[QStringLiteral("displayTime")] = display;
            best[QStringLiteral("dueNow")] = diff == 0;
            bestDiff = diff;
        }
    }
    return best;
}

void AppCore::fireDue(qint64 occurrenceId)
{
    const int index = indexOfOccurrence(occurrenceId);
    if (index < 0)
        return;
    QVariantMap occ = m_occurrences.at(index).toMap();
    occ[QStringLiteral("state")] = QStringLiteral("notified");
    occ[QStringLiteral("snoozeUntilTs")] = 0;
    // 升级链从这个时刻开始计时（不是 dueTs）：App 被杀一夜回来时，
    // 不能一睁眼就把 15 分钟前的提醒直接升级成打电话。
    occ[QStringLiteral("notifiedAtTs")] = m_nowMs;
    occ[QStringLiteral("escalationLevel")] = 1;
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("fire"), QString());
    logOperation(QStringLiteral("提醒"), QStringLiteral("到点提醒：%1").arg(occurrenceLabel(occ)));

    const int planIndex = indexOfPlan(occ.value(QStringLiteral("planId")).toLongLong());
    if (planIndex >= 0)
        emit reminderDue(publicOccurrence(m_plans.at(planIndex).toMap(), occ));
    emit escalationChanged(occurrenceId, 1);
}

int AppCore::advanceEscalation(int index)
{
    if (index < 0 || index >= m_occurrences.size())
        return 0;
    QVariantMap occ = m_occurrences.at(index).toMap();
    const qint64 occurrenceId = occ.value(QStringLiteral("occurrenceId")).toLongLong();
    if (occ.value(QStringLiteral("state")).toString() != QStringLiteral("notified"))
        return occ.value(QStringLiteral("escalationLevel")).toInt();

    qint64 notifiedAt = occ.value(QStringLiteral("notifiedAtTs")).toLongLong();
    if (notifiedAt <= 0) {
        // 老数据 / 服务端下发的 notified 没有基线：从这一刻起算，避免瞬间升级。
        notifiedAt = m_nowMs;
        occ[QStringLiteral("notifiedAtTs")] = notifiedAt;
        m_occurrences[index] = occ;
    }

    const int escalateMin = qMax(1, occ.value(QStringLiteral("escalateAfterMin")).toInt());
    const qint64 elapsedMs = qMax<qint64>(0, m_nowMs - notifiedAt);
    int level = 1;
    if (elapsedMs >= static_cast<qint64>(escalateMin) * 60 * 1000)
        level = 3;
    else if (elapsedMs >= static_cast<qint64>(kEscalationVoiceMin) * 60 * 1000)
        level = 2;

    const int previous = occ.value(QStringLiteral("escalationLevel")).toInt();
    if (level != previous) {
        occ[QStringLiteral("escalationLevel")] = level;
        m_occurrences[index] = occ;
        emit escalationChanged(occurrenceId, level);
    }
    return level;
}

QVariantMap AppCore::withEscalationDeadline(QVariantMap occ) const
{
    const int level = occ.value(QStringLiteral("escalationLevel")).toInt();
    int nextEscalationInSec = -1;
    if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("notified")) {
        const qint64 notifiedAt = occ.value(QStringLiteral("notifiedAtTs")).toLongLong();
        if (notifiedAt > 0) {
            const int escalateMin = qMax(1, occ.value(QStringLiteral("escalateAfterMin")).toInt());
            qint64 nextTs = 0;
            if (level <= 1)
                nextTs = notifiedAt + static_cast<qint64>(kEscalationVoiceMin) * 60 * 1000;
            else if (level == 2)
                nextTs = notifiedAt + static_cast<qint64>(escalateMin) * 60 * 1000;
            nextEscalationInSec =
                nextTs > m_nowMs ? static_cast<int>((nextTs - m_nowMs + 999) / 1000) : 0;
        }
    }
    occ[QStringLiteral("escalationLevel")] = level;
    occ[QStringLiteral("nextEscalationInSec")] = nextEscalationInSec;
    return occ;
}

void AppCore::appendEvent(qint64 occurrenceId, const QString &action, const QString &reason)
{
    const qint64 ts = m_nowMs;
    const QString key = QStringLiteral("%1:%2:%3:%4")
                            .arg(occurrenceId)
                            .arg(action)
                            .arg(ts)
                            .arg(m_nextEventSeq++);
    QVariantMap event;
    event[QStringLiteral("eventId")] =
        QUuid::createUuidV5(kEventNamespace, key).toString(QUuid::WithoutBraces);
    event[QStringLiteral("occurrenceId")] = occurrenceId;
    event[QStringLiteral("action")] = action;
    event[QStringLiteral("ts")] = ts;
    event[QStringLiteral("deviceId")] = QStringLiteral("android-demo");
    event[QStringLiteral("reason")] = reason;
    event[QStringLiteral("serverSeq")] = 0;
    m_events.append(event);

    // 内存上限：从最旧的"已处理"事件开始丢，待上传（serverSeq == 0）的一条都不动。
    while (m_events.size() > kMaxEventsInMemory) {
        int victim = -1;
        for (int i = 0; i < m_events.size(); ++i) {
            if (m_events.at(i).toMap().value(QStringLiteral("serverSeq")).toLongLong() != 0) {
                victim = i;
                break;
            }
        }
        if (victim < 0)
            break;
        m_events.removeAt(victim);
    }

    emit eventsChanged();
}

qint64 AppCore::clockNowMs() const
{
    return m_clockOverrideMs > 0 ? m_clockOverrideMs : QDateTime::currentMSecsSinceEpoch();
}

QDate AppCore::clockToday() const
{
    return QDateTime::fromMSecsSinceEpoch(clockNowMs()).date();
}

void AppCore::setClockForTests(qint64 nowMs)
{
    m_clockOverrideMs = nowMs > 0 ? nowMs : 0;
    m_nowMs = clockNowMs();
    // 故意不动 m_day：跨天必须由 onTick 自己发现（归档昨天 + 重建今天），
    // 注入时钟时顺手改 m_day 会把要测的那条路径抄近路跳过。
}

void AppCore::tickForTests()
{
    onTick();
}

QVariantList AppCore::operationLog() const
{
    QVariantList out;
    out.reserve(m_opLog.size());
    for (auto it = m_opLog.crbegin(); it != m_opLog.crend(); ++it)
        out.append(*it);
    return out;
}

void AppCore::logOperation(const QString &category, const QString &text)
{
    const QString line = text.trimmed();
    if (line.isEmpty())
        return;
    QVariantMap entry;
    entry[QStringLiteral("seq")] = m_nextOpSeq++;
    // 用墙上时钟而不是 m_nowMs：后者一秒才走一格，同一秒内的操作要能分先后。
    entry[QStringLiteral("ts")] = QDateTime::currentMSecsSinceEpoch();
    entry[QStringLiteral("category")] = category.trimmed();
    entry[QStringLiteral("text")] = line;
    m_opLog.append(entry);
    while (m_opLog.size() > kMaxOperationLog)
        m_opLog.removeFirst();
    emit operationLogChanged();
}

void AppCore::clearOperationLog()
{
    m_opLog.clear();
    // 清空本身要留痕，否则看不出日志被人清过。
    logOperation(QStringLiteral("管理"), QStringLiteral("清空了操作日志"));
}

bool AppCore::confirm(qint64 occurrenceId, QString *error)
{
    const int index = indexOfOccurrence(occurrenceId);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("occurrence_not_found");
        return false;
    }
    QVariantMap occ = m_occurrences.at(index).toMap();
    if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("confirmed")) {
        if (error)
            *error = QStringLiteral("already_confirmed");
        return false;
    }

    if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("missed")) {
        const int planIndex = indexOfPlan(occ.value(QStringLiteral("planId")).toLongLong());
        if (planIndex >= 0 && !planAllowsLate(m_plans.at(planIndex).toMap())) {
            if (error)
                *error = QStringLiteral("late_not_allowed");
            return false;
        }
    }

    const QString title = occ.value(QStringLiteral("title")).toString();
    const QString time = hhmmFromMs(occ.value(QStringLiteral("dueTs")).toLongLong());
    occ[QStringLiteral("state")] = QStringLiteral("confirmed");
    occ[QStringLiteral("snoozeUntilTs")] = 0;
    occ[QStringLiteral("escalationLevel")] = 0;
    occ[QStringLiteral("notifiedAtTs")] = 0;
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("confirm"), QString());
    logOperation(QStringLiteral("提醒"), QStringLiteral("确认完成：%1").arg(occurrenceLabel(occ)));

    emit occurrencesChanged();
    emit currentChanged();
    emit toast(QStringLiteral("已记下：%1 %2").arg(time, title));
    return true;
}

bool AppCore::snooze(qint64 occurrenceId, QString *error)
{
    const int index = indexOfOccurrence(occurrenceId);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("occurrence_not_found");
        return false;
    }
    QVariantMap occ = m_occurrences.at(index).toMap();
    const QString state = occ.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("confirmed")) {
        if (error)
            *error = QStringLiteral("already_confirmed");
        return false;
    }
    const int used = occ.value(QStringLiteral("snoozeCount")).toInt();
    if (used >= kMaxSnooze) {
        if (error)
            *error = QStringLiteral("snooze_limit");
        return false;
    }

    occ[QStringLiteral("state")] = QStringLiteral("snoozed");
    occ[QStringLiteral("snoozeCount")] = used + 1;
    occ[QStringLiteral("snoozeUntilTs")] = m_nowMs + kSnoozeMinutes * 60 * 1000;
    occ[QStringLiteral("escalationLevel")] = 0;
    occ[QStringLiteral("notifiedAtTs")] = 0;
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("snooze"), QString());
    logOperation(QStringLiteral("提醒"),
                 QStringLiteral("推迟 %1 分钟（第 %2 次）：%3")
                     .arg(kSnoozeMinutes).arg(used + 1).arg(occurrenceLabel(occ)));

    emit occurrencesChanged();
    emit currentChanged();
    emit toast(QStringLiteral("好的，%1 分钟后再提醒你").arg(kSnoozeMinutes));
    return true;
}

bool AppCore::undoOccurrence(qint64 occurrenceId, QString *error)
{
    const int index = indexOfOccurrence(occurrenceId);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("occurrence_not_found");
        return false;
    }
    QVariantMap occ = m_occurrences.at(index).toMap();
    const QString state = occ.value(QStringLiteral("state")).toString();

    QString next;
    if (state == QStringLiteral("confirmed") || state == QStringLiteral("snoozed")
        || state == QStringLiteral("missed") || state == QStringLiteral("escalated")) {
        next = QStringLiteral("notified");
    } else if (state == QStringLiteral("notified")) {
        next = QStringLiteral("pending");
    } else {
        if (error)
            *error = QStringLiteral("nothing_to_undo");
        return false;
    }

    occ[QStringLiteral("state")] = next;
    occ[QStringLiteral("snoozeUntilTs")] = 0;
    if (next == QStringLiteral("notified")) {
        // 撤回确认后回到待确认：升级链从现在重新计时。
        occ[QStringLiteral("notifiedAtTs")] = m_nowMs;
        occ[QStringLiteral("escalationLevel")] = 1;
    } else {
        occ[QStringLiteral("notifiedAtTs")] = 0;
        occ[QStringLiteral("escalationLevel")] = 0;
    }
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("undo"), QString());
    logOperation(QStringLiteral("提醒"),
                 QStringLiteral("撤销，恢复为%1：%2")
                     .arg(next == QStringLiteral("pending") ? QStringLiteral("待提醒")
                                                            : QStringLiteral("待确认"),
                          occurrenceLabel(occ)));

    emit occurrencesChanged();
    emit currentChanged();
    emit toast(QStringLiteral("已撤销"));
    return true;
}

bool AppCore::escalate(qint64 occurrenceId, QString *error)
{
    const int index = indexOfOccurrence(occurrenceId);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("occurrence_not_found");
        return false;
    }
    QVariantMap occ = m_occurrences.at(index).toMap();
    const QString state = occ.value(QStringLiteral("state")).toString();
    if (state != QStringLiteral("notified") && state != QStringLiteral("snoozed")
        && state != QStringLiteral("missed")) {
        if (error)
            *error = QStringLiteral("not_escalatable");
        return false;
    }

    occ[QStringLiteral("state")] = QStringLiteral("escalated");
    occ[QStringLiteral("escalationLevel")] = 3;
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("escalate"), QStringLiteral("timeout"));
    logOperation(QStringLiteral("提醒"),
                 QStringLiteral("超时未确认，已通知%1：%2")
                     .arg(m_caregiverName.isEmpty() ? QStringLiteral("家人") : m_caregiverName,
                          occurrenceLabel(occ)));

    emit occurrencesChanged();
    emit currentChanged();
    emit toast(QStringLiteral("已通知 %1：%2 还没有确认")
                   .arg(m_caregiverName, occ.value(QStringLiteral("title")).toString()));
    return true;
}

void AppCore::sos()
{
    appendEvent(0, QStringLiteral("escalate"), QStringLiteral("sos"));
    logOperation(QStringLiteral("呼救"),
                 QStringLiteral("发出紧急呼救，通知%1")
                     .arg(m_caregiverName.isEmpty() ? QStringLiteral("家人") : m_caregiverName));
    emit toast(QStringLiteral("已通知 %1").arg(m_caregiverName.isEmpty() ? QStringLiteral("家人") : m_caregiverName));
}

void AppCore::triggerDemoReminder()
{
    if (!m_demoMode) {
        emit toast(QStringLiteral("请先在设置里打开演示模式"));
        return;
    }
    qint64 target = 0;
    for (const QVariant &entry : std::as_const(m_occurrences)) {
        const QVariantMap occ = entry.toMap();
        const QString state = occ.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("pending") || state == QStringLiteral("notified")) {
            target = occ.value(QStringLiteral("occurrenceId")).toLongLong();
            break;
        }
    }
    if (target == 0 && !m_occurrences.isEmpty())
        target = m_occurrences.first().toMap().value(QStringLiteral("occurrenceId")).toLongLong();
    if (target == 0)
        return;
    fireDue(target);
    emit occurrencesChanged();
    emit currentChanged();
}

int AppCore::todayDone() const
{
    int n = 0;
    for (const QVariant &entry : m_occurrences) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("paused")).toBool())
            continue;
        if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("confirmed"))
            ++n;
    }
    return n;
}

int AppCore::todayTotal() const
{
    int n = 0;
    for (const QVariant &entry : m_occurrences) {
        if (!entry.toMap().value(QStringLiteral("paused")).toBool())
            ++n;
    }
    return n;
}

int AppCore::pendingCount() const
{
    int n = 0;
    for (const QVariant &entry : m_occurrences) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("paused")).toBool())
            continue;
        if (occ.value(QStringLiteral("state")).toString() != QStringLiteral("confirmed"))
            ++n;
    }
    return n;
}

QVariantList AppCore::plans() const
{
    QVariantList out;
    for (const QVariant &entry : m_plans) {
        QVariantMap plan = entry.toMap();
        if (!plan.value(QStringLiteral("deleted")).toBool()) {
            plan[QStringLiteral("repeatText")] =
                repeatTextFor(plan.value(QStringLiteral("rule")).toMap());
            out.append(plan);
        }
    }
    return out;
}

qint64 AppCore::createPlan(const QVariantMap &plan, QString *error)
{
    const QString title = plan.value(QStringLiteral("title")).toString().trimmed();
    const QVariantMap rule = plan.value(QStringLiteral("rule")).toMap();
    if (title.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty_title");
        return 0;
    }
    if (normalizeKind(rule).isEmpty()) {
        if (error)
            *error = QStringLiteral("bad_rule_kind");
        return 0;
    }
    if (rule.value(QStringLiteral("kind")).toString() != QStringLiteral("once")
        && rule.value(QStringLiteral("kind")).toString() != QStringLiteral("meal_offset")
        && rule.value(QStringLiteral("times")).toStringList().isEmpty()) {
        if (error)
            *error = QStringLiteral("empty_times");
        return 0;
    }

    QVariantMap stored = plan;
    if (!m_demoMode) {
        // 真实域：先用本地号段，等 POST /v1/plans 回执换成服务端号
        if (m_nextPlanId < kLocalPlanIdBase)
            m_nextPlanId = kLocalPlanIdBase;
        stored[QStringLiteral("localOnly")] = true;
    }
    stored[QStringLiteral("planId")] = m_nextPlanId++;
    stored[QStringLiteral("windowMin")] =
        plan.value(QStringLiteral("windowMin"), kDefaultWindowMin).toInt();
    stored[QStringLiteral("escalateAfterMin")] =
        plan.value(QStringLiteral("escalateAfterMin"), kDefaultEscalateMin).toInt();
    stored[QStringLiteral("allowLate")] = plan.value(QStringLiteral("allowLate"), true).toBool();
    stored[QStringLiteral("version")] = 1;
    stored[QStringLiteral("deleted")] = false;
    m_plans.append(stored);

    m_day = clockToday();
    rebuildToday();
    logOperation(QStringLiteral("计划"),
                 QStringLiteral("新增提醒：%1 %2").arg(planLabel(stored), repeatTextFor(rule)));
    emit plansChanged();
    emit toast(QStringLiteral("已添加提醒：%1 %2")
                   .arg(dueMinutesFor(rule, m_day).isEmpty()
                            ? QString()
                            : hhmmFromMinutes(dueMinutesFor(rule, m_day).first()),
                        title));
    return stored.value(QStringLiteral("planId")).toLongLong();
}

bool AppCore::updatePlan(qint64 planId, const QVariantMap &plan, QString *error)
{
    const int index = indexOfPlan(planId);
    if (index < 0 || m_plans.at(index).toMap().value(QStringLiteral("deleted")).toBool()) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    if (plan.contains(QStringLiteral("title"))
        && plan.value(QStringLiteral("title")).toString().trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("empty_title");
        return false;
    }
    if (plan.contains(QStringLiteral("rule"))) {
        const QVariantMap rule = plan.value(QStringLiteral("rule")).toMap();
        const QString kind = normalizeKind(rule);
        if (kind.isEmpty()) {
            if (error)
                *error = QStringLiteral("bad_rule_kind");
            return false;
        }
        if (kind != QStringLiteral("once") && kind != QStringLiteral("meal_offset")
            && rule.value(QStringLiteral("times")).toStringList().isEmpty()) {
            if (error)
                *error = QStringLiteral("empty_times");
            return false;
        }
        if (kind == QStringLiteral("weekly")
            && rule.value(QStringLiteral("weekdays")).toList().isEmpty()) {
            if (error)
                *error = QStringLiteral("empty_weekdays");
            return false;
        }
    }

    QVariantMap stored = m_plans.at(index).toMap();
    for (auto it = plan.constBegin(); it != plan.constEnd(); ++it) {
        // 身份与删除标记只能由各自的命令改，补丁里的同名键忽略。
        if (it.key() == QStringLiteral("planId") || it.key() == QStringLiteral("deleted")
            || it.key() == QStringLiteral("version") || it.key() == QStringLiteral("localOnly")
            || it.key() == QStringLiteral("syncRev") || it.key() == QStringLiteral("syncedRev")
            || it.key() == QStringLiteral("syncedVersion")
            || it.key() == QStringLiteral("syncRejected"))
            continue;
        stored.insert(it.key(), it.value());
    }
    stored[QStringLiteral("planId")] = planId;
    stored[QStringLiteral("version")] = stored.value(QStringLiteral("version")).toInt() + 1;
    stored[QStringLiteral("syncRev")] = stored.value(QStringLiteral("syncRev")).toLongLong() + 1;
    m_plans[index] = stored;

    // 记下这条计划今天每个 occurrence 原来的时间；改了时间的未完成项重新从"待提醒"开始，
    // 已经确认的保持确认（今天的进度不丢）。
    QHash<qint64, qint64> oldDue;
    for (const QVariant &entry : std::as_const(m_occurrences)) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() == planId)
            oldDue.insert(occ.value(QStringLiteral("occurrenceId")).toLongLong(),
                          occ.value(QStringLiteral("dueTs")).toLongLong());
    }

    m_day = clockToday();
    rebuildToday();

    for (int i = 0; i < m_occurrences.size(); ++i) {
        QVariantMap occ = m_occurrences.at(i).toMap();
        const qint64 id = occ.value(QStringLiteral("occurrenceId")).toLongLong();
        const auto it = oldDue.constFind(id);
        if (it == oldDue.constEnd() || *it == occ.value(QStringLiteral("dueTs")).toLongLong())
            continue;
        if (occ.value(QStringLiteral("state")).toString() == QStringLiteral("confirmed"))
            continue;
        occ[QStringLiteral("state")] = QStringLiteral("pending");
        occ[QStringLiteral("snoozeCount")] = 0;
        occ[QStringLiteral("snoozeUntilTs")] = 0;
        m_occurrences[i] = occ;
    }

    logOperation(QStringLiteral("计划"),
                 QStringLiteral("修改提醒：%1 %2")
                     .arg(planLabel(stored),
                          repeatTextFor(stored.value(QStringLiteral("rule")).toMap())));
    emit occurrencesChanged();
    emit currentChanged();
    emit plansChanged();
    return true;
}

bool AppCore::skipToday(qint64 planId, QString *error)
{
    const int index = indexOfPlan(planId);
    if (index < 0 || m_plans.at(index).toMap().value(QStringLiteral("deleted")).toBool()) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    m_day = clockToday();
    QVariantList stash;
    for (const QVariant &entry : std::as_const(m_occurrences)) {
        if (entry.toMap().value(QStringLiteral("planId")).toLongLong() == planId)
            stash.append(entry);
    }
    if (stash.isEmpty()) {
        if (error)
            *error = QStringLiteral("nothing_today");
        return false;
    }

    QVariantMap stored = m_plans.at(index).toMap();
    QStringList skip = stored.value(QStringLiteral("skipDates")).toStringList();
    const QString today = m_day.toString(Qt::ISODate);
    if (!skip.contains(today))
        skip.append(today);
    stored[QStringLiteral("skipDates")] = skip;
    stored[QStringLiteral("version")] = stored.value(QStringLiteral("version")).toInt() + 1;
    m_plans[index] = stored;

    m_skippedOccurrences.insert(planId, stash);
    rebuildToday();
    logOperation(QStringLiteral("计划"), QStringLiteral("今天不再提醒：%1").arg(planLabel(stored)));
    emit plansChanged();
    return true;
}

bool AppCore::unskipToday(qint64 planId, QString *error)
{
    const int index = indexOfPlan(planId);
    m_day = clockToday();
    const QString today = m_day.toString(Qt::ISODate);
    if (index < 0
        || !m_plans.at(index).toMap().value(QStringLiteral("skipDates")).toStringList().contains(today)) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    QVariantMap stored = m_plans.at(index).toMap();
    QStringList skip = stored.value(QStringLiteral("skipDates")).toStringList();
    skip.removeAll(today);
    stored[QStringLiteral("skipDates")] = skip;
    stored[QStringLiteral("version")] = stored.value(QStringLiteral("version")).toInt() + 1;
    m_plans[index] = stored;

    rebuildTodayFrom(m_occurrences + m_skippedOccurrences.take(planId));
    logOperation(QStringLiteral("计划"), QStringLiteral("恢复今天的提醒：%1").arg(planLabel(stored)));
    emit plansChanged();
    emit toast(QStringLiteral("已恢复今天的：%1").arg(stored.value(QStringLiteral("title")).toString()));
    return true;
}

bool AppCore::deletePlan(qint64 planId, QString *error)
{
    const int index = indexOfPlan(planId);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    QVariantMap stored = m_plans.at(index).toMap();
    if (stored.value(QStringLiteral("deleted")).toBool()) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    stored[QStringLiteral("deleted")] = true;
    stored[QStringLiteral("version")] = stored.value(QStringLiteral("version")).toInt() + 1;
    stored[QStringLiteral("syncRev")] = stored.value(QStringLiteral("syncRev")).toLongLong() + 1;
    m_plans[index] = stored;

    // 先留住今天这条计划的 occurrence，撤销删除时连同已确认的进度一起恢复。
    QVariantList stash;
    for (const QVariant &entry : std::as_const(m_occurrences)) {
        if (entry.toMap().value(QStringLiteral("planId")).toLongLong() == planId)
            stash.append(entry);
    }
    m_deletedOccurrences.insert(planId, stash);

    m_day = clockToday();
    rebuildToday();
    logOperation(QStringLiteral("计划"), QStringLiteral("删除提醒：%1").arg(planLabel(stored)));
    emit plansChanged();
    return true;
}

bool AppCore::restorePlan(qint64 planId, QString *error)
{
    const int index = indexOfPlan(planId);
    if (index < 0 || !m_plans.at(index).toMap().value(QStringLiteral("deleted")).toBool()) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    QVariantMap stored = m_plans.at(index).toMap();
    stored[QStringLiteral("deleted")] = false;
    stored[QStringLiteral("version")] = stored.value(QStringLiteral("version")).toInt() + 1;
    stored[QStringLiteral("syncRev")] = stored.value(QStringLiteral("syncRev")).toLongLong() + 1;
    m_plans[index] = stored;

    m_day = clockToday();
    rebuildTodayFrom(m_occurrences + m_deletedOccurrences.take(planId));
    logOperation(QStringLiteral("计划"), QStringLiteral("撤销删除：%1").arg(planLabel(stored)));
    emit plansChanged();
    emit toast(QStringLiteral("已恢复：%1").arg(stored.value(QStringLiteral("title")).toString()));
    return true;
}

qint64 AppCore::addRecord(const QString &type, double value, const QString &unit)
{
    QVariantMap rec;
    rec[QStringLiteral("recordId")] = m_nextRecordId++;
    rec[QStringLiteral("type")] = type;
    rec[QStringLiteral("value")] = value;
    rec[QStringLiteral("unit")] = unit;
    rec[QStringLiteral("at")] = m_nowMs;
    rec[QStringLiteral("correctedFrom")] = 0;
    m_records.append(rec);
    logOperation(QStringLiteral("记录"), QStringLiteral("记下：%1").arg(recordLabel(type, value, unit)));
    emit recordsChanged();
    return rec.value(QStringLiteral("recordId")).toLongLong();
}

bool AppCore::correctRecord(qint64 recordId, const QVariantMap &patch, QString *error)
{
    for (int i = 0; i < m_records.size(); ++i) {
        QVariantMap rec = m_records.at(i).toMap();
        if (rec.value(QStringLiteral("recordId")).toLongLong() != recordId)
            continue;
        for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
            // 身份与同步字段只归本类管，修正补丁不能改
            if (it.key() == QStringLiteral("recordId") || it.key() == QStringLiteral("type")
                || it.key() == QStringLiteral("serverRecordId")
                || it.key() == QStringLiteral("syncRev")
                || it.key() == QStringLiteral("syncedRev"))
                continue;
            rec.insert(it.key(), it.value());
        }
        rec[QStringLiteral("correctedFrom")] = recordId;
        // 版本 +1：已上传的记录据此补一次 PATCH；还在上传途中的，回执后也能发现落后
        rec[QStringLiteral("syncRev")] = rec.value(QStringLiteral("syncRev")).toLongLong() + 1;
        m_records[i] = rec;
        logOperation(QStringLiteral("记录"),
                     QStringLiteral("修正记录：%1")
                         .arg(recordLabel(rec.value(QStringLiteral("type")).toString(),
                                          rec.value(QStringLiteral("value")).toDouble(),
                                          rec.value(QStringLiteral("unit")).toString())));
        emit recordsChanged();
        return true;
    }
    if (error)
        *error = QStringLiteral("record_not_found");
    return false;
}

bool AppCore::removeRecord(qint64 recordId)
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).toMap().value(QStringLiteral("recordId")).toLongLong() != recordId)
            continue;
        const QVariantMap rec = m_records.at(i).toMap();
        m_records.removeAt(i);
        logOperation(QStringLiteral("记录"),
                     QStringLiteral("撤销记录：%1")
                         .arg(recordLabel(rec.value(QStringLiteral("type")).toString(),
                                          rec.value(QStringLiteral("value")).toDouble(),
                                          rec.value(QStringLiteral("unit")).toString())));
        emit recordsChanged();
        return true;
    }
    return false;
}

int AppCore::waterCups() const
{
    const qint64 today0 = midnightMs(clockToday());
    int cups = 0;
    for (const QVariant &entry : m_records) {
        const QVariantMap rec = entry.toMap();
        if (rec.value(QStringLiteral("type")).toString() != QStringLiteral("water"))
            continue;
        if (rec.value(QStringLiteral("at")).toLongLong() < today0)
            continue;
        cups += int(rec.value(QStringLiteral("value")).toDouble());
    }
    return cups;
}

QString AppCore::sleepStartText() const
{
    qint64 best = 0;
    for (const QVariant &entry : m_records) {
        const QVariantMap rec = entry.toMap();
        if (rec.value(QStringLiteral("type")).toString() != QStringLiteral("sleep_start"))
            continue;
        best = qMax(best, rec.value(QStringLiteral("at")).toLongLong());
    }
    return best > 0 ? hhmmFromMs(best) : QString();
}

QString AppCore::sleepEndText() const
{
    qint64 best = 0;
    for (const QVariant &entry : m_records) {
        const QVariantMap rec = entry.toMap();
        if (rec.value(QStringLiteral("type")).toString() != QStringLiteral("sleep_end"))
            continue;
        best = qMax(best, rec.value(QStringLiteral("at")).toLongLong());
    }
    return best > 0 ? hhmmFromMs(best) : QString();
}

int AppCore::sleepMinutes() const
{
    qint64 start = 0;
    qint64 end = 0;
    for (const QVariant &entry : m_records) {
        const QVariantMap rec = entry.toMap();
        const QString type = rec.value(QStringLiteral("type")).toString();
        const qint64 at = rec.value(QStringLiteral("at")).toLongLong();
        if (type == QStringLiteral("sleep_start"))
            start = qMax(start, at);
        else if (type == QStringLiteral("sleep_end"))
            end = qMax(end, at);
    }
    if (start <= 0 || end <= 0 || end <= start)
        return 0;
    return int((end - start) / 60000);
}

int AppCore::activityMinutes() const
{
    const qint64 today0 = midnightMs(clockToday());
    int minutes = 0;
    for (const QVariant &entry : m_records) {
        const QVariantMap rec = entry.toMap();
        if (rec.value(QStringLiteral("type")).toString() != QStringLiteral("activity"))
            continue;
        if (rec.value(QStringLiteral("at")).toLongLong() < today0)
            continue;
        minutes += int(rec.value(QStringLiteral("value")).toDouble());
    }
    return minutes;
}

QVariantList AppCore::records() const
{
    return m_records;
}

// ---------------------------------------------------------------------------
// I4 统计趋势：按自然日聚合记录，空日补 0，最后一项固定是今天。
// 睡眠记在"起床"那天：每条 sleep_end 找它之前最近的一条 sleep_start，
// 所以跨零点的睡眠（昨晚 22:40 → 今早 06:30）算在今天。
// ---------------------------------------------------------------------------

QVariantList AppCore::recordTrend(const QString &type, int days) const
{
    const int span = qBound(1, days, 365);
    const QDate today = clockToday();
    QHash<QString, double> byDay;

    if (type == QStringLiteral("sleep")) {
        QList<qint64> starts;
        for (const QVariant &entry : m_records) {
            const QVariantMap rec = entry.toMap();
            if (rec.value(QStringLiteral("type")).toString() == QStringLiteral("sleep_start"))
                starts.append(rec.value(QStringLiteral("at")).toLongLong());
        }
        std::sort(starts.begin(), starts.end());

        for (const QVariant &entry : m_records) {
            const QVariantMap rec = entry.toMap();
            if (rec.value(QStringLiteral("type")).toString() != QStringLiteral("sleep_end"))
                continue;
            const qint64 end = rec.value(QStringLiteral("at")).toLongLong();
            qint64 start = 0;
            for (qint64 candidate : starts) {
                if (candidate < end)
                    start = qMax(start, candidate);
            }
            if (start <= 0 || end <= start)
                continue;
            const QDate day = QDateTime::fromMSecsSinceEpoch(end).date();
            byDay[day.toString(Qt::ISODate)] += double(end - start) / 60000.0;
        }
    } else {
        const QString kind = type == QStringLiteral("water") ? QStringLiteral("water")
                                                            : QStringLiteral("activity");
        for (const QVariant &entry : m_records) {
            const QVariantMap rec = entry.toMap();
            if (rec.value(QStringLiteral("type")).toString() != kind)
                continue;
            const QDate day =
                QDateTime::fromMSecsSinceEpoch(rec.value(QStringLiteral("at")).toLongLong()).date();
            byDay[day.toString(Qt::ISODate)] += rec.value(QStringLiteral("value")).toDouble();
        }
    }

    QVariantList out;
    out.reserve(span);
    for (int i = span - 1; i >= 0; --i) {
        const QDate day = today.addDays(-i);
        const double value = byDay.value(day.toString(Qt::ISODate), 0.0);
        QVariantMap item;
        item[QStringLiteral("date")] = day.toString(Qt::ISODate);
        item[QStringLiteral("value")] =
            type == QStringLiteral("sleep") ? QVariant(qRound(value)) : QVariant(value);
        out.append(item);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 暂停模式（R1-7）：设备级开关。暂停期间到点的提醒打 paused 标记，
// 不弹、不升级、不算漏服；恢复后只对"还没到点"的提醒照常调度，
// 不补弹已经过去的，避免回家后一次性炸出一堆提醒。
// ---------------------------------------------------------------------------

bool AppCore::paused() const
{
    return m_pauseUntilMs > 0 && clockNowMs() < m_pauseUntilMs;
}

bool AppCore::pauseUntil(qint64 untilMs)
{
    if (untilMs <= clockNowMs())
        return false;
    m_pauseUntilMs = untilMs;
    markPausedOccurrences();
    logOperation(QStringLiteral("提醒"),
                 QStringLiteral("暂停提醒到 %1")
                     .arg(QDateTime::fromMSecsSinceEpoch(untilMs)
                              .toString(QStringLiteral("MM-dd HH:mm"))));
    emit pausedChanged();
    emit occurrencesChanged();
    emit currentChanged();
    schedulePersist();
    return true;
}

bool AppCore::pauseForMinutes(int minutes)
{
    if (minutes <= 0)
        return false;
    return pauseUntil(clockNowMs() + static_cast<qint64>(minutes) * 60 * 1000);
}

bool AppCore::resumeReminders()
{
    if (m_pauseUntilMs == 0)
        return false;
    m_pauseUntilMs = 0;

    // 恢复时只放开"还没到点"的提醒；暂停期间已经过去的保持"已暂停"，
    // 不补弹（住院一周回家不该一次炸出 7 条）。
    const qint64 now = clockNowMs();
    bool resumedAny = false;
    for (int i = 0; i < m_occurrences.size(); ++i) {
        QVariantMap occ = m_occurrences.at(i).toMap();
        if (!occ.value(QStringLiteral("paused")).toBool())
            continue;
        const qint64 snoozeUntil = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong();
        const qint64 dueAt = snoozeUntil > 0 ? snoozeUntil
                                             : occ.value(QStringLiteral("dueTs")).toLongLong();
        if (dueAt <= now)
            continue;
        occ[QStringLiteral("paused")] = false;
        m_occurrences[i] = occ;
        resumedAny = true;
    }
    Q_UNUSED(resumedAny)

    logOperation(QStringLiteral("提醒"), QStringLiteral("恢复提醒"));
    emit pausedChanged();
    emit occurrencesChanged();
    emit currentChanged();
    schedulePersist();
    return true;
}

void AppCore::markPausedOccurrences()
{
    if (m_pauseUntilMs <= 0)
        return;
    bool changed = false;
    for (int i = 0; i < m_occurrences.size(); ++i) {
        QVariantMap occ = m_occurrences.at(i).toMap();
        if (occ.value(QStringLiteral("paused")).toBool())
            continue;
        const QString state = occ.value(QStringLiteral("state")).toString();
        if (state != QStringLiteral("pending") && state != QStringLiteral("snoozed"))
            continue;
        const qint64 snoozeUntil = occ.value(QStringLiteral("snoozeUntilTs")).toLongLong();
        const qint64 dueAt = snoozeUntil > 0 ? snoozeUntil
                                             : occ.value(QStringLiteral("dueTs")).toLongLong();
        // 只有"暂停期内会到点"的才标暂停；暂停结束后才到点的照常提醒。
        if (dueAt > m_pauseUntilMs)
            continue;
        occ[QStringLiteral("paused")] = true;
        m_occurrences[i] = occ;
        changed = true;
    }
    if (changed)
        logOperation(QStringLiteral("提醒"),
                     QStringLiteral("暂停期间到点的提醒记为已暂停"));
}


QVariantList AppCore::stats(int days) const
{
    // 统一口径：每天 {total, done, missed}；"今天"永远取今天实时的 occurrence，
    // 这样首页进度、首页完成率和统计页三处看到的是同一份数据。
    // 演示域：今天以前是虚构曲线；真实域：只从开始使用那天算起，没有前期数据。
    static const QList<int> base7 = {50, 50, 100, 100, 100, 100, 100};
    static const QList<int> base30 = {50, 100, 75, 100, 100, 50, 100, 100, 75, 100};
    const QList<int> &base = days == 7 ? base7 : base30;
    const QDate today = m_day.isValid() ? m_day : clockToday();

    int first = days - 1;
    if (!m_demoMode && m_startDate.isValid())
        first = qBound(0, static_cast<int>(m_startDate.daysTo(today)), days - 1);

    const int shown = first + 1;
    QVariantList out;
    for (int i = first; i >= 0; --i) {
        const QDate day = today.addDays(-i);
        int total = 0;
        int done = 0;
        int missed = 0;
        int paused = 0;
        if (i == 0) {
            const QVariantMap t = tally(m_occurrences, false);
            total = t.value(QStringLiteral("total")).toInt();
            done = t.value(QStringLiteral("done")).toInt();
            missed = t.value(QStringLiteral("missed")).toInt();
            paused = t.value(QStringLiteral("paused")).toInt();
        } else if (m_demoMode) {
            const int fake = base.at((days - 1 - i) % base.size());
            total = 4;
            done = qRound(fake * total / 100.0);
            missed = total - done;
        } else {
            const QVariantMap t = m_history.value(day.toString(Qt::ISODate)).toMap();
            total = t.value(QStringLiteral("total")).toInt();
            done = t.value(QStringLiteral("done")).toInt();
            missed = t.value(QStringLiteral("missed")).toInt();
            paused = t.value(QStringLiteral("paused")).toInt();
        }
        const int rate = total > 0 ? qRound(done * 100.0 / total) : 0;
        const bool showLabel = shown <= 7 || i == first || i == 0 || i % 5 == 0;

        QVariantMap item;
        item[QStringLiteral("date")] = day.toString(Qt::ISODate);
        item[QStringLiteral("label")] =
            showLabel ? (days == 7 ? weekdayShort(day.dayOfWeek())
                                   : QStringLiteral("%1/%2").arg(day.month()).arg(day.day()))
                      : QString();
        item[QStringLiteral("rate")] = rate;
        // v1.1 正式键
        item[QStringLiteral("expected")] = total;
        item[QStringLiteral("confirmed")] = done;
        item[QStringLiteral("missed")] = missed;
        // 暂停条数单独计：不进分母、不算漏服。
        item[QStringLiteral("paused")] = paused;
        // 兼容旧展示代码
        item[QStringLiteral("done")] = done;
        item[QStringLiteral("total")] = total;
        item[QStringLiteral("isToday")] = i == 0;
        out.append(item);
    }
    return out;
}

QVariantList AppCore::eventLog() const
{
    return m_events;
}

QString AppCore::eventLogCsv() const
{
    QString csv = QStringLiteral("event_id,occurrence_id,action,ts,device_id,reason,server_seq\n");
    for (const QVariant &entry : m_events) {
        const QVariantMap event = entry.toMap();
        csv += QStringLiteral("%1,%2,%3,%4,%5,%6,%7\n")
                   .arg(event.value(QStringLiteral("eventId")).toString())
                   .arg(event.value(QStringLiteral("occurrenceId")).toLongLong())
                   .arg(event.value(QStringLiteral("action")).toString())
                   .arg(event.value(QStringLiteral("ts")).toLongLong())
                   .arg(event.value(QStringLiteral("deviceId")).toString())
                   .arg(event.value(QStringLiteral("reason")).toString())
                   .arg(event.value(QStringLiteral("serverSeq")).toLongLong());
    }
    return csv;
}

QVariantList AppCore::pendingEvents() const
{
    QVariantList out;
    for (const QVariant &entry : m_events) {
        const QVariantMap event = entry.toMap();
        if (event.value(QStringLiteral("serverSeq")).toLongLong() == 0)
            out.append(event);
    }
    return out;
}

void AppCore::markEventsSynced(const QVariantList &acked)
{
    bool changed = false;
    for (const QVariant &entry : acked) {
        const QVariantMap ack = entry.toMap();
        const QString eventId = ack.value(QStringLiteral("eventId")).toString();
        const qint64 seq = ack.value(QStringLiteral("serverSeq")).toLongLong();
        if (eventId.isEmpty() || seq <= 0)
            continue;
        for (int i = 0; i < m_events.size(); ++i) {
            QVariantMap event = m_events.at(i).toMap();
            if (event.value(QStringLiteral("eventId")).toString() != eventId
                || event.value(QStringLiteral("serverSeq")).toLongLong() != 0)
                continue;
            event[QStringLiteral("serverSeq")] = seq;
            m_events[i] = event;
            changed = true;
            break;
        }
    }
    if (changed)
        emit eventsChanged();
}

void AppCore::expireEvents(const QStringList &eventIds)
{
    bool changed = false;
    for (int i = 0; i < m_events.size(); ++i) {
        QVariantMap event = m_events.at(i).toMap();
        if (event.value(QStringLiteral("serverSeq")).toLongLong() != 0)
            continue;
        if (!eventIds.contains(event.value(QStringLiteral("eventId")).toString()))
            continue;
        event[QStringLiteral("serverSeq")] = -1; // -1 = 服务端已拒绝，不再重传
        m_events[i] = event;
        changed = true;
    }
    if (changed)
        emit eventsChanged();
}

namespace {

bool recordNeedsUpload(const QVariantMap &rec)
{
    const qint64 serverId = rec.value(QStringLiteral("serverRecordId")).toLongLong();
    if (serverId == 0)
        return true;
    return serverId > 0
        && rec.value(QStringLiteral("syncRev")).toLongLong()
            > rec.value(QStringLiteral("syncedRev")).toLongLong();
}

} // namespace

QVariantList AppCore::pendingRecords() const
{
    QVariantList out;
    if (m_demoMode)
        return out;
    for (const QVariant &entry : m_records) {
        QVariantMap rec = entry.toMap();
        if (!recordNeedsUpload(rec))
            continue;
        const QString key = QStringLiteral("record:%1:%2")
                                .arg(rec.value(QStringLiteral("recordId")).toLongLong())
                                .arg(rec.value(QStringLiteral("at")).toLongLong());
        rec[QStringLiteral("clientId")] =
            QUuid::createUuidV5(kEventNamespace, key).toString(QUuid::WithoutBraces);
        out.append(rec);
    }
    return out;
}

int AppCore::pendingRecordCount() const
{
    if (m_demoMode)
        return 0;
    int count = 0;
    for (const QVariant &entry : m_records) {
        if (recordNeedsUpload(entry.toMap()))
            ++count;
    }
    return count;
}

void AppCore::markRecordSynced(qint64 recordId, qint64 serverRecordId, qint64 rev)
{
    for (int i = 0; i < m_records.size(); ++i) {
        QVariantMap rec = m_records.at(i).toMap();
        if (rec.value(QStringLiteral("recordId")).toLongLong() != recordId)
            continue;
        if (serverRecordId > 0 && rec.value(QStringLiteral("serverRecordId")).toLongLong() <= 0)
            rec[QStringLiteral("serverRecordId")] = serverRecordId;
        if (rev > rec.value(QStringLiteral("syncedRev")).toLongLong())
            rec[QStringLiteral("syncedRev")] = rev;
        m_records[i] = rec;
        emit recordsChanged();
        return;
    }
    // 找不到：上传途中被撤销了（服务端暂无删除接口，那条会留在服务端）
}

void AppCore::rejectRecord(qint64 recordId)
{
    for (int i = 0; i < m_records.size(); ++i) {
        QVariantMap rec = m_records.at(i).toMap();
        if (rec.value(QStringLiteral("recordId")).toLongLong() != recordId)
            continue;
        rec[QStringLiteral("serverRecordId")] = -1;
        m_records[i] = rec;
        emit recordsChanged();
        return;
    }
}

void AppCore::forgetServerRecord(qint64 recordId)
{
    for (int i = 0; i < m_records.size(); ++i) {
        QVariantMap rec = m_records.at(i).toMap();
        if (rec.value(QStringLiteral("recordId")).toLongLong() != recordId)
            continue;
        rec[QStringLiteral("serverRecordId")] = 0;
        rec[QStringLiteral("syncedRev")] = 0;
        m_records[i] = rec;
        emit recordsChanged();
        return;
    }
}

namespace {

QString planSyncOp(const QVariantMap &plan)
{
    if (plan.value(QStringLiteral("syncRejected")).toBool())
        return QString();
    const bool deleted = plan.value(QStringLiteral("deleted")).toBool();
    if (plan.value(QStringLiteral("localOnly")).toBool())
        return deleted ? QString() : QStringLiteral("create"); // 没传过又删了：不用传
    if (plan.value(QStringLiteral("syncRev")).toLongLong()
        <= plan.value(QStringLiteral("syncedRev")).toLongLong())
        return QString();
    return deleted ? QStringLiteral("delete") : QStringLiteral("update");
}

qint64 remapOccurrenceId(qint64 occurrenceId, qint64 fromPlan, qint64 toPlan)
{
    if (occurrenceId <= 0 || occurrenceId / kOccurrenceIdBase != fromPlan)
        return occurrenceId;
    return toPlan * kOccurrenceIdBase + occurrenceId % kOccurrenceIdBase;
}

QVariantList remapOccurrences(const QVariantList &list, qint64 fromPlan, qint64 toPlan)
{
    QVariantList out;
    out.reserve(list.size());
    for (const QVariant &entry : list) {
        QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("planId")).toLongLong() == fromPlan) {
            occ[QStringLiteral("planId")] = toPlan;
            occ[QStringLiteral("occurrenceId")] = remapOccurrenceId(
                occ.value(QStringLiteral("occurrenceId")).toLongLong(), fromPlan, toPlan);
        }
        out.append(occ);
    }
    return out;
}

} // namespace

QVariantList AppCore::pendingPlans() const
{
    QVariantList out;
    if (m_demoMode)
        return out;
    for (const QVariant &entry : m_plans) {
        QVariantMap plan = entry.toMap();
        const QString op = planSyncOp(plan);
        if (op.isEmpty())
            continue;
        plan[QStringLiteral("op")] = op;
        out.append(plan);
    }
    return out;
}

int AppCore::pendingPlanCount() const
{
    return pendingPlans().size();
}

QVariantList AppCore::uploadableEvents() const
{
    QSet<qint64> notOnServer;
    for (const QVariant &entry : m_plans) {
        const QVariantMap plan = entry.toMap();
        if (plan.value(QStringLiteral("localOnly")).toBool()
            || plan.value(QStringLiteral("syncRejected")).toBool())
            notOnServer.insert(plan.value(QStringLiteral("planId")).toLongLong());
    }
    const QVariantList pending = pendingEvents();
    if (notOnServer.isEmpty())
        return pending;
    QVariantList out;
    for (const QVariant &entry : pending) {
        const qint64 occurrenceId = entry.toMap().value(QStringLiteral("occurrenceId")).toLongLong();
        if (occurrenceId > 0 && notOnServer.contains(occurrenceId / kOccurrenceIdBase))
            continue;
        out.append(entry);
    }
    return out;
}

void AppCore::markPlanCreated(qint64 localPlanId, qint64 serverPlanId, qint64 rev,
                              qint64 serverVersion)
{
    if (serverPlanId <= 0)
        return;
    int index = indexOfPlan(localPlanId);
    if (index < 0)
        return;

    if (serverPlanId != localPlanId) {
        // 拉取可能抢在回执前把这条新计划当"服务端新计划"拉了下来：去掉那份副本
        const int dup = indexOfPlan(serverPlanId);
        if (dup >= 0) {
            m_plans.removeAt(dup);
            QVariantList kept;
            for (const QVariant &entry : std::as_const(m_occurrences)) {
                if (entry.toMap().value(QStringLiteral("planId")).toLongLong() != serverPlanId)
                    kept.append(entry);
            }
            m_occurrences = kept;
            index = indexOfPlan(localPlanId);
        }

        m_occurrences = remapOccurrences(m_occurrences, localPlanId, serverPlanId);
        for (int i = 0; i < m_events.size(); ++i) {
            QVariantMap event = m_events.at(i).toMap();
            const qint64 oldId = event.value(QStringLiteral("occurrenceId")).toLongLong();
            const qint64 newId = remapOccurrenceId(oldId, localPlanId, serverPlanId);
            if (newId == oldId)
                continue;
            event[QStringLiteral("occurrenceId")] = newId;
            m_events[i] = event;
        }
        if (m_deletedOccurrences.contains(localPlanId))
            m_deletedOccurrences.insert(serverPlanId,
                                        remapOccurrences(m_deletedOccurrences.take(localPlanId),
                                                         localPlanId, serverPlanId));
        if (m_skippedOccurrences.contains(localPlanId))
            m_skippedOccurrences.insert(serverPlanId,
                                        remapOccurrences(m_skippedOccurrences.take(localPlanId),
                                                         localPlanId, serverPlanId));
    }

    QVariantMap plan = m_plans.at(index).toMap();
    plan[QStringLiteral("planId")] = serverPlanId;
    plan.remove(QStringLiteral("localOnly"));
    if (rev > plan.value(QStringLiteral("syncedRev")).toLongLong())
        plan[QStringLiteral("syncedRev")] = rev;
    plan[QStringLiteral("syncedVersion")] = serverVersion > 0 ? serverVersion : 1;
    m_plans[index] = plan;

    logOperation(QStringLiteral("计划"),
                 QStringLiteral("计划已上传：%1（编号 %2）").arg(planLabel(plan)).arg(serverPlanId));
    emit plansChanged();
    emit occurrencesChanged();
    emit currentChanged();
    emit eventsChanged(); // 挂在这条计划上的事件现在可以传了
}

void AppCore::markPlanSynced(qint64 planId, qint64 rev, qint64 serverVersion)
{
    const int index = indexOfPlan(planId);
    if (index < 0)
        return;
    QVariantMap plan = m_plans.at(index).toMap();
    if (rev > plan.value(QStringLiteral("syncedRev")).toLongLong())
        plan[QStringLiteral("syncedRev")] = rev;
    if (serverVersion > plan.value(QStringLiteral("syncedVersion")).toLongLong())
        plan[QStringLiteral("syncedVersion")] = serverVersion;
    m_plans[index] = plan;
    emit plansChanged();
}

void AppCore::rejectPlan(qint64 planId)
{
    const int index = indexOfPlan(planId);
    if (index < 0)
        return;
    QVariantMap plan = m_plans.at(index).toMap();
    plan[QStringLiteral("syncRejected")] = true;
    m_plans[index] = plan;
    emit plansChanged();
}

void AppCore::reconcileServerPlans(const QList<qint64> &serverPlanIds)
{
    if (m_demoMode || serverPlanIds.isEmpty())
        return;
    const QSet<qint64> onServer(serverPlanIds.cbegin(), serverPlanIds.cend());
    bool changed = false;
    for (int i = 0; i < m_plans.size(); ++i) {
        QVariantMap plan = m_plans.at(i).toMap();
        const qint64 planId = plan.value(QStringLiteral("planId")).toLongLong();
        if (planId <= 0 || planId >= kLocalPlanIdBase || onServer.contains(planId)
            || plan.value(QStringLiteral("deleted")).toBool()
            || plan.value(QStringLiteral("localOnly")).toBool()
            || plan.value(QStringLiteral("syncRejected")).toBool())
            continue;
        // 以服务端为准：标删除，本地没传完的改动一并作废（不再 PATCH 去复活它）
        plan[QStringLiteral("deleted")] = true;
        plan[QStringLiteral("syncedRev")] = plan.value(QStringLiteral("syncRev")).toLongLong();
        m_plans[i] = plan;
        logOperation(QStringLiteral("计划"),
                     QStringLiteral("服务端已删除：%1").arg(planLabel(plan)));
        changed = true;
    }
    if (!changed)
        return;
    m_day = clockToday();
    rebuildToday();
    emit plansChanged();
    emit occurrencesChanged();
    emit currentChanged();
}

void AppCore::forgetServerPlan(qint64 planId)
{
    const int index = indexOfPlan(planId);
    if (index < 0)
        return;
    QVariantMap plan = m_plans.at(index).toMap();
    plan[QStringLiteral("localOnly")] = true;
    plan[QStringLiteral("syncedRev")] = 0;
    plan.remove(QStringLiteral("syncedVersion"));
    m_plans[index] = plan;
    emit plansChanged();
}

void AppCore::applyServerPlans(const QVariantList &serverPlans)
{
    bool changed = false;
    for (const QVariant &entry : serverPlans) {
        const QVariantMap server = entry.toMap();
        const qint64 planId = server.value(QStringLiteral("planId")).toLongLong();
        if (planId <= 0)
            continue;
        const int index = indexOfPlan(planId);
        if (index < 0) {
            // 服务端新计划：直接入库；把本地 id 计数器推到它之上，防止
            // 之后本地 createPlan 撞出同一个 planId。
            if (m_nextPlanId <= planId)
                m_nextPlanId = planId + 1;
            m_plans.append(server);
            changed = true;
            continue;
        }
        const QVariantMap local = m_plans.at(index).toMap();
        // 本地改动还没传上去：不让服务端的旧值盖掉，等 PATCH 回执后再对齐
        if (local.value(QStringLiteral("localOnly")).toBool()
            || local.value(QStringLiteral("syncRev")).toLongLong()
                > local.value(QStringLiteral("syncedRev")).toLongLong())
            continue;
        const qint64 serverVersion = server.value(QStringLiteral("version")).toLongLong();
        if (local.contains(QStringLiteral("syncedVersion"))) {
            // 本机上传过的计划：只接受比"已确认版本"新的。同版本回放不覆盖——
            // 云端会把 interval/course 规则降级成 daily、窗口用全局配置，
            // 覆盖回来会改掉老人本机设好的提醒。
            if (serverVersion <= local.value(QStringLiteral("syncedVersion")).toLongLong())
                continue;
            QVariantMap merged = server;
            merged[QStringLiteral("syncedVersion")] = serverVersion;
            m_plans[index] = merged;
            changed = true;
            continue;
        }
        // 规范 §9：冲突以服务端为准。版本高或相等才接受（相等通常就是
        // 同一条回放，覆盖无副作用）。
        if (serverVersion >= local.value(QStringLiteral("version")).toLongLong()) {
            m_plans[index] = server;
            changed = true;
        }
    }
    if (!changed)
        return;
    m_day = clockToday();
    rebuildToday();
    emit plansChanged();
}

void AppCore::applyServerOccurrences(const QVariantList &serverOccurrences)
{
    bool changed = false;
    for (const QVariant &entry : serverOccurrences) {
        const QVariantMap server = entry.toMap();
        const qint64 occurrenceId =
            server.value(QStringLiteral("occurrenceId")).toLongLong();
        const int index = indexOfOccurrence(occurrenceId);
        // 服务端会下发昨天/明天的条目，端上只管今天清单里的
        if (index < 0)
            continue;
        QVariantMap local = m_occurrences.at(index).toMap();
        const QString localState = local.value(QStringLiteral("state")).toString();
        const QString serverState = server.value(QStringLiteral("state")).toString();
        // 本地已终态的不回退：确认/升级是老人亲自做的动作，
        // 服务端旧快照不能把它改回去（missed 是自动迁移，服从服务端）。
        if (localState == QStringLiteral("confirmed")
            || localState == QStringLiteral("escalated"))
            continue;
        if (serverState == localState
            && server.value(QStringLiteral("snoozeCount")).toInt()
                == local.value(QStringLiteral("snoozeCount")).toInt())
            continue;
        local[QStringLiteral("state")] = serverState;
        local[QStringLiteral("snoozeCount")] =
            server.value(QStringLiteral("snoozeCount")).toInt();
        local[QStringLiteral("snoozeUntilTs")] =
            server.value(QStringLiteral("snoozeUntilTs")).toLongLong();
        m_occurrences[index] = local;
        changed = true;
    }
    if (!changed)
        return;
    rebuildToday();
    emit occurrencesChanged();
    emit currentChanged();
}

int AppCore::indexOfOccurrence(qint64 id) const
{
    for (int i = 0; i < m_occurrences.size(); ++i) {
        if (m_occurrences.at(i).toMap().value(QStringLiteral("occurrenceId")).toLongLong() == id)
            return i;
    }
    return -1;
}

int AppCore::indexOfPlan(qint64 id) const
{
    for (int i = 0; i < m_plans.size(); ++i) {
        if (m_plans.at(i).toMap().value(QStringLiteral("planId")).toLongLong() == id)
            return i;
    }
    return -1;
}

QVariantMap AppCore::occurrenceById(qint64 id) const
{
    const int index = indexOfOccurrence(id);
    return index >= 0 ? m_occurrences.at(index).toMap() : QVariantMap();
}

void AppCore::setAutoReminders(bool on)
{
    if (m_autoReminders == on)
        return;
    m_autoReminders = on;
    logOperation(QStringLiteral("设置"),
                 on ? QStringLiteral("打开自动提醒") : QStringLiteral("关闭自动提醒"));
    schedulePersist();
}

void AppCore::resetDemo()
{
    // 只重建演示数据：无论当前在哪个模式，真实数据都不被覆盖。
    const bool wasDemo = m_demoMode;
    const DataSnapshot real = wasDemo ? m_realData : snapshot();

    seedDemo();
    m_demoData = snapshot();

    if (!wasDemo) {
        m_realData = real;
        m_demoMode = true;
        applySnapshot(m_demoData);
        emit demoModeChanged();
    } else {
        applySnapshot(m_demoData);
    }

    logOperation(QStringLiteral("设置"), QStringLiteral("恢复演示数据"));
    emit toast(QStringLiteral("演示数据已恢复"));
}

// ---------------------------------------------------------------------------
// 本地持久化（SQLite）
//
// 设计取舍：只把"真实数据域"落盘，演示域仍是内存快照（重启即回到演示初始态，
// 与 README 里"演示数据不写回真实域"的约定一致）。每次真实域变更后，
// 在同一轮事件循环末尾把整份真实域快照写进本地库，事务内先清表再插入，
// 保证库里永远是"某一时刻的完整状态"，不会出现半截数据。
// ---------------------------------------------------------------------------

void AppCore::openDatabase()
{
    const QString overridePath = qEnvironmentVariable("YIBAN_APP_DB");
    if (!overridePath.isEmpty()) {
        m_dbPath = overridePath;
    } else {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_dbPath = QDir(dir).filePath(QStringLiteral("app_core.sqlite"));
    }

    const QFileInfo info(m_dbPath);
    if (!info.absolutePath().isEmpty())
        QDir().mkpath(info.absolutePath());

    if (QSqlDatabase::contains(kDbConnection))
        QSqlDatabase::removeDatabase(kDbConnection);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kDbConnection);
    db.setDatabaseName(m_dbPath);
    m_dbReady = db.open();
    if (!m_dbReady) {
        qWarning() << "[AppCore] 本地数据库打开失败：" << m_dbPath << db.lastError().text();
        return;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
}

void AppCore::ensureSchema()
{
    if (!m_dbReady)
        return;

    QSqlDatabase db = QSqlDatabase::database(kDbConnection);
    QSqlQuery query(db);
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS meta ("
                       "key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS plans ("
                       "id INTEGER PRIMARY KEY, json TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS occurrences ("
                       "id INTEGER PRIMARY KEY, plan_id INTEGER, day TEXT, state TEXT, "
                       "json TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS records ("
                       "id INTEGER PRIMARY KEY, json TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS events ("
                       "event_id TEXT PRIMARY KEY, occurrence_id INTEGER, action TEXT, ts INTEGER, "
                       "synced INTEGER NOT NULL DEFAULT 0, json TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_occurrences_day ON occurrences(day)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_synced ON events(synced)"),
    };
    for (const QString &sql : statements) {
        if (!query.exec(sql))
            qWarning() << "[AppCore] 建表失败：" << sql << query.lastError().text();
    }
}

void AppCore::schedulePersist()
{
    if (!m_dbReady || m_demoMode || m_persistPending)
        return;
    m_persistPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_persistPending = false;
        persistNow();
    });
}

void AppCore::persistNow()
{
    if (!m_dbReady || m_demoMode)
        return;

    const DataSnapshot data = snapshot();
    QSqlDatabase db = QSqlDatabase::database(kDbConnection);
    if (!db.isOpen() || !db.transaction())
        return;

    QSqlQuery query(db);
    bool ok = query.exec(QStringLiteral("DELETE FROM plans"))
              && query.exec(QStringLiteral("DELETE FROM occurrences"))
              && query.exec(QStringLiteral("DELETE FROM records"))
              && query.exec(QStringLiteral("DELETE FROM events"));

    const auto insertPlan = [&](const QVariantMap &plan) {
        query.prepare(QStringLiteral("INSERT INTO plans (id, json) VALUES (?, ?)"));
        query.addBindValue(plan.value(QStringLiteral("planId")).toLongLong());
        query.addBindValue(variantMapToJson(plan));
        return query.exec();
    };
    const auto insertOccurrence = [&](const QVariantMap &occ) {
        query.prepare(QStringLiteral(
            "INSERT INTO occurrences (id, plan_id, day, state, json) VALUES (?, ?, ?, ?, ?)"));
        query.addBindValue(occ.value(QStringLiteral("occurrenceId")).toLongLong());
        query.addBindValue(occ.value(QStringLiteral("planId")).toLongLong());
        query.addBindValue(data.day.isValid() ? data.day.toString(Qt::ISODate) : QString());
        query.addBindValue(occ.value(QStringLiteral("state")).toString());
        query.addBindValue(variantMapToJson(occ));
        return query.exec();
    };
    const auto insertRecord = [&](const QVariantMap &record) {
        query.prepare(QStringLiteral("INSERT INTO records (id, json) VALUES (?, ?)"));
        query.addBindValue(record.value(QStringLiteral("recordId")).toLongLong());
        query.addBindValue(variantMapToJson(record));
        return query.exec();
    };
    const auto insertEvent = [&](const QVariantMap &event) {
        query.prepare(QStringLiteral(
            "INSERT INTO events (event_id, occurrence_id, action, ts, synced, json) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        query.addBindValue(event.value(QStringLiteral("eventId")).toString());
        query.addBindValue(event.value(QStringLiteral("occurrenceId")).toLongLong());
        query.addBindValue(event.value(QStringLiteral("action")).toString());
        query.addBindValue(event.value(QStringLiteral("ts")).toLongLong());
        // serverSeq：0 = 待上传，>0 = 服务端已收，-1 = 服务端已拒绝（同样不再上传）
        query.addBindValue(event.value(QStringLiteral("serverSeq")).toLongLong() != 0 ? 1 : 0);
        query.addBindValue(variantMapToJson(event));
        return query.exec();
    };

    // 事件裁剪：待上传（serverSeq == 0）全留；已处理（ack / 拒绝）只留最近
    // kMaxSyncedEvents 条。修掉"重启一次就删掉最早事件、连没上传的一起丢"的问题。
    int handledCount = 0;
    for (const QVariant &entry : data.events) {
        if (entry.toMap().value(QStringLiteral("serverSeq")).toLongLong() != 0)
            ++handledCount;
    }
    int handledToSkip = qMax(0, handledCount - kMaxSyncedEvents);
    QVariantList eventsToPersist;
    for (const QVariant &entry : data.events) {
        const bool handled = entry.toMap().value(QStringLiteral("serverSeq")).toLongLong() != 0;
        if (handled && handledToSkip > 0) {
            --handledToSkip;
            continue;
        }
        eventsToPersist.append(entry);
    }

    if (ok) {
        for (const QVariant &entry : data.plans)
            ok = insertPlan(entry.toMap()) && ok;
        for (const QVariant &entry : data.occurrences)
            ok = insertOccurrence(entry.toMap()) && ok;
        for (const QVariant &entry : data.records)
            ok = insertRecord(entry.toMap()) && ok;
        for (const QVariant &entry : eventsToPersist)
            ok = insertEvent(entry.toMap()) && ok;
    }

    const auto setMeta = [&](const QString &key, const QString &value) {
        query.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"));
        query.addBindValue(key);
        query.addBindValue(value);
        return query.exec();
    };
    if (ok) {
        ok = setMeta(QStringLiteral("schema_version"), QStringLiteral("1"))
             && setMeta(QStringLiteral("next_plan_id"), QString::number(data.nextPlanId))
             && setMeta(QStringLiteral("next_record_id"), QString::number(data.nextRecordId))
             && setMeta(QStringLiteral("next_event_seq"), QString::number(data.nextEventSeq))
             && setMeta(QStringLiteral("elder_name"), data.elderName)
             && setMeta(QStringLiteral("caregiver_name"), data.caregiverName)
             && setMeta(QStringLiteral("caregiver_phone"), data.caregiverPhone)
             && setMeta(QStringLiteral("day"), data.day.toString(Qt::ISODate))
             && setMeta(QStringLiteral("start_date"), data.startDate.toString(Qt::ISODate))
             && setMeta(QStringLiteral("auto_reminders"),
                        m_autoReminders ? QStringLiteral("1") : QStringLiteral("0"))
             && setMeta(QStringLiteral("pause_until_ms"), QString::number(m_pauseUntilMs))
             && setMeta(QStringLiteral("history"),
                        QString::fromUtf8(variantMapToJson(data.history)))
             && setMeta(QStringLiteral("deleted_occurrences"),
                        QString::fromUtf8(variantListHashToJson(data.deletedOccurrences)))
             && setMeta(QStringLiteral("skipped_occurrences"),
                        QString::fromUtf8(variantListHashToJson(data.skippedOccurrences)))
             && setMeta(QStringLiteral("saved_at_ms"),
                        QString::number(QDateTime::currentMSecsSinceEpoch()));
    }

    if (!ok) {
        qWarning() << "[AppCore] 写入本地库失败：" << query.lastError().text();
        db.rollback();
        return;
    }
    db.commit();
}

void AppCore::clearDatabase()
{
    if (!m_dbReady)
        return;
    QSqlDatabase db = QSqlDatabase::database(kDbConnection);
    QSqlQuery query(db);
    query.exec(QStringLiteral("DELETE FROM plans"));
    query.exec(QStringLiteral("DELETE FROM occurrences"));
    query.exec(QStringLiteral("DELETE FROM records"));
    query.exec(QStringLiteral("DELETE FROM events"));
    query.exec(QStringLiteral("DELETE FROM meta"));
}

bool AppCore::loadRealData(DataSnapshot *data)
{
    if (!m_dbReady || !data)
        return false;

    QSqlDatabase db = QSqlDatabase::database(kDbConnection);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT key, value FROM meta")))
        return false;

    QHash<QString, QString> meta;
    while (query.next())
        meta.insert(query.value(0).toString(), query.value(1).toString());
    if (meta.isEmpty())
        return false;

    DataSnapshot loaded;
    loaded.nextPlanId = meta.value(QStringLiteral("next_plan_id"), QStringLiteral("1")).toLongLong();
    loaded.nextRecordId =
        meta.value(QStringLiteral("next_record_id"), QStringLiteral("1")).toLongLong();
    loaded.nextEventSeq =
        meta.value(QStringLiteral("next_event_seq"), QStringLiteral("1")).toLongLong();
    loaded.elderName = meta.value(QStringLiteral("elder_name"));
    loaded.caregiverName = meta.value(QStringLiteral("caregiver_name"));
    loaded.caregiverPhone = meta.value(QStringLiteral("caregiver_phone"));
    loaded.day = QDate::fromString(meta.value(QStringLiteral("day")), Qt::ISODate);
    loaded.startDate = QDate::fromString(meta.value(QStringLiteral("start_date")), Qt::ISODate);
    loaded.history = jsonToVariantMap(meta.value(QStringLiteral("history")).toUtf8());
    loaded.deletedOccurrences =
        jsonToVariantListHash(meta.value(QStringLiteral("deleted_occurrences")).toUtf8());
    loaded.skippedOccurrences =
        jsonToVariantListHash(meta.value(QStringLiteral("skipped_occurrences")).toUtf8());
    m_autoReminders = meta.value(QStringLiteral("auto_reminders"), QStringLiteral("1"))
                      != QStringLiteral("0");
    m_pauseUntilMs = meta.value(QStringLiteral("pause_until_ms"), QStringLiteral("0")).toLongLong();
    // 已经到期的暂停视为自动恢复：不留下"暂停中"的假状态。
    if (m_pauseUntilMs > 0 && m_pauseUntilMs <= clockNowMs())
        m_pauseUntilMs = 0;

    if (!query.exec(QStringLiteral("SELECT json FROM plans ORDER BY id")))
        return false;
    while (query.next())
        loaded.plans.append(jsonToVariantMap(query.value(0).toByteArray()));

    if (!query.exec(QStringLiteral("SELECT json FROM occurrences ORDER BY id")))
        return false;
    while (query.next())
        loaded.occurrences.append(jsonToVariantMap(query.value(0).toByteArray()));

    if (!query.exec(QStringLiteral("SELECT json FROM records ORDER BY id")))
        return false;
    while (query.next())
        loaded.records.append(jsonToVariantMap(query.value(0).toByteArray()));

    // 事件全量读回：写入时已裁剪（待上传全留 + 最近 800 条已处理），不再 LIMIT 截断。
    if (!query.exec(QStringLiteral("SELECT json FROM events ORDER BY ts")))
        return false;
    while (query.next())
        loaded.events.append(jsonToVariantMap(query.value(0).toByteArray()));

    *data = loaded;
    return true;
}

void AppCore::resetStorage()
{
    clearDatabase();
    m_demoMode = false;
    m_pauseUntilMs = 0;
    clearActive();                 // 真实域清空
    m_realData = snapshot();
    seedDemo();                    // 演示域重建
    m_demoData = snapshot();
    applySnapshot(m_realData);     // 当前回到空的真实域

    emit plansChanged();
    emit recordsChanged();
    emit profileChanged();
    emit occurrencesChanged();
}
