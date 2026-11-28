package com.yiban.elder;

import android.app.admin.DeviceAdminReceiver;
import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

/**
 * B2-4：device owner 的入口，供
 *   adb shell dpm set-device-owner com.yiban.elder/.AdminReceiver
 * 使用。设上之后把本应用加进 lock-task 白名单，startLockTask 不再弹系统确认。
 *
 * 前提：测试机上不能有账号（可能要清机），见 1124TODO V-4。
 * 不是 device owner 的机器上这些回调不会被触发，现有 startLockTask 的
 * try/catch 会安全降级为屏幕固定。
 */
public class AdminReceiver extends DeviceAdminReceiver {
    private static final String TAG = "YibanAdmin";

    @Override
    public void onEnabled(final Context context, final Intent intent) {
        super.onEnabled(context, intent);
        allowLockTask(context);
    }

    @Override
    public CharSequence onDisableRequested(Context context, Intent intent) {
        return "退出设备管理员后，老人机模式会退回屏幕固定（按返回键可能退出）。";
    }

    /** 把本包名加进 lockTask 白名单；device owner 才有权限调用。 */
    public static void allowLockTask(Context context) {
        try {
            DevicePolicyManager dpm =
                    (DevicePolicyManager) context.getSystemService(Context.DEVICE_POLICY_SERVICE);
            if (dpm == null || !dpm.isDeviceOwnerApp(context.getPackageName()))
                return;
            ComponentName admin = new ComponentName(context, AdminReceiver.class);
            dpm.setLockTaskPackages(admin, new String[]{context.getPackageName()});
            Log.i(TAG, "lock-task 白名单已设置");
        } catch (Throwable t) {
            Log.w(TAG, "setLockTaskPackages 失败（非 device owner 时正常）", t);
        }
    }

    public static boolean isDeviceOwner(Context context) {
        try {
            DevicePolicyManager dpm =
                    (DevicePolicyManager) context.getSystemService(Context.DEVICE_POLICY_SERVICE);
            return dpm != null && dpm.isDeviceOwnerApp(context.getPackageName());
        } catch (Throwable t) {
            return false;
        }
    }
}
