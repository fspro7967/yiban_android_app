package com.yiban.elder;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * 开机重建闹钟。
 *
 * Android 重启会清空所有 AlarmManager 闹钟，闹钟型应用必须自己重建。
 * 这里不启动 Activity（Android 10+ 禁止后台拉起界面），也不启动前台服务
 * （省掉一个常驻通知），只按 AlarmStore 里的影子记录把还没到期的闹钟重新排一遍。
 */
public class BootReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null) {
            return;
        }
        String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action)
                && !"android.intent.action.QUICKBOOT_POWERON".equals(action)) {
            return;
        }

        long now = System.currentTimeMillis();
        for (AlarmStore.Entry entry : AlarmStore.all(context)) {
            if (entry.triggerAt > now) {
                YibanPlatform.scheduleAlarm(context, entry.id, entry.triggerAt,
                        entry.title, entry.text);
            } else {
                // 关机期间已经错过的：不补响（补响会把人吓一跳），交给应用启动后的
                // 状态机把它们标成 missed。这里只清影子记录。
                AlarmStore.remove(context, entry.id);
            }
        }
    }
}
