#include "profile_vm.h"

#include "app_core.h"

#include <QRegularExpression>

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

bool ProfileVM::updateCaregiver(const QString &name, const QString &phone)
{
    const QString trimmedName = name.trimmed();
    const QString trimmedPhone = phone.trimmed();
    const QString digits = trimmedPhone;
    static const QRegularExpression phonePattern(QStringLiteral("^[0-9 +()-]{7,20}$"));

    if (trimmedName.isEmpty() || !phonePattern.match(digits).hasMatch())
        return false;

    AppCore::instance()->setProfile(AppCore::instance()->elderName(), trimmedName, trimmedPhone);
    return true;
}
