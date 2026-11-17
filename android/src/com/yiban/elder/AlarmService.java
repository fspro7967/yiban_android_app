package com.yiban.elder;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

/**
 * 到点提醒的"发声体"。
 *
 * 为什么要有这个服务：闹钟到点时 Qt 主进程通常已经不在了（锁屏、被系统回收、
 * 用户从最近任务里划掉），BroadcastReceiver 只有十几秒存活时间，既放不了
 * 45 秒的铃声，也不该在里面做长耗时的事。前台服务则具备三个必要条件：
 *
 *   1. 有前台通知 → 进程不会被随手杀掉；
 *   2. 可以持 PARTIAL_WAKE_LOCK → 屏幕黑着时 CPU 不休眠，铃声和 TTS 不断；
 *   3. 可以把闹铃音量拉满、走 STREAM_ALARM → 静音/振动模式下照样响。
 *
 * 它只负责"出声 + 亮屏通知"，不判断这是哪条提醒、也不改提醒状态：
 * 状态由 AppCore 掌握，界面顶起来之后 QML 会问它。
 */
public class AlarmService extends Service {

    public static final String ACTION_START = "com.yiban.elder.ALARM_START";
    public static final String ACTION_STOP = "com.yiban.elder.ALARM_STOP";

    /** 响铃上限：45 秒。再久老人会被吵得心慌，也费电。 */
    private static final long RING_DURATION_MS = 45_000L;
    /** 夜间安静下的"只亮屏"时长：15 秒足够看清，也不至于整夜亮着。 */
    private static final long SILENT_DURATION_MS = 15_000L;
    private static final long TTS_DELAY_MS = 900L;
    private static final String TAG = "YibanAlarm";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private MediaPlayer player;
    private PowerManager.WakeLock wakeLock;
    /** 把屏幕从黑着点亮起来（与保 CPU 的 PARTIAL 锁分开）。 */
    private PowerManager.WakeLock screenLock;
    /** 到点临时改掉的勿扰过滤器，播完恢复；-1 = 没改过。 */
    private int savedDndFilter = -1;
    private boolean started = false;

    /** 播报结束后把铃声恢复回来（播报期间暂停铃声用的）。 */
    private final Runnable ringDuckRestore = this::restoreRing;

    private final Runnable stopTask = new Runnable() {
        @Override
        public void run() {
            stopEverything();
        }
    };

    private final Runnable speakTask = new Runnable() {
        @Override
        public void run() {
            // 播报文案的两条路，按可靠度排：
            //   1. 离线预渲染 WAV（排程时由 C++ 侧用仓库自带模型合成好落盘）：
            //      跟放铃声一样"读文件出声"，不依赖设备有没有 TTS 引擎；
            //   2. 系统 TTS：WAV 缺失/损坏时的兜底。
            // 两条路都会先把铃声压到 15%：铃声循环和播报同在闹铃流上，
            // 同音量混音时播报完全听不清（真机反馈 bug #2）。
            if (pendingAudioPath != null && !pendingAudioPath.isEmpty()
                    && new java.io.File(pendingAudioPath).canRead()) {
                duckRing();
                playAnnouncement(pendingAudioPath);
            } else if (pendingText != null) {
                duckRing();
                YibanPlatform.speakText(AlarmService.this, pendingText,
                        AlarmService.this::restoreRing);
                // 兜底：TTS 引擎异常时 onDone 不会回调，按字数估算强制恢复
                long estimate = Math.min(15_000L,
                        Math.max(2_000L, pendingText.length() * 220L));
                handler.postDelayed(ringDuckRestore, estimate);
            }
        }
    };

