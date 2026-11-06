#include "profile_vm.h"

#include "app_core.h"

ProfileVM::ProfileVM(QObject *parent)
    : QObject(parent)
{
    connect(AppCore::instance(), &AppCore::profileChanged, this, &ProfileVM::profileChanged);
}

QString ProfileVM::elderName() const
{
    return AppCore::instance()->elderName();
}

QString ProfileVM::caregiverName() const
{
    return AppCore::instance()->caregiverName();
}

QString ProfileVM::caregiverPhone() const
{
    return AppCore::instance()->caregiverPhone();
}

void ProfileVM::applyProfile(const QString &elderName, const QString &caregiverName,
                             const QString &caregiverPhone)
{
    AppCore::instance()->setProfile(elderName, caregiverName, caregiverPhone);
}
