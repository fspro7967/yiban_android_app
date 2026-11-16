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
import android.media.AudioAttributes;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.os.SystemClock;
import android.provider.Settings;
import android.speech.tts.TextToSpeech;
import android.telephony.SmsManager;

import java.util.Locale;

/**
 * C++ 平台层（src/platform/platform.cpp）的 Java 落点。
 *
 * 只做系统动作：权限、通知、精确闹钟、语音播报、拨号、短信、屏幕固定。
 * 不判业务状态、不存业务数据——提醒该不该响由 AppCore 的状态机决定。
 *
 * 所有方法都是静态的，C++ 侧按 QJniObject::callStaticMethod 调用，
 * 方法名与签名改动必须同步改 platform.cpp。
 */
public final class YibanPlatform {

    /**
     * 通知通道 id 带版本号是**故意的**：Android 的通道一旦创建，它的声音、
     * 重要级别就归用户所有，代码再改也不会生效。v1（yiban_reminder）当初
     * 是系统默认音，实测在锁屏/静音场景下听不见；换成新 id 才能让新配置
     * （闹铃音频属性 + 高优先级）真正落到用户设备上。
     */
    public static final String CHANNEL_ID = "yiban_reminder_v2";
    public static final String CHANNEL_ID_LEGACY = "yiban_reminder";
    public static final String CHANNEL_NAME = "到点提醒";

    private static TextToSpeech sTts;
    private static volatile boolean sTtsReady;
    private static String sTtsPending;
    /** 最近一次开口的时刻（elapsedRealtime），用于去重：服务念过界面就不再念一遍。 */
    private static volatile long sSpokenAt = 0L;

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

    public static boolean hasPostNotifications(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            // 13 以下没有这个运行时权限，装完就有
            return true;
        }
        return hasPermission(ctx, "android.permission.POST_NOTIFICATIONS");
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

    /**
     * Android 14 起 USE_FULL_SCREEN_INTENT 不再对普通应用自动授予，
     * 只有闹钟/通话类应用才默认放行。所以锁屏不亮屏这件事，
     * 在 14+ 上必须靠"查 canUseFullScreenIntent + 引导用户去开"来解决，
     * 光在 Manifest 里声明是不够的。
     */
    public static boolean canUseFullScreenIntent(Context ctx) {
        if (Build.VERSION.SDK_INT < 34) {
            return true;
        }
        try {
            NotificationManager nm =
                    (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            return nm == null || nm.canUseFullScreenIntent();
        } catch (Throwable ignored) {
            return false;
        }
    }

    public static void openFullScreenIntentSettings(Context ctx) {
        if (Build.VERSION.SDK_INT < 34) {
            openExactAlarmSettings(ctx);
            return;
        }
        try {
            Intent intent = new Intent("android.settings.MANAGE_APP_USE_FULL_SCREEN_INTENT",
                    Uri.parse("package:" + ctx.getPackageName()));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(intent);
        } catch (Exception ignored) {
            try {
                Intent fallback = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        Uri.parse("package:" + ctx.getPackageName()));
                fallback.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(fallback);
            } catch (Exception ignoredAgain) {
                // 放弃：至少还有通知栏那条
            }
        }
    }

