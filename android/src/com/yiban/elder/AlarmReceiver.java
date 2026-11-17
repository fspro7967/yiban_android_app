package com.yiban.elder;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

/**
 * 到点闹钟的落点：AlarmManager 时间一到，系统广播到这里。
 *
 * 这里**不做实际工作**，只负责把 AlarmService 拉起来，原因：
 *   * BroadcastReceiver 的 onReceive 只有十几秒预算，之后进程随时可能被回收，
 *     撑不起"响铃 45 秒 + 语音播报"这种长任务；
 *   * 从 Android 8 起后台服务要靠 startForegroundService，从 12 起还要有
 *     "精确闹钟触发"这个豁免，正好是闹钟广播自带的，所以只能在这一跳里发起。
 *
 * 拉不起来（ROM 限制/权限缺失）就退回"发一条全屏通知"，绝不会静默什么都不做。
 */
public class AlarmReceiver extends BroadcastReceiver {

    public static final String ACTION_FIRE = "com.yiban.elder.ALARM_FIRE";
    public static final String EXTRA_ID = "alarm_id";
    public static final String EXTRA_TITLE = "alarm_title";
    public static final String EXTRA_TEXT = "alarm_text";
    public static final String EXTRA_SILENT = "alarm_silent";
    public static final String EXTRA_SPEAK = "alarm_speak";
    /** 离线 TTS 预渲染的播报 WAV；空串/文件不存在时 AlarmService 退回系统 TTS。 */
    public static final String EXTRA_AUDIO_PATH = "alarm_audio_path";

    private static final String TAG = "YibanAlarm";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION_FIRE.equals(intent.getAction())) {
            return;
        }

        long id = intent.getLongExtra(EXTRA_ID, 0L);
        String title = intent.getStringExtra(EXTRA_TITLE);
        String text = intent.getStringExtra(EXTRA_TEXT);
        boolean silent = intent.getBooleanExtra(EXTRA_SILENT, false);
        boolean speak = intent.getBooleanExtra(EXTRA_SPEAK, true);
        String audioPath = intent.getStringExtra(EXTRA_AUDIO_PATH);
        if (audioPath == null) {
            audioPath = "";
        }

        // 进程被系统回收过时，Intent 里可能没有文案（理论上不会，但有备无患）：
        // 从登记表补一次，补不到就用兜底文案，绝不弹空通知。
        if (title == null || title.isEmpty()) {
            AlarmStore.Entry entry = AlarmStore.find(context, id);
            if (entry != null) {
                title = entry.title;
                text = entry.text;
                silent = entry.silent;
                speak = entry.speak;
                audioPath = entry.audioPath;
            }
        }
        if (title == null || title.isEmpty()) {
            title = "到点提醒";
        }
        if (text == null) {
            text = "";
        }

        // 先删登记表：这条已经触发过，开机重建时不该再来一次。
        // 放在启动服务之前，避免服务启动过程中被别的路径再读一次。
        AlarmStore.remove(context, id);

        if (!startAlarmService(context, id, title, text, silent, speak, audioPath)) {
            // 服务起不来：至少把全屏通知发出去，界面还能被点开
            Log.w(TAG, "AlarmService 未能启动，降级为通知");
            YibanPlatform.showNotification(context, notificationId(id), title, text, true);
        }
    }

    private boolean startAlarmService(Context context, long id, String title, String text,
            boolean silent, boolean speak, String audioPath) {
        Intent service = new Intent(context, AlarmService.class);
        service.setAction(AlarmService.ACTION_START);
        service.putExtra(EXTRA_ID, id);
        service.putExtra(EXTRA_TITLE, title);
        service.putExtra(EXTRA_TEXT, text);
        service.putExtra(EXTRA_SILENT, silent);
        service.putExtra(EXTRA_SPEAK, speak);
        service.putExtra(EXTRA_AUDIO_PATH, audioPath);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(service);
            } else {
                context.startService(service);
            }
            return true;
        } catch (Exception e) {
            Log.w(TAG, "startForegroundService 失败: " + e);
            return false;
        }
    }

    /** 通知 id 用 int，occurrenceId 是 long，取模压一下；同一个闹钟始终同一个通知。 */
    static int notificationId(long id) {
        return (int) (id % 1000000L);
    }
}
