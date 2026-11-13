#include "platform.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace Platform {
namespace {

#ifdef Q_OS_ANDROID
constexpr auto kBridge = "com/yiban/elder/YibanPlatform";

QJniObject activityContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

const char *permissionName(Permission p)
{
    switch (p) {
    case Permission::Notifications:
        return "android.permission.POST_NOTIFICATIONS";
    case Permission::ExactAlarm:
        return "android.permission.SCHEDULE_EXACT_ALARM";
    case Permission::Phone:
        return "android.permission.CALL_PHONE";
    case Permission::Sms:
        return "android.permission.SEND_SMS";
    case Permission::Microphone:
        return "android.permission.RECORD_AUDIO";
    }
    return "";
}
#endif

} // namespace

bool isAndroid()
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

bool hasPermission(Permission p)
{
#ifdef Q_OS_ANDROID
    if (p == Permission::ExactAlarm)
        return canScheduleExactAlarms();

    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;

    QJniObject name = QJniObject::fromString(QString::fromLatin1(permissionName(p)));
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "hasPermission", "(Landroid/content/Context;Ljava/lang/String;)Z",
        context.object(), name.object());
#else
    Q_UNUSED(p)
    return false;
#endif
}

void requestPermission(Permission p)
{
#ifdef Q_OS_ANDROID
    if (p == Permission::ExactAlarm) {
        openExactAlarmSettings();
        return;
    }

    const QJniObject context = activityContext();
    if (!context.isValid())
        return;

    QJniObject name = QJniObject::fromString(QString::fromLatin1(permissionName(p)));
    // requestCode 只用于系统回调配对，QtActivity 会把它交给 Qt 的权限队列；
    // 我们不用回调，授权结果下次轮询 hasPermission 时自然读到。
    QJniObject::callStaticMethod<void>(
        kBridge, "requestPermission", "(Landroid/content/Context;Ljava/lang/String;I)V",
        context.object(), name.object(), jint(4101));
#else
    Q_UNUSED(p)
#endif
}

QStringList grantedLabels()
{
#ifdef Q_OS_ANDROID
    QStringList labels;
    if (hasPermission(Permission::Notifications))
        labels << QStringLiteral("通知");
    if (hasPermission(Permission::ExactAlarm))
        labels << QStringLiteral("精确闹钟");
    if (hasPermission(Permission::Phone))
        labels << QStringLiteral("电话");
    if (hasPermission(Permission::Sms))
        labels << QStringLiteral("短信");
    if (hasPermission(Permission::Microphone))
        labels << QStringLiteral("麦克风");
    return labels;
#else
    // 桌面没有权限模型：按 demo 口径全给，QML 不需要分支。
    return {QStringLiteral("通知"), QStringLiteral("麦克风"), QStringLiteral("电话")};
#endif
}

bool canScheduleExactAlarms()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "canScheduleExactAlarms", "(Landroid/content/Context;)Z", context.object());
#else
    return true;
#endif
}

void openExactAlarmSettings()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "openExactAlarmSettings",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

void requestIgnoreBatteryOptimizations()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "requestIgnoreBatteryOptimizations",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

void notify(int id, const QString &title, const QString &text, bool fullScreen)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;

    QJniObject jTitle = QJniObject::fromString(title);
    QJniObject jText = QJniObject::fromString(text);
    QJniObject::callStaticMethod<void>(
        kBridge, "showNotification",
        "(Landroid/content/Context;ILjava/lang/String;Ljava/lang/String;Z)V",
        context.object(), jint(id), jTitle.object(), jText.object(), jboolean(fullScreen));
#else
    Q_UNUSED(id)
    Q_UNUSED(title)
    Q_UNUSED(text)
    Q_UNUSED(fullScreen)
#endif
}

void cancelNotification(int id)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "cancelNotification",
                                       "(Landroid/content/Context;I)V", context.object(),
                                       jint(id));
#else
    Q_UNUSED(id)
#endif
}

bool scheduleAlarm(qint64 occurrenceId, qint64 triggerAtMs,
                   const QString &title, const QString &text)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;

    QJniObject jTitle = QJniObject::fromString(title);
    QJniObject jText = QJniObject::fromString(text);
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "scheduleAlarm",
        "(Landroid/content/Context;JJLjava/lang/String;Ljava/lang/String;)Z",
        context.object(), jlong(occurrenceId), jlong(triggerAtMs), jTitle.object(),
        jText.object());
#else
    Q_UNUSED(occurrenceId)
    Q_UNUSED(triggerAtMs)
    Q_UNUSED(title)
    Q_UNUSED(text)
    return false;
#endif
}

void cancelAlarm(qint64 occurrenceId)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "cancelAlarm",
                                       "(Landroid/content/Context;J)V", context.object(),
                                       jlong(occurrenceId));
#else
    Q_UNUSED(occurrenceId)
#endif
}

void cancelAllAlarms()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "cancelAllAlarms",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

bool call(const QString &number)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid() || number.trimmed().isEmpty())
        return false;

    QJniObject jNumber = QJniObject::fromString(number);
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "call", "(Landroid/content/Context;Ljava/lang/String;)Z",
        context.object(), jNumber.object());
#else
    Q_UNUSED(number)
    return false;
#endif
}

bool sendSms(const QString &number, const QString &text)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid() || number.trimmed().isEmpty())
        return false;

    QJniObject jNumber = QJniObject::fromString(number);
    QJniObject jText = QJniObject::fromString(text);
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "sendSms", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        context.object(), jNumber.object(), jText.object());
#else
    Q_UNUSED(number)
    Q_UNUSED(text)
    return false;
#endif
}

bool enterKiosk()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    QJniObject::callStaticMethod<void>(kBridge, "startLockTask",
                                       "(Landroid/content/Context;)V", context.object());
    return true;
#else
    return false;
#endif
}

void exitKiosk()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "stopLockTask",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

} // namespace Platform
