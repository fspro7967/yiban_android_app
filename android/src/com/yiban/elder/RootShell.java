package com.yiban.elder;

import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/**
 * root 命令执行器。测试机已 root（Magisk），用来做三件系统层面的事：
 *
 *   1. 到点临时解除勿扰（写入通知策略访问白名单，见 YibanPlatform）；
 *   2. 自动开启无障碍服务，让系统拦截"短信发送确认"弹窗时自动点"发送"；
 *   3. 直接向 modem 的 AT 通道写指令发短信（见 AtSmsSender），
 *      彻底绕开 HyperOS 对非默认短信应用的发送确认拦截。
 *
 * 所有命令都有超时：su 弹授权框时进程会挂着不返回，不能把调用线程卡死。
 * 全部方法返回 null/ false 表示"没 root 或命令失败"，调用方必须有自己的兜底。
 */
public final class RootShell {

    private static final String TAG = "YibanRoot";
    /** 缓存 root 可用性：一次会话探测一次就够，su 授权框也不会反复弹。 */
    private static volatile Boolean sAvailable;

    private RootShell() {
    }

    /** 设备有没有可用的 su。首次探测会触发 Magisk 的授权框，超时按没 root 处理。 */
    public static boolean available() {
        Boolean cached = sAvailable;
        if (cached != null) {
            return cached;
        }
        String out = exec("id", 8_000);
        boolean ok = out != null && out.contains("uid=0");
        sAvailable = ok;
        if (!ok) {
            Log.i(TAG, "root 不可用，相关增强功能走普通路径");
        }
        return ok;
    }

    /**
     * 以 root 执行一条 shell 命令，返回合并后的 stdout+stderr。
     * 超时、无 su、命令失败一律返回 null（或已有部分输出）——调用方自己判断内容。
     */
    public static String exec(String command, long timeoutMs) {
        Process process = null;
        try {
            process = Runtime.getRuntime().exec(new String[] {"su", "-c", command});
            final Process p = process;
            StringBuilder out = new StringBuilder();
            Thread reader = new Thread(() -> {
                try (BufferedReader r = new BufferedReader(
                        new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = r.readLine()) != null) {
                        out.append(line).append('\n');
                    }
                } catch (Exception ignored) {
                }
            });
            reader.setDaemon(true);
            reader.start();
            // stderr 也吞掉，避免缓冲区满了把 su 卡住
            Thread errDrain = new Thread(() -> {
                try (BufferedReader r = new BufferedReader(
                        new InputStreamReader(p.getErrorStream(), StandardCharsets.UTF_8))) {
                    while (r.readLine() != null) {
                        // 丢弃
                    }
                } catch (Exception ignored) {
                }
            });
            errDrain.setDaemon(true);
            errDrain.start();

            if (!p.waitFor(timeoutMs, TimeUnit.MILLISECONDS)) {
                p.destroy();
                Log.w(TAG, "命令超时: " + command);
                return out.length() > 0 ? out.toString() : null;
            }
            reader.join(1_000);
            return out.toString();
        } catch (Exception e) {
            Log.w(TAG, "root 命令失败: " + command + " -> " + e);
            return null;
        } finally {
            if (process != null) {
                process.destroy();
            }
        }
    }

    /** 便捷版：8 秒超时。 */
    public static String exec(String command) {
        return exec(command, 8_000);
    }

    /** 文件/设备节点是否存在（/dev 下的节点没有 root 读不到目录，只能用 test 探）。 */
    public static boolean pathExists(String path) {
        String out = exec("test -e " + path + " && echo y");
        return out != null && out.contains("y");
    }

    /**
     * 把包名追加进一项 Settings.Secure 的白名单（已有则不动）。
     * 用于 enabled_notification_policy_access_packages 和
     * enabled_accessibility_services 这类"以冒号分隔的组件列表"。
     */
    public static boolean appendSecureList(String key, String value) {
        String current = exec("settings get secure " + key);
        if (current == null) {
            return false;
        }
        current = current.trim();
        if (current.contains(value)) {
            return true; // 已在列表里
        }
        String merged = (current == null || current.isEmpty()
                || "null".equals(current) || "none".equals(current))
                ? value : current + ":" + value;
        String out = exec("settings put secure " + key + " " + merged);
        return out != null;
    }

    /** root 可用时才返回非 null：su 的真实路径（有些 ROM 在 /system/xbin）。 */
    public static File suBinary() {
        for (String path : new String[] {"/system/bin/su", "/system/xbin/su",
                "/sbin/su", "/su/bin/su"}) {
            File f = new File(path);
            if (f.canExecute()) {
                return f;
            }
        }
        return null;
    }
}
