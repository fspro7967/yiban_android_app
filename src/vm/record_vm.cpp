#include "record_vm.h"

#include "app_core.h"
#include "facade_errors.h"

namespace {

QString durationText(int totalMinutes)
{
    const int h = totalMinutes / 60;
    const int m = totalMinutes % 60;
    if (h <= 0)
        return QStringLiteral("%1 分钟").arg(m);
    if (m == 0)
        return QStringLiteral("%1 小时").arg(h);
    return QStringLiteral("%1 小时 %2 分").arg(h).arg(m);
}

} // namespace

RecordVM::RecordVM(QObject *parent)
    : QObject(parent)
{
    AppCore *core = AppCore::instance();
    connect(core, &AppCore::recordsChanged, this, &RecordVM::recordsChanged);
    connect(core, &AppCore::toast, this, &RecordVM::toast);
}

QVariantList RecordVM::records() const
{
    return AppCore::instance()->records();
}

QVariantList RecordVM::stats7() const
{
    return AppCore::instance()->stats(7);
}

QVariantList RecordVM::stats30() const
{
    return AppCore::instance()->stats(30);
}

int RecordVM::waterCups() const
{
    return AppCore::instance()->waterCups();
}

int RecordVM::waterGoal() const
{
    return AppCore::instance()->waterGoal();
}

QString RecordVM::sleepStart() const
{
    return AppCore::instance()->sleepStartText();
}

QString RecordVM::sleepEnd() const
{
    return AppCore::instance()->sleepEndText();
}

int RecordVM::sleepMinutes() const
{
    return AppCore::instance()->sleepMinutes();
}

int RecordVM::activityMinutes() const
{
    return AppCore::instance()->activityMinutes();
}

qint64 RecordVM::addWater()
{
    AppCore *core = AppCore::instance();
    if (core->waterCups() >= core->waterGoal()) {
        emit commandRejected(QStringLiteral("water_goal_reached"),
                             facadeErrorText(QStringLiteral("water_goal_reached")));
        return 0;
    }
    const qint64 id = core->addRecord(QStringLiteral("water"), 1, QStringLiteral("杯"));
    emit recordsChanged();
    emit toast(QStringLiteral("今天第 %1 杯水，慢慢喝").arg(core->waterCups()));
    return id;
}

qint64 RecordVM::markSleepStart()
{
    const qint64 id = AppCore::instance()->addRecord(QStringLiteral("sleep_start"), 0, QString());
    emit recordsChanged();
    emit toast(QStringLiteral("已记下：现在开始睡觉"));
    return id;
}

qint64 RecordVM::markSleepEnd()
{
    AppCore *core = AppCore::instance();
    const qint64 id = core->addRecord(QStringLiteral("sleep_end"), 0, QString());
    emit recordsChanged();
    emit toast(QStringLiteral("已记下：昨晚睡了 %1").arg(durationText(core->sleepMinutes())));
    return id;
}

qint64 RecordVM::addActivity(const QString &kind)
{
    int minutes = 20;
    if (kind == QStringLiteral("散步"))
        minutes = 15;
    else if (kind == QStringLiteral("家务"))
        minutes = 10;

    const qint64 id = AppCore::instance()->addRecord(QStringLiteral("activity"), minutes,
                                                     QStringLiteral("分钟"));
    emit recordsChanged();
    emit toast(QStringLiteral("已记下：%1 %2 分钟").arg(kind).arg(minutes));
    return id;
}

qint64 RecordVM::addRecord(const QString &type, double value, const QString &unit)
{
    const qint64 id = AppCore::instance()->addRecord(type, value, unit);
    emit recordsChanged();
    return id;
}

bool RecordVM::correctRecord(qint64 recordId, const QVariantMap &patch)
{
    QString error;
    if (!AppCore::instance()->correctRecord(recordId, patch, &error)) {
        emit commandRejected(error, facadeErrorText(error));
        return false;
    }
    emit recordsChanged();
    emit toast(QStringLiteral("已修正"));
    return true;
}

bool RecordVM::removeRecord(qint64 recordId)
{
    if (!AppCore::instance()->removeRecord(recordId))
        return false;
    emit recordsChanged();
    emit toast(QStringLiteral("已撤销"));
    return true;
}
