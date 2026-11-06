#include "app_core.h"

#include <QDateTime>
#include <QTime>
#include <QHash>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <climits>
#include <utility>

namespace {

constexpr qint64 kOccurrenceIdBase = 1000000000LL;
constexpr qint64 kSnoozeMinutes = 10;
constexpr int kMaxSnooze = 3;
constexpr int kDefaultWindowMin = 30;
constexpr int kDefaultEscalateMin = 15;

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

} // namespace

AppCore::AppCore(QObject *parent)
    : QObject(parent)
{
    m_day = QDate::currentDate();
    m_nowMs = QDateTime::currentMSecsSinceEpoch();

    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &AppCore::onTick);
    m_tick.start();

    seedDemo();
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

    m_elderName = QStringLiteral("陈奶奶");
    m_caregiverName = QStringLiteral("女儿 陈晓");
    m_caregiverPhone = QStringLiteral("138 0000 5678");

    struct SeedPlan {
        const char *title;
        const char *dose;
        const char *note;
        const char *time;
    };
    const SeedPlan seeds[] = {
        {"晨起喝水", "温水 1 杯", "小口慢慢喝", "07:00"},
        {"吃降压药", "苯磺酸氨氯地平 1 片", "早饭后，温水送服", "08:00"},
        {"吃降糖药", "二甲双胍 1 片", "随午餐服用", "12:00"},
        {"测血压", "坐稳后测量，记录数值", "测量前安静休息 5 分钟", "18:00"},
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

void AppCore::setProfile(const QString &elderName, const QString &caregiverName,
                         const QString &caregiverPhone)
{
    m_elderName = elderName;
    m_caregiverName = caregiverName;
    m_caregiverPhone = caregiverPhone;
    emit profileChanged();
}

void AppCore::onTick()
{
    const QDate today = QDate::currentDate();
    if (today != m_day) {
        m_day = today;
        rebuildToday();
    }

    m_nowMs = QDateTime::currentMSecsSinceEpoch();
    emit nowChanged();

    bool changed = false;
    QList<qint64> toFire;
    QList<qint64> toMiss;

    const int nowMinutes = QTime::currentTime().hour() * 60 + QTime::currentTime().minute();
    for (const QVariant &entry : std::as_const(m_occurrences)) {
        const QVariantMap occ = entry.toMap();
        const QString state = occ.value(QStringLiteral("state")).toString();
        const qint64 id = occ.value(QStringLiteral("occurrenceId")).toLongLong();

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
        changed = true;
    }

    if (changed) {
        emit occurrencesChanged();
        emit currentChanged();
    }
}

void AppCore::rebuildToday()
{
    // 保留同 id occurrence 的既有状态：新建/删除计划只重排清单，不重置今天的进度。
    QHash<qint64, QVariantMap> previous;
    for (const QVariant &entry : std::as_const(m_occurrences))
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
            if (const auto it = previous.constFind(occurrenceId); it != previous.constEnd()) {
                occ[QStringLiteral("state")] = it->value(QStringLiteral("state"));
                occ[QStringLiteral("snoozeCount")] = it->value(QStringLiteral("snoozeCount"));
                occ[QStringLiteral("snoozeUntilTs")] = it->value(QStringLiteral("snoozeUntilTs"));
            }
            m_occurrences.append(occ);
        }
    }
    emit occurrencesChanged();
    emit currentChanged();
}

bool AppCore::planFiresOn(const QVariantMap &plan, const QDate &day)
{
    const QVariantMap rule = plan.value(QStringLiteral("rule")).toMap();
    const QString kind = normalizeKind(rule);
    if (kind.isEmpty())
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
    out[QStringLiteral("canConfirm")] = state != QStringLiteral("confirmed");
    out[QStringLiteral("canSnooze")] = state != QStringLiteral("confirmed")
        && snoozeCount < kMaxSnooze;
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
        out.append(publicOccurrence(m_plans.at(planIndex).toMap(), occ));
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

    const int nowMinutes = QTime::currentTime().hour() * 60 + QTime::currentTime().minute();
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
            best = planIndex >= 0 ? publicOccurrence(m_plans.at(planIndex).toMap(), occ) : occ;
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
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("fire"), QString());

    const int planIndex = indexOfPlan(occ.value(QStringLiteral("planId")).toLongLong());
    if (planIndex >= 0)
        emit reminderDue(publicOccurrence(m_plans.at(planIndex).toMap(), occ));
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

    const QString title = occ.value(QStringLiteral("title")).toString();
    const QString time = hhmmFromMs(occ.value(QStringLiteral("dueTs")).toLongLong());
    occ[QStringLiteral("state")] = QStringLiteral("confirmed");
    occ[QStringLiteral("snoozeUntilTs")] = 0;
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("confirm"), QString());

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
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("snooze"), QString());

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
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("undo"), QString());

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
    m_occurrences[index] = occ;
    appendEvent(occurrenceId, QStringLiteral("escalate"), QStringLiteral("timeout"));

    emit occurrencesChanged();
    emit currentChanged();
    emit toast(QStringLiteral("已通知 %1：%2 还没有确认")
                   .arg(m_caregiverName, occ.value(QStringLiteral("title")).toString()));
    return true;
}