    public static boolean isIgnoringBatteryOptimizations(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        try {
            PowerManager pm = (PowerManager) ctx.getSystemService(Context.POWER_SERVICE);
            return pm != null && pm.isIgnoringBatteryOptimizations(ctx.getPackageName());
        } catch (Exception e) {
            return false;
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

    // ---------------------------------------------------------------- 语音

    /**
     * 到点播报走系统 TTS，不走 Qt 的 QTextToSpeech。
     *
     * 两个理由：
     *   1. 闹钟响的时候 Qt 进程可能已经被系统回收，QTextToSpeech 根本不存在；
     *   2. qtts 需要设备侧有 TTS 引擎且 Qt 的 tts 插件被打进 APK，实测多数
     *      国产 ROM 上 availableEngines() 为空 —— 表现就是"没有语音播报"。
     *
     * 系统 TTS 只要设备装了语音引擎就能出声，且能在前台服务里跑，
     * 界面上"再听一遍"和闹钟播报因此共用同一条链路。
     */
    public static boolean canSpeak(Context ctx) {
        ensureTts(ctx);
        return sTts != null;
    }

    public static void speakText(Context ctx, String text) {
        if (text == null || text.trim().isEmpty()) {
            return;
        }
        ensureTts(ctx);
        if (sTts == null) {
            return;
        }
        if (!sTtsReady) {
            // 引擎还在初始化：记住最后一句，就绪后补播
            sTtsPending = text;
            return;
        }
        try {
            sTts.speak(text.trim(), TextToSpeech.QUEUE_FLUSH, null, "yiban");
            sSpokenAt = SystemClock.elapsedRealtime();
        } catch (Exception ignored) {
        }
    }

    /**
     * 最近 withinMs 毫秒内是不是刚播报过。
     *
     * 闹钟响铃时 AlarmService 先念了正文，随后全屏界面被顶起来，
     * QML 的 ReminderScreen 又会调一次 speak()——没有这个去重就是
     * 同一句话连念两遍，听起来像卡带。
     */
    public static boolean wasSpokenRecently(long withinMs) {
        return SystemClock.elapsedRealtime() - sSpokenAt < withinMs;
    }

    /**
     * 由 AlarmService 在播放离线预渲染 WAV 后调用：播报走了"放音频"这条路，
     * 没经过 sTts.speak，不记时间戳的话界面那边的去重就失效了。
     */
    public static void noteSpoken() {
        sSpokenAt = SystemClock.elapsedRealtime();
    }

    public static void stopSpeaking() {
        if (sTts != null) {
            try {
                sTts.stop();
            } catch (Exception ignored) {
            }
        }
        sTtsPending = null;
    }

    private static synchronized void ensureTts(Context ctx) {
        if (sTts != null) {
            return;
        }
        try {
            final Context app = ctx.getApplicationContext();
            sTts = new TextToSpeech(app, status -> {
                if (status != TextToSpeech.SUCCESS || sTts == null) {
                    sTtsReady = false;
                    return;
                }
                try {
                    int result = sTts.setLanguage(Locale.CHINA);
                    if (result == TextToSpeech.LANG_MISSING_DATA
                            || result == TextToSpeech.LANG_NOT_SUPPORTED) {
                        sTts.setLanguage(Locale.getDefault());
                    }
                    sTts.setSpeechRate(0.9f);
                    sTts.setPitch(1.0f);
                } catch (Exception ignored) {
                }
                sTtsReady = true;
                final String pending = sTtsPending;
                sTtsPending = null;
                if (pending != null) {
                    speakText(app, pending);
                }
            });
        } catch (Throwable t) {
            sTts = null;
            sTtsReady = false;
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

        // 显式指定闹铃音 + USAGE_ALARM：不指定时通道用"通知音"，
        // 音量走铃声音量、静音模式下会被静音，老人根本听不见。
        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ALARM)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        Uri sound = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM);
        if (sound == null) {
            sound = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION);
        }
        if (sound != null) {
            channel.setSound(sound, attrs);
        }
        channel.enableLights(true);
        channel.enableVibration(true);
        channel.setVibrationPattern(new long[] { 0, 600, 400, 600, 400, 900 });
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

    private static int pendingFlags() {
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        return flags;
    }

    private static PendingIntent launchPendingIntent(Context ctx, long id) {
        Intent intent = launchIntent(ctx);
        if (intent == null) {
            return null;
        }
        return PendingIntent.getActivity(ctx, requestCode(id), intent, pendingFlags());
    }

    /**
     * 组一条提醒通知。fullScreen=true 时带 full-screen intent：
     * 锁屏且系统放开权限的情况下，系统直接把 QtActivity 顶到最前。
     *
     * 抽成 public 是因为 AlarmService 要以 startForeground() 复用同一条通知，
     * 否则前台服务和通知栏会出现两条一样的提醒。
     */
    public static Notification buildAlarmNotification(Context ctx, int id, String title,
            String text, boolean fullScreen) {
        ensureChannel(ctx);

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
                .setContentTitle(title == null ? "到点提醒" : title)
                .setContentText(text == null ? "" : text)
                .setStyle(new Notification.BigTextStyle().bigText(text == null ? "" : text))
                .setCategory(Notification.CATEGORY_ALARM)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setAutoCancel(true)
                .setOngoing(false);

        PendingIntent pending = launchPendingIntent(ctx, id);
        if (pending != null) {
            builder.setContentIntent(pending);
            if (fullScreen) {
                // 锁屏状态下的"整屏提醒"就靠这一句；权限不够时系统自动降级成横幅，
                // 所以它必须和 canUseFullScreenIntent() 的引导配合使用。
                builder.setFullScreenIntent(pending, true);
            }
        }

        // 响铃期间要能一键安静：通知上的按钮比"关掉整条通知"更符合老人的直觉
        Intent stop = new Intent(ctx, AlarmService.class);
        stop.setAction(AlarmService.ACTION_STOP);
        PendingIntent stopPending = PendingIntent.getService(ctx, requestCode(id) + 1, stop,
                pendingFlags());
        builder.addAction(new Notification.Action.Builder(
                android.R.drawable.ic_menu_close_clear_cancel, "我知道了", stopPending).build());

        return builder.build();
    }

    /** 发通知；fullScreen=true 时锁屏也直接亮屏弹出全屏提醒。 */
    public static void showNotification(Context ctx, int id, String title, String text,
            boolean fullScreen) {
        try {
            NotificationManager nm =
                    (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm == null) {
                return;
            }
            if (!hasPostNotifications(ctx)) {
                return; // 没授权时 notify 也是静默丢弃，不如直接返回
            }
            nm.notify(id, buildAlarmNotification(ctx, id, title, text, fullScreen));
        } catch (Exception ignored) {
            // 个别 ROM 在权限被撤销的瞬间会抛 SecurityException；C++ 侧靠 DeviceVM.permissions 提示
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

    private static PendingIntent fireIntent(Context ctx, long alarmId, String title, String text,
            boolean silent, boolean speak, String audioPath) {
        Intent intent = new Intent(ctx, AlarmReceiver.class);
        intent.setAction(AlarmReceiver.ACTION_FIRE);
        intent.putExtra(AlarmReceiver.EXTRA_ID, alarmId);
        intent.putExtra(AlarmReceiver.EXTRA_TITLE, title);
        intent.putExtra(AlarmReceiver.EXTRA_TEXT, text);
        intent.putExtra(AlarmReceiver.EXTRA_SILENT, silent);
        intent.putExtra(AlarmReceiver.EXTRA_SPEAK, speak);
        intent.putExtra(AlarmReceiver.EXTRA_AUDIO_PATH, audioPath == null ? "" : audioPath);
        return PendingIntent.getBroadcast(ctx, requestCode(alarmId), intent, pendingFlags());
    }

    public static boolean scheduleAlarm(Context ctx, long alarmId, long triggerAtMs,
            String title, String text) {
        return scheduleAlarm(ctx, alarmId, triggerAtMs, title, text, false, true, null);
    }

    /**
     * 精确闹钟，三档：
     *   1. setAlarmClock —— Doze 下也能准时，状态栏显示闹钟图标，用户看得见；
     *   2. setExactAndAllowWhileIdle —— 没有闹钟权限但系统仍允许精确时的次选；
     *   3. setAndAllowWhileIdle —— 什么都不给时的兜底，可能晚几分钟，但不至于不响。
     *
     * silent/speak 两位来自界面设置：夜间安静时段只亮屏不出声，
     * 关掉语音播报时只响铃不念文案。它们随闹钟一起存进登记表，
     * 这样进程被杀、重启重建之后仍然守得住。
     *
     * audioPath 是 C++ 侧用仓库自带离线 TTS 模型预渲染的播报 WAV（可能为空）：
     * 到点时 AlarmService 优先放它——很多国产 ROM 没有中文 TTS 引擎，
     * 系统 TTS 念不出声，离线渲染的音频跟铃声一样必然能放。
     *
     * 返回值表示"是否拿到了精确档"，调用方据此提示用户去开权限。
     */
    public static boolean scheduleAlarm(Context ctx, long alarmId, long triggerAtMs,
            String title, String text, boolean silent, boolean speak, String audioPath) {
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        if (am == null) {
            return false;
        }
        if (triggerAtMs <= System.currentTimeMillis()) {
            return false;
        }

        PendingIntent fire = fireIntent(ctx, alarmId, title, text, silent, speak, audioPath);
        boolean exact = false;
        try {
            if (canScheduleExactAlarms(ctx)) {
                PendingIntent show = launchPendingIntent(ctx, alarmId);
                am.setAlarmClock(new AlarmManager.AlarmClockInfo(triggerAtMs, show), fire);
                exact = true;
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMs, fire);
                exact = true;
            } else {
                am.setExact(AlarmManager.RTC_WAKEUP, triggerAtMs, fire);
                exact = true;
            }
        } catch (SecurityException e) {
            // Android 12+ 没有 exact 权限时走到这里
            exact = false;
            try {
                am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMs, fire);
            } catch (Exception ignored) {
                return false;
            }
        } catch (Exception e) {
            return false;
        }

        // 无论精确与否都登记，开机后靠它重建（含"是否静音/是否播报/离线音频路径"）
        AlarmStore.put(ctx, alarmId, triggerAtMs, title, text, silent, speak, audioPath);
        return exact;
    }

    public static void cancelAlarm(Context ctx, long alarmId) {
        try {
            AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
            if (am != null) {
                am.cancel(fireIntent(ctx, alarmId, "", "", false, true, null));
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

    // ---------------------------------------------------------------- 诊断

    /**
     * 一行诊断串，应用启动时写进日志。
     *
     * "adb 里能看到 alarm 但不响"这类问题，80% 出在这四项里，一次性打出来
     * 比让人反复猜要快得多：通知权限、精确闹钟权限、全屏提醒权限、电池白名单。
     */
    public static String diagnose(Context ctx) {
        StringBuilder sb = new StringBuilder();
        sb.append("通知权限=").append(hasPostNotifications(ctx))
                .append(" 精确闹钟=").append(canScheduleExactAlarms(ctx))
                .append(" 全屏提醒=").append(canUseFullScreenIntent(ctx))
                .append(" 电池白名单=").append(isIgnoringBatteryOptimizations(ctx))
                .append(" 已登记闹钟=").append(AlarmStore.all(ctx).size());

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                NotificationManager nm =
                        (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
                NotificationChannel channel = nm == null ? null : nm.getNotificationChannel(CHANNEL_ID);
                if (channel == null) {
                    sb.append(" 通道=未创建");
                } else {
                    sb.append(" 通道重要级=").append(channel.getImportance())
                            .append(" 通道声音=").append(channel.getSound() == null ? "无" : "有");
                }
            } catch (Exception ignored) {
            }
        }
        return sb.toString();
    }
}
