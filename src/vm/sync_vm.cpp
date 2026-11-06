#include "sync_vm.h"

#include "app_core.h"
#include "facade_errors.h"

SyncVM::SyncVM(QObject *parent)
    : QObject(parent)
{
    AppCore *core = AppCore::instance();
    connect(core, &AppCore::occurrencesChanged, this, &SyncVM::syncChanged);
    connect(core, &AppCore::toast, this, &SyncVM::toast);
}

QString SyncVM::syncState() const
{
    return QStringLiteral("offline");
}

qint64 SyncVM::lastSyncTs() const
{
    return 0;
}

int SyncVM::pendingCount() const
{
    return AppCore::instance()->eventLog().size();
}

void SyncVM::retrySync()
{
    emit commandRejected(QStringLiteral("offline"),
                         facadeErrorText(QStringLiteral("offline")));
}

QString SyncVM::eventLogCsv() const
{
    return AppCore::instance()->eventLogCsv();
}