void AppCore::sos()
{
    appendEvent(0, QStringLiteral("escalate"), QStringLiteral("sos"));
    emit toast(QStringLiteral("已通知 %1").arg(m_caregiverName));
}

void AppCore::triggerDemoReminder()
{
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
        if (entry.toMap().value(QStringLiteral("state")).toString()
            == QStringLiteral("confirmed"))
            ++n;
    }
    return n;
}

int AppCore::todayTotal() const
{
    return m_occurrences.size();
}

int AppCore::pendingCount() const
{
    int n = 0;
    for (const QVariant &entry : m_occurrences) {
        if (entry.toMap().value(QStringLiteral("state")).toString()
            != QStringLiteral("confirmed"))
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
    stored[QStringLiteral("planId")] = m_nextPlanId++;
    stored[QStringLiteral("windowMin")] =
        plan.value(QStringLiteral("windowMin"), kDefaultWindowMin).toInt();
    stored[QStringLiteral("escalateAfterMin")] =
        plan.value(QStringLiteral("escalateAfterMin"), kDefaultEscalateMin).toInt();
    stored[QStringLiteral("version")] = 1;
    stored[QStringLiteral("deleted")] = false;
    m_plans.append(stored);

    m_day = QDate::currentDate();
    rebuildToday();
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
    if (index < 0) {
        if (error)
            *error = QStringLiteral("plan_not_found");
        return false;
    }
    QVariantMap stored = m_plans.at(index).toMap();
    for (auto it = plan.constBegin(); it != plan.constEnd(); ++it)
        stored.insert(it.key(), it.value());
    stored[QStringLiteral("planId")] = planId;
    stored[QStringLiteral("version")] = stored.value(QStringLiteral("version")).toInt() + 1;
    m_plans[index] = stored;

    m_day = QDate::currentDate();
    rebuildToday();
    emit plansChanged();
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
    m_plans[index] = stored;

    m_day = QDate::currentDate();
    rebuildToday();
    emit plansChanged();
    emit toast(QStringLiteral("已删除这条提醒"));
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
            if (it.key() == QStringLiteral("recordId") || it.key() == QStringLiteral("type"))
                continue;
            rec.insert(it.key(), it.value());
        }
        rec[QStringLiteral("correctedFrom")] = recordId;
        m_records[i] = rec;
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
        m_records.removeAt(i);
        emit recordsChanged();
        return true;
    }
    return false;
}

int AppCore::waterCups() const
{
    const qint64 today0 = midnightMs(QDate::currentDate());
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
    const qint64 today0 = midnightMs(QDate::currentDate());
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

QVariantList AppCore::stats(int days) const
{
    static const QList<int> base7 = {50, 50, 100, 100, 100, 100, 100};
    static const QList<int> base30 = {50, 100, 75, 100, 100, 50, 100, 100, 75, 100};
    const QList<int> &base = days == 7 ? base7 : base30;
    const QDate today = QDate::currentDate();

    QVariantList out;
    for (int i = days - 1; i >= 0; --i) {
        const QDate day = today.addDays(-i);
        const int rate = base.at((days - 1 - i) % base.size());
        const int total = 4;
        const int done = qRound(rate * total / 100.0);
        const bool showLabel = days == 7 || i == days - 1 || i == 0 || i % 5 == 0;

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
        item[QStringLiteral("missed")] = total - done;
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
}

void AppCore::resetDemo()
{
    seedDemo();
    emit toast(QStringLiteral("演示数据已恢复"));
}
