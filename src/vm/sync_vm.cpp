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
    AppCore::instance()->logOperation(
        QStringLiteral("管理"),
        QStringLiteral("手动同步：当前离线，%1 条记录待上传").arg(pendingCount()));
    emit commandRejected(QStringLiteral("offline"),
                         facadeErrorText(QStringLiteral("offline")));
}

QString SyncVM::eventLogCsv() const
{
    return AppCore::instance()->eventLogCsv();
}
