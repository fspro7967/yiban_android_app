#include "platform.h"

#include <QVariantMap>

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
    case Permission::FullScreenIntent:
        // 不是一个可申请的运行时权限，走 canUseFullScreenIntent 单独判断
        return "";
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
    if (p == Permission::FullScreenIntent)
        return canUseFullScreenIntent();

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
    if (p == Permission::FullScreenIntent) {
        openFullScreenIntentSettings();
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
    if (hasPermission(Permission::FullScreenIntent))
        labels << QStringLiteral("锁屏提醒");
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

bool isIgnoringBatteryOptimizations()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "isIgnoringBatteryOptimizations", "(Landroid/content/Context;)Z",
        context.object());
#else
    return true;
#endif
}

QVariantList permissionDiagnostics()
{
    // F1 权限页逐项画 ✅/❌；返回 QVariantMap 列表，QML 直接当 model 用。
    struct Item {
        const char *key;
        const char *label;
        Permission permission;
    };
    static const Item items[] = {
        { "notifications",    "通知",     Permission::Notifications },
        { "exactAlarm",       "精确闹钟", Permission::ExactAlarm },
        { "fullscreenIntent", "锁屏提醒", Permission::FullScreenIntent },
        { "phone",            "电话",     Permission::Phone },
        { "sms",              "短信",     Permission::Sms },
        { "microphone",       "麦克风",   Permission::Microphone },
    };

    QVariantList out;
    for (const Item &item : items) {
        out.append(QVariantMap {
            { QStringLiteral("key"), QString::fromLatin1(item.key) },
            { QStringLiteral("label"), QString::fromUtf8(item.label) },
#ifdef Q_OS_ANDROID
            { QStringLiteral("granted"), hasPermission(item.permission) },
            { QStringLiteral("canOpenSettings"), true },
#else
            // 桌面没有权限模型，按 demo 口径全给，也不需要设置跳转。
            { QStringLiteral("granted"), true },
            { QStringLiteral("canOpenSettings"), false },
#endif
        });
    }
    out.append(QVariantMap {
        { QStringLiteral("key"), QStringLiteral("battery") },
        { QStringLiteral("label"), QStringLiteral("电池白名单") },
        { QStringLiteral("granted"), isIgnoringBatteryOptimizations() },
#ifdef Q_OS_ANDROID
        { QStringLiteral("canOpenSettings"), true },
#else
        { QStringLiteral("canOpenSettings"), false },
#endif
    });
    return out;
}

bool openSystemSettings(const QString &key)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    if (key == QLatin1String("notifications")) {
        QJniObject::callStaticMethod<void>(kBridge, "openNotificationSettings",
                                           "(Landroid/content/Context;)V", context.object());
        return true;
    }
    if (key == QLatin1String("exactAlarm")) {
        openExactAlarmSettings();
        return true;
    }
    if (key == QLatin1String("fullscreenIntent")) {
        openFullScreenIntentSettings();
        return true;
    }
    if (key == QLatin1String("battery")) {
        requestIgnoreBatteryOptimizations();
        return true;
    }
    if (key == QLatin1String("phone") || key == QLatin1String("sms")
        || key == QLatin1String("microphone")) {
        QJniObject::callStaticMethod<void>(kBridge, "openAppDetailsSettings",
                                           "(Landroid/content/Context;)V", context.object());
        return true;
    }
#else
    Q_UNUSED(key)
#endif
    return false;
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

bool canUseFullScreenIntent()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "canUseFullScreenIntent", "(Landroid/content/Context;)Z", context.object());
#else
    return false;
#endif
}

void openFullScreenIntentSettings()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "openFullScreenIntentSettings",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

void speak(const QString &text)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid() || text.trimmed().isEmpty())
        return;
    QJniObject jText = QJniObject::fromString(text);
    QJniObject::callStaticMethod<void>(kBridge, "speakText",
                                       "(Landroid/content/Context;Ljava/lang/String;)V",
                                       context.object(), jText.object());
#else
    Q_UNUSED(text)
#endif
}

void stopSpeaking()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kBridge, "stopSpeaking", "()V");
#endif
}

bool canSpeak()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(kBridge, "canSpeak",
                                                  "(Landroid/content/Context;)Z",
                                                  context.object());
#else
    return false;
#endif
}

bool wasSpokenRecently(int withinMs)
{
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kBridge, "wasSpokenRecently", "(J)Z",
                                                  jlong(withinMs));
#else
    Q_UNUSED(withinMs)
    return false;
#endif
}

void setSpeechRate(double rate)
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kBridge, "setSpeechRate", "(F)V", jfloat(rate));
#else
    Q_UNUSED(rate)
#endif
}

void setSpeechPitch(double pitch)
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kBridge, "setSpeechPitch", "(F)V", jfloat(pitch));
#else
    Q_UNUSED(pitch)
#endif
}

QStringList chineseVoices()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return {};
    const QJniObject names = QJniObject::callStaticObjectMethod(
        kBridge, "chineseVoices", "(Landroid/content/Context;)Ljava/lang/String;",
        context.object());
    if (!names.isValid())
        return {};
    return names.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
#else
    return {};
#endif
}

bool chineseVoiceMissing()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "chineseVoiceMissing", "(Landroid/content/Context;)Z", context.object());
#else
    return false;
#endif
}

void startConnectionService()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "startConnectionService",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

void stopConnectionService()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kBridge, "stopConnectionService",
                                       "(Landroid/content/Context;)V", context.object());
#endif
}

bool isDeviceOwner()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(kBridge, "isDeviceOwner",
                                                  "(Landroid/content/Context;)Z",
                                                  context.object());
#else
    return false;
#endif
}

QString diagnostics()
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return QStringLiteral("android context 不可用");
    QJniObject result = QJniObject::callStaticObjectMethod(
        kBridge, "diagnose", "(Landroid/content/Context;)Ljava/lang/String;", context.object());
    return result.isValid() ? result.toString() : QString();
#else
    return QStringLiteral("desktop");
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
                   const QString &title, const QString &text, bool silent, bool speak,
                   const QString &audioPath)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = activityContext();
    if (!context.isValid())
        return false;

    QJniObject jTitle = QJniObject::fromString(title);
    QJniObject jText = QJniObject::fromString(text);
    QJniObject jAudio = QJniObject::fromString(audioPath);
    return QJniObject::callStaticMethod<jboolean>(
        kBridge, "scheduleAlarm",
        "(Landroid/content/Context;JJLjava/lang/String;Ljava/lang/String;ZZLjava/lang/String;)Z",
        context.object(), jlong(occurrenceId), jlong(triggerAtMs), jTitle.object(),
        jText.object(), jboolean(silent), jboolean(speak), jAudio.object());
#else
    Q_UNUSED(occurrenceId)
    Q_UNUSED(triggerAtMs)
    Q_UNUSED(title)
    Q_UNUSED(text)
    Q_UNUSED(silent)
    Q_UNUSED(speak)
    Q_UNUSED(audioPath)
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