    private String pendingText;
    private String pendingAudioPath;
    /** 播报音频（离线 WAV），与铃声 player 分开：铃声循环，播报只放一遍。 */
    private MediaPlayer announcementPlayer;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopEverything();
            return START_NOT_STICKY;
        }

        final long id = intent != null ? intent.getLongExtra(AlarmReceiver.EXTRA_ID, 0L) : 0L;
        String title = intent != null ? intent.getStringExtra(AlarmReceiver.EXTRA_TITLE) : null;
        String text = intent != null ? intent.getStringExtra(AlarmReceiver.EXTRA_TEXT) : null;
        final boolean silent = intent != null
                && intent.getBooleanExtra(AlarmReceiver.EXTRA_SILENT, false);
        final boolean speak = intent == null
                || intent.getBooleanExtra(AlarmReceiver.EXTRA_SPEAK, true);
        final String audioPath = intent != null
                ? intent.getStringExtra(AlarmReceiver.EXTRA_AUDIO_PATH) : null;
        if (title == null || title.isEmpty()) {
            title = "到点提醒";
        }
        if (text == null) {
            text = "";
        }

        startInForeground(AlarmReceiver.notificationId(id), title, text, silent);
        acquireWakeLock();
        wakeScreen();
        bypassDnd();

        if (silent) {
            // 夜间安静：只亮屏、只留通知，一声不响。
            // 这是界面开关的真实语义，此前只有 App 内播报遵守，闹钟服务照响，属于 bug。
            pendingText = null;
            pendingAudioPath = null;
            handler.postDelayed(stopTask, SILENT_DURATION_MS);
        } else {
            startRinging();
            pendingAudioPath = speak ? (audioPath == null ? "" : audioPath) : null;
            pendingText = !speak ? null : (text.isEmpty() ? title : title + "。" + text);
            if (pendingText != null || pendingAudioPath != null) {
                handler.postDelayed(speakTask, TTS_DELAY_MS);
            }
            handler.postDelayed(stopTask, RING_DURATION_MS);
        }
        started = true;
        return START_NOT_STICKY;
    }

    /**
     * 前台化。Android 10+ 要带 type，Android 14 起 type 必须在 Manifest 里声明
     * 且对应权限已授予，否则 startForeground 抛异常 —— 抛了就退回不带 type 的
     * 老写法，再不行就放弃，让通知栏那条兜底。
     */
    private void startInForeground(int notificationId, String title, String text, boolean quiet) {
        if (started) {
            return;
        }
        Notification notification = YibanPlatform.buildAlarmNotification(
                this, notificationId, title, text, true, quiet);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(notificationId, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            } else {
                startForeground(notificationId, notification);
            }
        } catch (Exception first) {
            Log.w(TAG, "startForeground(type) 失败，退回无 type 写法: " + first);
            try {
                startForeground(notificationId, notification);
            } catch (Exception second) {
                Log.w(TAG, "startForeground 失败，只保留通知: " + second);
                YibanPlatform.showNotification(this, notificationId, title, text, true);
            }
        }
    }

    private void acquireWakeLock() {
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm == null) {
                return;
            }
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "yiban:alarm");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire(RING_DURATION_MS + 10_000L);
        } catch (Exception e) {
            Log.w(TAG, "拿不到 wakelock: " + e);
        }
    }

    /**
     * 亮屏。PARTIAL_WAKE_LOCK 只保 CPU，屏幕黑着就一直是黑的 —— 真机
     * （HyperOS 2 / Android 14）反馈"到点不亮屏"就是缺了这一步。
     *
     * SCREEN_BRIGHT + ACQUIRE_CAUSES_WAKEUP 直接把屏幕点亮，不依赖
     * full-screen intent 权限（Android 14 上那个权限默认不发放，
     * HyperOS 还额外要"锁屏显示"，靠它是靠不住的）。
     * 再尽力把应用界面顶上来：QtActivity 在 Manifest 里带
     * showWhenLocked/turnScreenOn，权限够时就是全屏提醒页。
     */
    private void wakeScreen() {
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null) {
                screenLock = pm.newWakeLock(
                        PowerManager.SCREEN_BRIGHT_WAKE_LOCK
                                | PowerManager.ACQUIRE_CAUSES_WAKEUP
                                | PowerManager.ON_AFTER_RELEASE,
                        "yiban:alarmScreen");
                screenLock.setReferenceCounted(false);
                screenLock.acquire(15_000L);
            }
        } catch (Throwable t) {
            Log.w(TAG, "亮屏 wakelock 拿不到: " + t);
        }
        try {
            // HyperOS 会拦"后台弹出界面"（Android 14 的 BAL 限制 + MIUI 私有
            // appop），full-screen intent 权限又默认不发放——root 下自助放行，
            // 老人机的提醒页才顶得上来（真机反馈 bug #1）。
            YibanPlatform.grantPopupPermissions(this);
            Intent launch = YibanPlatform.launchIntent(this);
            if (launch != null) {
                startActivity(launch);
            }
        } catch (Throwable t) {
            // Android 14 的后台启动限制 / HyperOS"后台弹出界面"没开时会拦，
            // 屏幕已经亮了 + 锁屏上有通知，不至于看不见
            Log.i(TAG, "服务内启动界面被拦（可接受）: " + t.getMessage());
        }
    }

    /**
     * 到点临时解除勿扰。USAGE_ALARM 在"优先"模式的默认配置下也会被压住
     * （真机反馈 bug #3：夜间开着勿扰，闹铃不响）。有通知策略访问权限时
     * 把过滤器切回"全部"，播完恢复原样；没权限但已 root 就自助写白名单。
     */
    private void bypassDnd() {
        try {
            NotificationManager nm =
                    (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm == null) {
                return;
            }
            final int current = nm.getCurrentInterruptionFilter();
            if (current == NotificationManager.INTERRUPTION_FILTER_ALL
                    || current == NotificationManager.INTERRUPTION_FILTER_UNKNOWN) {
                return;
            }
            if (!nm.isNotificationPolicyAccessGranted() && RootShell.available()) {
                RootShell.appendSecureList("enabled_notification_policy_access_packages",
                        getPackageName());
            }
            if (nm.isNotificationPolicyAccessGranted()) {
                savedDndFilter = current;
                nm.setInterruptionFilter(NotificationManager.INTERRUPTION_FILTER_ALL);
                Log.i(TAG, "到点临时解除勿扰（原过滤器=" + current + "），播完恢复");
            } else {
                Log.w(TAG, "勿扰开着且没有通知策略权限，铃声可能被压住；请在设置里给应用"
                        + "「勿扰」访问权限，或开启勿扰的\"闹钟\"例外");
            }
        } catch (Throwable t) {
            Log.w(TAG, "勿扰处理失败: " + t);
        }
    }

    private void restoreDnd() {
        if (savedDndFilter < 0) {
            return;
        }
        try {
            NotificationManager nm =
                    (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm != null && nm.isNotificationPolicyAccessGranted()) {
                nm.setInterruptionFilter(savedDndFilter);
            }
        } catch (Throwable ignored) {
        }
        savedDndFilter = -1;
    }

    /**
     * 播报期间把铃声**整个暂停**：铃声和播报同在闹铃流上，同一流里两路
     * 音源按音量混音，就算把铃声压到 12%，HyperOS 的提示音混响下语音
     * 还是难听清（真机反馈 bug #2）。暂停最干净——播报完再接着响。
     */
    private void duckRing() {
        handler.removeCallbacks(ringDuckRestore);
        if (player != null && player.isPlaying()) {
            try {
                player.pause();
            } catch (Exception ignored) {
            }
        }
    }

    /** 播报结束（完成/出错/兜底计时到）后恢复铃声，可重入。 */
    private void restoreRing() {
        handler.removeCallbacks(ringDuckRestore);
        if (player != null && !player.isPlaying()) {
            try {
                player.start(); // 从暂停处继续循环响铃
            } catch (Exception ignored) {
            }
        }
    }

    /**
     * 响铃。用 MediaPlayer 而不是 Ringtone：Ringtone 不能可靠地设置音频属性，
     * 在"媒体音量 0 / 静音模式"下会直接不出声，而这正是最要命的场景。
     */
    private void startRinging() {
        try {
            AudioManager am = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
            if (am != null) {
                int max = am.getStreamMaxVolume(AudioManager.STREAM_ALARM);
                am.setStreamVolume(AudioManager.STREAM_ALARM, max, 0);
            }
        } catch (Exception e) {
            Log.w(TAG, "拉高铃声音量失败: " + e);
        }

        try {
            Uri uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM);
            if (uri == null) {
                uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION);
            }
            if (uri == null) {
                return;
            }
            player = new MediaPlayer();
            player.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build());
            player.setDataSource(this, uri);
            player.setLooping(true);
            player.prepare();
            player.start();
        } catch (Exception e) {
            Log.w(TAG, "响铃失败: " + e);
            releasePlayer();
        }
    }

    /**
     * 播放离线预渲染的播报 WAV（一次，不循环）。
     * 用 USAGE_ALARM 与铃声同流：铃声已把闹铃音量拉满，播报跟着走同一音量，
     * 静音模式下也能听见。播完记录时间戳，全屏界面顶起来后 QML 的 speak()
     * 靠它去重，避免同一句话念两遍。
     */
    private void playAnnouncement(String path) {
        try {
            MediaPlayer player = new MediaPlayer();
            player.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build());
            player.setDataSource(path);
            player.setOnCompletionListener(mp -> {
                mp.release();
                if (announcementPlayer == mp) {
                    announcementPlayer = null;
                }
                restoreRing(); // 播完了，铃声回到满音量
            });
            player.setOnErrorListener((mp, what, extra) -> {
                mp.release();
                if (announcementPlayer == mp) {
                    announcementPlayer = null;
                }
                restoreRing();
                return true;
            });
            player.prepare();
            player.start();
            announcementPlayer = player;
            YibanPlatform.noteSpoken();
            // 兜底：onCompletion 因任何原因没回调时，按音频实际时长强制恢复铃声
            handler.postDelayed(ringDuckRestore, player.getDuration() + 1_500L);
        } catch (Exception e) {
            Log.w(TAG, "播放离线播报失败，退回系统 TTS: " + e);
            restoreRing();
            if (pendingText != null) {
                duckRing();
                YibanPlatform.speakText(AlarmService.this, pendingText,
                        AlarmService.this::restoreRing);
            }
        }
    }

    private void stopEverything() {
        handler.removeCallbacks(stopTask);
        handler.removeCallbacks(speakTask);
        handler.removeCallbacks(ringDuckRestore);
        releasePlayer();
        releaseAnnouncementPlayer();
        YibanPlatform.stopSpeaking();
        restoreDnd();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        wakeLock = null;
        if (screenLock != null && screenLock.isHeld()) {
            screenLock.release();
        }
        screenLock = null;
        started = false;
        try {
            stopForeground(false); // 通知留着，老人还能看到刚才提醒了什么
        } catch (Exception ignored) {
        }
        stopSelf();
    }

    private void releaseAnnouncementPlayer() {
        if (announcementPlayer == null) {
            return;
        }
        try {
            if (announcementPlayer.isPlaying()) {
                announcementPlayer.stop();
            }
        } catch (Exception ignored) {
        }
        try {
            announcementPlayer.release();
        } catch (Exception ignored) {
        }
        announcementPlayer = null;
    }

    private void releasePlayer() {
        if (player == null) {
            return;
        }
        try {
            if (player.isPlaying()) {
                player.stop();
            }
        } catch (Exception ignored) {
        }
        try {
            player.release();
        } catch (Exception ignored) {
        }
        player = null;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        releasePlayer();
        releaseAnnouncementPlayer();
        restoreDnd();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        wakeLock = null;
        if (screenLock != null && screenLock.isHeld()) {
            screenLock.release();
        }
        screenLock = null;
        super.onDestroy();
    }
}
