package com.yiban.elder;

import android.app.Activity;
import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.telephony.SmsManager;

/**
 * C++ 平台层（src/platform/platform.cpp）的 Java 落点。
 *
 * 只做系统动作：权限、通知、精确闹钟、拨号、短信、屏幕固定。
 * 不判业务状态、不存业务数据——提醒该不该响由 AppCore 的状态机决定。
 *
 * 所有方法都是静态的，C++ 侧按 QJniObject::callStaticMethod 调用，
 * 方法名与签名改动必须同步改 platform.cpp。
 */
public final class YibanPlatform {

    public static final String CHANNEL_ID = "yiban_reminder";
    public static final String CHANNEL_NAME = "到点提醒";

    private YibanPlatform() {
    }

    private static Activity activity(Context ctx) {
        return ctx instanceof Activity ? (Activity) ctx : null;
    }

    // ---------------------------------------------------------------- 权限

    public static boolean hasPermission(Context ctx, String permission) {
        try {
            return ctx.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
        } catch (Exception e) {
            return false;
        }
    }

    /**
     * 单独申请一个权限。用数组重载是为了避开 JNI 里构造 String[] 的麻烦，
     * 一次性只需要一个权限。
     */
    public static void requestPermission(Context ctx, String permission, int requestCode) {
        Activity activity = activity(ctx);
        if (activity == null) {
            return;
        }
        try {
            activity.requestPermissions(new String[] { permission }, requestCode);
        } catch (Exception ignored) {
            // 已经问过且被"不再询问"时系统不会弹窗，等用户去设置里改；
            // 这里不抛回 C++，避免一次权限异常把提醒流程打断。
        }
    }

