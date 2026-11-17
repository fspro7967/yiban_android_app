package com.yiban.elder;

import android.accessibilityservice.AccessibilityService;
import android.os.SystemClock;
import android.util.Log;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;

import java.util.List;

/**
 * 短信确认弹窗的"自动点发送"。
 *
 * HyperOS 对非默认短信应用发短信时会弹一个系统确认框，SmsManager 的
 * sendTextMessage 要等用户点"发送"才真正出网。老人按呼救时没人替他点。
 *
 * 工作方式：无障碍服务监听窗口事件，HyperOS 的确认弹窗出现在
 * com.miui.securitycenter（部分版本在 com.android.systemui / com.lbe.security）
 * 里时，找到文字为"发送 / 允许 / 确认"的按钮自动点一下。
 *
 * 防误触的三重闸：
 *   1. 只在 sendSms 之后的 15 秒窗口内才动手（arm() 记时间戳）；
 *   2. 只看 MIUI/LBE 权限弹窗的包名，别的弹窗一概不碰；
 *   3. 只点白名单文字的按钮，其余控件不碰。
 *
 * 服务默认关闭，root 下由 YibanPlatform 自动开启（enabled_accessibility_services）；
 * 没有 root 时需要用户在无障碍设置里手动开启一次。
 */
public class SmsConfirmService extends AccessibilityService {

    private static final String TAG = "YibanSmsConfirm";
    /** sendSms 后多久内的确认弹窗才会被自动点。 */
    private static final long ARM_WINDOW_MS = 15_000L;
    /** 允许自动点击的按钮文字。 */
    private static final String[] BUTTON_TEXTS = {"发送", "允许", "确认", "好"};
    /** 确认弹窗可能所在的包（HyperOS 各版本落点不同，按前缀匹配）。 */
    private static final String[] TARGET_PACKAGES = {
            "com.miui.securitycenter", "com.lbe.security", "com.android.systemui",
    };

    /** 最近一次 sendSms 的时刻（elapsedRealtime）。volatile：无障碍线程和主线程并发读。 */
    private static volatile long sArmedAt = 0L;

    public static void arm() {
        sArmedAt = SystemClock.elapsedRealtime();
    }

    private static boolean armed() {
        return SystemClock.elapsedRealtime() - sArmedAt < ARM_WINDOW_MS;
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        if (!armed() || event == null) {
            return;
        }
        CharSequence pkgCs = event.getPackageName();
        if (pkgCs == null) {
            return;
        }
        String pkg = pkgCs.toString();
        boolean target = false;
        for (String t : TARGET_PACKAGES) {
            if (pkg.startsWith(t)) {
                target = true;
                break;
            }
        }
        if (!target) {
            return;
        }
        // 弹窗先有窗口、按钮后渲染：窗口切换 + 内容变化都盯，
        // 命中白名单按钮才动手，防误触逻辑不变。
        final int type = event.getEventType();
        if (type != AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED
                && type != AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED) {
            return;
        }

        try {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            if (root == null) {
                return;
            }
            for (String text : BUTTON_TEXTS) {
                List<AccessibilityNodeInfo> nodes =
                        root.findAccessibilityNodeInfosByText(text);
                if (nodes == null) {
                    continue;
                }
                for (AccessibilityNodeInfo node : nodes) {
                    if (node == null || !node.isEnabled() || !node.isClickable()) {
                        continue;
                    }
                    CharSequence label = node.getText();
                    if (label == null || !text.contentEquals(label)) {
                        // findByText 是包含匹配，必须精确等于才点，防止点到别的字样
                        continue;
                    }
                    node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                    Log.i(TAG, "已自动点击短信确认按钮: " + text);
                    return;
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "自动确认失败: " + e);
        }
    }

    @Override
    public void onInterrupt() {
        // 不会被打断式输出，留空
    }
}
