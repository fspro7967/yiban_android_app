#include "log_vm.h"

#include "app_core.h"

LogVM::LogVM(QObject *parent)
    : QObject(parent)
{
    connect(AppCore::instance(), &AppCore::operationLogChanged, this, &LogVM::entriesChanged);
}

QVariantList LogVM::entries() const
{
    return AppCore::instance()->operationLog();
}

int LogVM::count() const
{
    return AppCore::instance()->operationLogSize();
}

void LogVM::note(const QString &category, const QString &text)
{
    AppCore::instance()->logOperation(category, text);
}

void LogVM::clear()
{
    AppCore::instance()->clearOperationLog();
}