    public static boolean canScheduleExactAlarms(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return true;
        }
        try {
            AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
            return am != null && am.canScheduleExactAlarms();
        } catch (Exception e) {
            return false;
        }
    }

    public static void openExactAlarmSettings(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return;
        }
        try {
            Intent intent = new Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM,
                    Uri.parse("package:" + ctx.getPackageName()));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(intent);
        } catch (Exception ignored) {
            // 个别 ROM 没有这个设置页，退回应用详情页
            try {
                Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        Uri.parse("package:" + ctx.getPackageName()));
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(intent);
            } catch (Exception ignoredAgain) {
                // 什么都不做：C++ 侧会退回非精确闹钟
            }
        }
    }

    public static void requestIgnoreBatteryOptimizations(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }
        try {
            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                    Uri.parse("package:" + ctx.getPackageName()));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(intent);
        } catch (Exception ignored) {
            try {
                Intent intent = new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(intent);
            } catch (Exception ignoredAgain) {
                // 老 ROM 没有这两个页面，放弃
            }
        }
    }

    // ---------------------------------------------------------------- 通知

    private static void ensureChannel(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationManager nm =
                (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null || nm.getNotificationChannel(CHANNEL_ID) != null) {
            return;
        }
        NotificationChannel channel = new NotificationChannel(CHANNEL_ID, CHANNEL_NAME,
                NotificationManager.IMPORTANCE_HIGH);
        channel.setDescription("到点提醒与紧急呼救");
        channel.enableLights(true);
        channel.enableVibration(true);
        channel.setBypassDnd(true);
        channel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
        nm.createNotificationChannel(channel);
    }

    private static Intent launchIntent(Context ctx) {
        Intent intent = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
        if (intent == null) {
            return null;
        }
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_SINGLE_TOP
                | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        return intent;
    }

    private static int requestCode(long id) {
        return (int) (id % Integer.MAX_VALUE);
    }

    private static PendingIntent launchPendingIntent(Context ctx, long id) {
        Intent intent = launchIntent(ctx);
        if (intent == null) {
            return null;
        }
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        return PendingIntent.getActivity(ctx, requestCode(id), intent, flags);
    }

    /** 发通知；fullScreen=true 时锁屏也直接亮屏弹出全屏提醒。 */
    public static void showNotification(Context ctx, int id, String title, String text, boolean fullScreen) {
        try {
            ensureChannel(ctx);
            NotificationManager nm =
                    (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm == null) {
                return;
            }

            Notification.Builder builder;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                builder = new Notification.Builder(ctx, CHANNEL_ID);
            } else {
                builder = new Notification.Builder(ctx);
            }

            int icon = ctx.getApplicationInfo().icon;
            if (icon == 0) {
                icon = android.R.drawable.ic_dialog_info;
            }

            builder.setSmallIcon(icon)
                    .setContentTitle(title)
                    .setContentText(text)
                    .setStyle(new Notification.BigTextStyle().bigText(text))
                    .setCategory(Notification.CATEGORY_ALARM)
                    .setVisibility(Notification.VISIBILITY_PUBLIC)
                    .setAutoCancel(true)
                    .setOngoing(false);

            PendingIntent pending = launchPendingIntent(ctx, id);
            if (pending != null) {
                builder.setContentIntent(pending);
                if (fullScreen) {
                    // 锁屏状态下的"整屏提醒"就是靠这一句：系统直接把 Activity 顶上来。
                    // 需要 USE_FULL_SCREEN_INTENT 权限，见 AndroidManifest.xml。
                    builder.setFullScreenIntent(pending, true);
                }
            }

            nm.notify(id, builder.build());
        } catch (Exception ignored) {
            // 没授权通知时静默失败；C++ 侧靠 DeviceVM.permissions 提示用户去开
        }
    }

    public static void cancelNotification(Context ctx, int id) {
        try {
            NotificationManager nm =
                    (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm != null) {
                nm.cancel(id);
            }
        } catch (Exception ignored) {
        }
    }

    // ---------------------------------------------------------------- 闹钟

    private static PendingIntent fireIntent(Context ctx, long alarmId, String title, String text) {
        Intent intent = new Intent(ctx, AlarmReceiver.class);
        intent.setAction(AlarmReceiver.ACTION_FIRE);
        intent.putExtra(AlarmReceiver.EXTRA_ID, alarmId);
        intent.putExtra(AlarmReceiver.EXTRA_TITLE, title);
        intent.putExtra(AlarmReceiver.EXTRA_TEXT, text);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        return PendingIntent.getBroadcast(ctx, requestCode(alarmId), intent, flags);
    }

    /**
     * 精确闹钟：优先 setAlarmClock（Doze 下也能准时，且系统会显示闹钟图标）。
     * 没有精确闹钟权限时退化成 setAndAllowWhileIdle，最多晚几分钟，
     * 但至少不会"完全不响"。
     */
    public static boolean scheduleAlarm(Context ctx, long alarmId, long triggerAtMs,
            String title, String text) {
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am == null) {
            return false;
        }
        if (triggerAtMs <= System.currentTimeMillis()) {
            return false;
        }

        PendingIntent fire = fireIntent(ctx, alarmId, title, text);
        boolean exact = canScheduleExactAlarms(ctx);
        try {
            if (exact && Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                PendingIntent show = launchPendingIntent(ctx, alarmId);
                am.setAlarmClock(new AlarmManager.AlarmClockInfo(triggerAtMs, show), fire);
            } else {
                am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMs, fire);
            }
        } catch (SecurityException e) {
            try {
                am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMs, fire);
                exact = false;
            } catch (Exception ignored) {
                return false;
            }
        } catch (Exception e) {
            return false;
        }

        // 无论精确与否都登记，开机后靠它重建
        AlarmStore.put(ctx, alarmId, triggerAtMs, title, text);
        return exact;
    }

    public static void cancelAlarm(Context ctx, long alarmId) {
        try {
            AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
            if (am != null) {
                am.cancel(fireIntent(ctx, alarmId, "", ""));
            }
        } catch (Exception ignored) {
        }
        AlarmStore.remove(ctx, alarmId);
    }

    public static void cancelAllAlarms(Context ctx) {
        for (AlarmStore.Entry entry : AlarmStore.all(ctx)) {
            cancelAlarm(ctx, entry.id);
        }
        AlarmStore.clear(ctx);
    }

    // ---------------------------------------------------------------- 呼救

    /** 直接拨号；没权限就返回 false，让 C++ 降级成短信。 */
    public static boolean call(Context ctx, String number) {
        if (number == null || number.trim().isEmpty()) {
            return false;
        }
        if (!hasPermission(ctx, android.Manifest.permission.CALL_PHONE)) {
            return false;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_CALL, Uri.parse("tel:" + normalize(number)));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(intent);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean sendSms(Context ctx, String number, String text) {
        if (number == null || number.trim().isEmpty()) {
            return false;
        }
        if (!hasPermission(ctx, android.Manifest.permission.SEND_SMS)) {
            return false;
        }
        try {
            SmsManager sm;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                sm = ctx.getSystemService(SmsManager.class);
            } else {
                sm = SmsManager.getDefault();
            }
            if (sm == null) {
                return false;
            }
            sm.sendTextMessage(normalize(number), null, text, null, null);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /** 去掉 "+86"、空格、横线、括号，只留数字和开头的 +。 */
    private static String normalize(String number) {
        StringBuilder sb = new StringBuilder();
        String trimmed = number.trim();
        for (int i = 0; i < trimmed.length(); i++) {
            char c = trimmed.charAt(i);
            if (Character.isDigit(c) || (c == '+' && sb.length() == 0)) {
                sb.append(c);
            }
        }
        return sb.toString();
    }

    // ---------------------------------------------------------------- 老人机模式

    public static void startLockTask(Context ctx) {
        Activity activity = activity(ctx);
        if (activity == null) {
            return;
        }
        try {
            activity.startLockTask();
        } catch (Exception ignored) {
            // 没有设备管理员时系统会先弹"屏幕固定"确认，用户取消即失败；
            // 这里不抛错，C++ 侧照常置 kioskActive，退出通道仍然可用。
        }
    }

    public static void stopLockTask(Context ctx) {
        Activity activity = activity(ctx);
        if (activity == null) {
            return;
        }
        try {
            activity.stopLockTask();
        } catch (Exception ignored) {
        }
    }
}
