#include "reminder_vm.h"

#include "app_core.h"
#include "facade_errors.h"

ReminderVM::ReminderVM(QObject *parent)
    : QObject(parent)
{
    AppCore *core = AppCore::instance();
    connect(core, &AppCore::occurrencesChanged, this, &ReminderVM::todayChanged);
    connect(core, &AppCore::occurrencesChanged, this, &ReminderVM::currentChanged);
    connect(core, &AppCore::currentChanged, this, &ReminderVM::currentChanged);
    connect(core, &AppCore::nowChanged, this, &ReminderVM::nowChanged);
    connect(core, &AppCore::reminderDue, this, &ReminderVM::reminderDue);
    connect(core, &AppCore::toast, this, &ReminderVM::toast);
}

QVariantList ReminderVM::today() const
{
    return AppCore::instance()->occurrencesToday();
}

QVariantMap ReminderVM::current() const
{
    return AppCore::instance()->currentOccurrence();
}

QVariantMap ReminderVM::next() const
{
    return AppCore::instance()->nextUpcomingOccurrence();
}

QVariantList ReminderVM::upcoming() const
{
    return AppCore::instance()->upcomingOccurrences();
}

int ReminderVM::todayDone() const
{
    return AppCore::instance()->todayDone();
}

int ReminderVM::todayTotal() const
{
    return AppCore::instance()->todayTotal();
}

int ReminderVM::pendingCount() const
{
    return AppCore::instance()->pendingCount();
}

qint64 ReminderVM::nowMs() const
{
    return AppCore::instance()->nowMs();
}

int ReminderVM::snoozeRemaining() const
{
    const qint64 id = m_focusedId != 0
        ? m_focusedId
        : current().value(QStringLiteral("occurrenceId")).toLongLong();
    if (id == 0)
        return 3;
    for (const QVariant &entry : today()) {
        const QVariantMap occ = entry.toMap();
        if (occ.value(QStringLiteral("occurrenceId")).toLongLong() == id)
            return occ.value(QStringLiteral("snoozeRemaining")).toInt();
    }
    return 3;
}

bool ReminderVM::autoReminders() const
{
    return AppCore::instance()->autoReminders();
}

void ReminderVM::setAutoReminders(bool on)
{
    if (autoReminders() == on)
        return;
    AppCore::instance()->setAutoReminders(on);
    emit autoRemindersChanged();
}

void ReminderVM::confirm(qint64 occurrenceId)
{
    QString error;
    if (!AppCore::instance()->confirm(occurrenceId, &error))
        emit commandRejected(error, facadeErrorText(error));
}

void ReminderVM::snooze(qint64 occurrenceId)
{
    QString error;
    if (!AppCore::instance()->snooze(occurrenceId, &error))
        emit commandRejected(error, facadeErrorText(error));
}

void ReminderVM::undo(qint64 occurrenceId)
{
    QString error;
    if (!AppCore::instance()->undoOccurrence(occurrenceId, &error))
        emit commandRejected(error, facadeErrorText(error));
}

void ReminderVM::focus(qint64 occurrenceId)
{
    if (m_focusedId == occurrenceId)
        return;
    m_focusedId = occurrenceId;
    emit currentChanged();
}

void ReminderVM::escalate(qint64 occurrenceId)
{
    QString error;
    if (!AppCore::instance()->escalate(occurrenceId, &error))
        emit commandRejected(error, facadeErrorText(error));
}

void ReminderVM::sos()
{
    AppCore::instance()->sos();
}

void ReminderVM::triggerDemoReminder()
{
    AppCore::instance()->triggerDemoReminder();
}
