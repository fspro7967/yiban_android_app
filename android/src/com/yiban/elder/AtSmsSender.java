package com.yiban.elder;

import android.util.Log;

/**
 * root 直发短信：绕过 HyperOS 对非默认短信应用的"发送确认"拦截。
 *
 * 原理：应用层的 SmsManager 走 telephony framework，MIUI/HyperOS 在这条路上
 * 插了确认弹窗；而 modem 的 AT 命令通道（MediaTek 平台是 /dev/ttyC*）是
 * 字符设备，root 进程可以直接读写 —— AT+CMGS 是 GSM 27.005 的标准发短信
 * 指令，modem 收到就直接提交 SMSC，不经过任何应用层拦截。
 *
 * 中文内容用 PDU 文本模式的 UCS2 编码（AT+CSCS="UCS2"），号码同样编码。
 * 每条短信最多 70 个 UCS2 字符，超长按 69 字符切段（不发 UDH，收件端
 * 看到的是连续几条，紧急短信宁可分段也别丢内容）。
 *
 * 通道探测有一次性缓存：应用启动时后台预热（YibanPlatform.diagnose 里调
 * warmUp），呼救时直接用现成结果，几秒内就能把短信送进 modem。
 *
 * 任何一步失败都返回 false，由调用方退回 SmsManager + 自动确认的普通路径。
 */
public final class AtSmsSender {

    private static final String TAG = "YibanAtSms";
    /** 探测结果缓存有效期：modem 状态可能变化，过期后重新握手。 */
    private static final long PROBE_TTL_MS = 10 * 60_000L;
    /** 单条短信的 UCS2 字符上限（70），留 1 个余量。 */
    private static final int MAX_UCS2_CHARS = 69;

    /** 探测到可用的 AT 通道；null = 没探测到或已过期。 */
    private static volatile String sDevice;
    private static volatile long sProbedAt;
    private static volatile boolean sWarming;

    private AtSmsSender() {
    }

    /** 应用启动时的后台预热：提前把 AT 通道找到，呼救时省 3~8 秒。 */
    public static void warmUp() {
        if (sWarming || (sDevice != null && fresh())) {
            return;
        }
        sWarming = true;
        new Thread(() -> {
            try {
                sDevice = probe();
                sProbedAt = now();
                Log.i(TAG, sDevice != null ? "AT 通道就绪: " + sDevice : "未找到可用 AT 通道");
            } finally {
                sWarming = false;
            }
        }, "yiban-at-probe").start();
    }

    /**
     * 发送。号码/正文不限制（自动切段），全部成功返回 true。
     * 内部每步都有超时，最坏情况约 20 秒 —— 调用方放后台线程跑。
     */
    public static boolean send(String number, String text) {
        if (number == null || number.trim().isEmpty() || text == null || text.isEmpty()) {
            return false;
        }
        String dev = sDevice != null && fresh() ? sDevice : null;
        if (dev == null) {
            dev = probe();
            sDevice = dev;
            sProbedAt = now();
        }
        if (dev == null) {
            return false;
        }

        final String numHex = ucs2Hex(normalize(number));
        final String body = text;
        final int chunks = (body.length() + MAX_UCS2_CHARS - 1) / MAX_UCS2_CHARS;
        for (int i = 0; i < chunks; i++) {
            int from = i * MAX_UCS2_CHARS;
            int to = Math.min(body.length(), from + MAX_UCS2_CHARS);
            String chunkHex = ucs2Hex(body.substring(from, to));
            String reply = sendSession(dev, numHex, chunkHex);
            boolean ok = reply != null && reply.contains("+CMGS") && reply.contains("OK");
            Log.i(TAG, "AT 发送段 " + (i + 1) + "/" + chunks + " -> " + (ok ? "成功" : "失败"));
            if (!ok) {
                return false; // 有段失败：返回 false，让调用方走 SmsManager 兜底
            }
        }
        return true;
    }

    // ------------------------------------------------------------ 内部实现

    private static long now() {
        return android.os.SystemClock.elapsedRealtime();
    }

    private static boolean fresh() {
        return now() - sProbedAt < PROBE_TTL_MS;
    }

    /** MediaTek 的 AT 通道常见落点，Qualcomm 的 smd 兜底。握手 "AT" 等回 "OK"。 */
    private static final String[] CANDIDATES = {
            "/dev/ttyC1", "/dev/ttyC2", "/dev/ttyC3", "/dev/ttyC4", "/dev/ttyC5",
            "/dev/ttyC0", "/dev/smd11", "/dev/smd0",
    };

    private static String probe() {
        if (!RootShell.available()) {
            return null;
        }
        for (String dev : CANDIDATES) {
            if (!RootShell.pathExists(dev)) {
                continue;
            }
            String reply = exchange(dev, "AT", 2_500);
            if (reply != null && reply.contains("OK")) {
                Log.i(TAG, "AT 握手成功: " + dev);
                return dev;
            }
        }
        return null;
    }

    /**
     * 一次 AT 问答。cat 必须先挂上再写命令：tty 在没有读者时到达的数据
     * 会被驱动丢弃，"写完再开 cat"会错过应答（sendSession 同款时序）。
     */
    private static String exchange(String dev, String atCmd, long readTimeoutMs) {
        long sec = Math.max(2, readTimeoutMs / 1000);
        String script = "timeout " + sec + " cat " + dev + " & W=$!; "
                + "sleep 0.2; printf '" + atCmd + "\\r' > " + dev + "; wait $W";
        return RootShell.exec(script, readTimeoutMs + 5_000);
    }

    /**
     * 完整的发送会话（读与写并发，一次 root 会话跑完）：
     *   cat 挂后台收应答；写端依次发 CMGF/CSCS/CMGS/正文+Ctrl-Z；等 cat 超时返回。
     * 输出里 "+CMGS: <序号>" + "OK" 即发送成功。
     */
    private static String sendSession(String dev, String numHex, String bodyHex) {
        String script =
                "timeout 14 cat " + dev + " & W=$!; "
                + "( printf 'AT+CMGF=1\\r'; sleep 0.3;"
                + " printf 'AT+CSCS=\"UCS2\"\\r'; sleep 0.3;"
                + " printf 'AT+CMGS=\\\"" + numHex + "\\\"\\r'; sleep 0.6;"
                + " printf '" + bodyHex + "\\032'; sleep 6 ) > " + dev
                + "; wait $W";
        return RootShell.exec(script, 22_000);
    }

    /** 去掉 +86/空格/横线等，只留数字和开头的 +。 */
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

    /** UCS2（UTF-16BE）十六进制编码，27.005 在 CSCS="UCS2" 下号码和正文都用它。 */
    private static String ucs2Hex(String s) {
        StringBuilder sb = new StringBuilder(s.length() * 4);
        for (int i = 0; i < s.length(); i++) {
            int v = s.charAt(i) & 0xFFFF;
            sb.append(Character.forDigit((v >> 12) & 0xF, 16));
            sb.append(Character.forDigit((v >> 8) & 0xF, 16));
            sb.append(Character.forDigit((v >> 4) & 0xF, 16));
            sb.append(Character.forDigit(v & 0xF, 16));
        }
        return sb.toString();
    }
}
