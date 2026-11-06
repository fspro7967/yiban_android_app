#include "plan_vm.h"

#include "app_core.h"
#include "facade_errors.h"

PlanVM::PlanVM(QObject *parent)
    : QObject(parent)
{
    AppCore *core = AppCore::instance();
    connect(core, &AppCore::plansChanged, this, &PlanVM::plansChanged);
    connect(core, &AppCore::occurrencesChanged, this, &PlanVM::plansChanged);
    connect(core, &AppCore::toast, this, &PlanVM::toast);
}

QVariantList PlanVM::plans() const
{
    return AppCore::instance()->plans();
}

QVariantMap PlanVM::todaySummary() const
{
    int expected = 0;
    int confirmed = 0;
    int missed = 0;
    for (const QVariant &entry : AppCore::instance()->occurrencesToday()) {
        const QString state = entry.toMap().value(QStringLiteral("state")).toString();
        ++expected;
        if (state == QStringLiteral("confirmed"))
            ++confirmed;
        else if (state == QStringLiteral("missed"))
            ++missed;
    }
    QVariantMap out;
    out[QStringLiteral("expected")] = expected;
    out[QStringLiteral("confirmed")] = confirmed;
    out[QStringLiteral("missed")] = missed;
    return out;
}

bool PlanVM::createPlan(const QVariantMap &plan)
{
    QString error;
    if (AppCore::instance()->createPlan(plan, &error) == 0) {
        emit commandRejected(error, facadeErrorText(error));
        return false;
    }
    emit plansChanged();
    return true;
}

bool PlanVM::updatePlan(qint64 planId, const QVariantMap &patch)
{
    QString error;
    if (!AppCore::instance()->updatePlan(planId, patch, &error)) {
        emit commandRejected(error, facadeErrorText(error));
        return false;
    }
    emit plansChanged();
    return true;
}

bool PlanVM::deletePlan(qint64 planId)
{
    QString error;
    if (!AppCore::instance()->deletePlan(planId, &error)) {
        emit commandRejected(error, facadeErrorText(error));
        return false;
    }
    emit plansChanged();
    return true;
}
