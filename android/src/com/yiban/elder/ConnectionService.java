package com.yiban.elder;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import androidx.core.app.NotificationCompat;

/**
 * B2-2：连着床头灯板期间的前台服务。只做一件事——挂一条无声的常驻通知，
 * 让系统别把 Qt 主循环冻住（灭屏/切后台后 5 秒心跳会停）。
 * 通知点进去回应用；端上 BleLink 断开时由 Platform.stopConnectionService 停掉。
 */
public class ConnectionService extends Service {
    private static final String TAG = "YibanConn";
    public static final String ACTION_START = "com.yiban.elder.CONN_START";
    public static final String ACTION_STOP = "com.yiban.elder.CONN_STOP";
    private static final String CHANNEL_ID = "yiban_board_link";
    private static final int NOTIFICATION_ID = 9102;
    private boolean started = false;

    @Override
    public IBinder onBind(Intent intent) { return null; }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (ACTION_STOP.equals(action)) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (!started) {
            ensureChannel();
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    startForeground(NOTIFICATION_ID, buildNotification(),
                            android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
                } else {
                    startForeground(NOTIFICATION_ID, buildNotification());
                }
                started = true;
                Log.i(TAG, "灯板连接前台服务已启动");
            } catch (Throwable t) {
                // Android 14 上没声明 connectedDevice 权限会抛：降级为先止服务，
                // 不能让心跳没保住还崩掉 App。
                Log.w(TAG, "startForeground 失败", t);
                stopSelf();
            }
        }
        return START_NOT_STICKY;
    }

    private void ensureChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm == null || nm.getNotificationChannel(CHANNEL_ID) != null) return;
        NotificationChannel ch = new NotificationChannel(
                CHANNEL_ID, "床头灯板连接", NotificationManager.IMPORTANCE_LOW);
        ch.setDescription("连接灯板期间保持蓝牙心跳，不响铃不振动");
        ch.enableVibration(false);
        ch.setSound(null, null);
        nm.createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        Intent open = YibanPlatform.launchIntent(this);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, flags);
        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentTitle("颐伴正在连接床头灯板")
                .setContentText("保持联网心跳，灭屏也能收到提醒")
                .setContentIntent(pi)
                .setOngoing(true)
                .setCategory(Notification.CATEGORY_SERVICE)
                .build();
    }

    @Override
    public void onDestroy() {
        if (started) {
            stopForeground(true);
            started = false;
        }
        super.onDestroy();
    }
}
