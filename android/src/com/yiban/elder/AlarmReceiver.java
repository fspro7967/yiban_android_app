package com.yiban.elder;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * 到点闹钟的落点：AlarmManager 时间一到，系统广播到这里。
 *
 * 不启动 Qt 进程、不判状态——只弹一条高优先级通知，带 full-screen intent：
 * 锁屏时系统会把 QtActivity 直接顶上来，剩下的（显示哪条提醒、怎么确认）
 * 由 QML + AppCore 决定。
 */
public class AlarmReceiver extends BroadcastReceiver {

    public static final String ACTION_FIRE = "com.yiban.elder.ALARM_FIRE";
    public static final String EXTRA_ID = "alarm_id";
    public static final String EXTRA_TITLE = "alarm_title";
    public static final String EXTRA_TEXT = "alarm_text";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION_FIRE.equals(intent.getAction())) {
            return;
        }

        long id = intent.getLongExtra(EXTRA_ID, 0L);
        String title = intent.getStringExtra(EXTRA_TITLE);
        String text = intent.getStringExtra(EXTRA_TEXT);

        // 进程被系统回收过时，Intent 里可能没有文案（理论上不会，但有备无患）：
        // 从登记表补一次，补不到就用兜底文案，绝不弹空通知。
        if (title == null || title.isEmpty()) {
            AlarmStore.Entry entry = AlarmStore.find(context, id);
            if (entry != null) {
                title = entry.title;
                text = entry.text;
            }
        }
        if (title == null || title.isEmpty()) {
            title = "到点提醒";
        }
        if (text == null) {
            text = "";
        }

        YibanPlatform.showNotification(context, notificationId(id), title, text, true);

        // 这一条已经响过：从登记表删掉，开机重建时不会重复。
        AlarmStore.remove(context, id);
    }

    /** 通知 id 用 int，occurrenceId 是 long，取模压一下；同一个闹钟始终同一个通知。 */
    static int notificationId(long id) {
        return (int) (id % 1000000L);
    }
}
