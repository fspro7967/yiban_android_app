package com.yiban.elder;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * 开机重建闹钟。
 *
 * Android 重启会清空所有 AlarmManager 闹钟，闹钟型应用必须自己重建。
 * 这里不启动 Activity（Android 10+ 禁止后台拉起界面），也不启动前台服务
 * （省掉一个常驻通知），只按 AlarmStore 里的影子记录把还没到期的闹钟重新排一遍。
 *
 * 注意 Manifest 里这个 receiver 必须是 exported="true"：
 * BOOT_COMPLETED 由系统进程（不同 uid）投递，写成 false 时广播直接到不了，
 * 表现就是"重启后整个应用的提醒都不响了"。这是本项目 v1 的真实 bug。
 */
public class BootReceiver extends BroadcastReceiver {

    private static final String TAG = "YibanAlarm";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null) {
            return;
        }
        String action = intent.getAction();
        final boolean boot = Intent.ACTION_BOOT_COMPLETED.equals(action)
                || "android.intent.action.QUICKBOOT_POWERON".equals(action)
                || "android.intent.action.HTC.QUICKBOOT_POWERON".equals(action);
        // 覆盖安装/升级后 AlarmManager 里的闹钟同样会被清掉，一并重建
        final boolean upgraded = Intent.ACTION_MY_PACKAGE_REPLACED.equals(action);
        if (!boot && !upgraded) {
            return;
        }

        long now = System.currentTimeMillis();
        int restored = 0;
        int dropped = 0;
        for (AlarmStore.Entry entry : AlarmStore.all(context)) {
            if (entry.triggerAt > now) {
                YibanPlatform.scheduleAlarm(context, entry.id, entry.triggerAt,
                        entry.title, entry.text, entry.silent, entry.speak, entry.audioPath);
                ++restored;
            } else {
                // 关机期间已经错过的：不补响（补响会把人吓一跳），交给应用启动后的
                // 状态机把它们标成 missed。这里只清影子记录。
                AlarmStore.remove(context, entry.id);
                ++dropped;
            }
        }
        Log.i(TAG, "开机/升级后重建闹钟: 恢复 " + restored + " 条，丢弃过期 " + dropped + " 条");
    }
}
