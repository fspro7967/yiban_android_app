package com.yiban.elder;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * 闹钟登记表（SharedPreferences）。
 *
 * 为什么 Java 侧还要存一份：AlarmManager 里的闹钟**开机后全部丢失**，
 * 而重建闹钟需要"什么时候、提醒什么"这两样信息。开机时 Qt 进程还没起来，
 * 拿不到 C++ 里的计划表，所以在这里留一份最小副本，只给 BootReceiver 用。
 *
 * 应用启动后 C++ 会重新排程并覆盖这里的内容，因此它永远不是真相源，
 * 只是一份"闹钟的影子"。
 */
public final class AlarmStore {

    private static final String PREFS = "yiban_alarms";
    private static final String SEP = "\u0001";

    public static final class Entry {
        public final long id;
        public final long triggerAt;
        public final String title;
        public final String text;

        Entry(long id, long triggerAt, String title, String text) {
            this.id = id;
            this.triggerAt = triggerAt;
            this.title = title;
            this.text = text;
        }
    }

    private AlarmStore() {
    }

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static String key(long id) {
        return "alarm_" + id;
    }

    public static void put(Context ctx, long id, long triggerAt, String title, String text) {
        String value = triggerAt + SEP + safe(title) + SEP + safe(text);
        prefs(ctx).edit().putString(key(id), value).apply();
    }

    public static void remove(Context ctx, long id) {
        prefs(ctx).edit().remove(key(id)).apply();
    }

    public static void clear(Context ctx) {
        prefs(ctx).edit().clear().apply();
    }

    public static Entry find(Context ctx, long id) {
        String value = prefs(ctx).getString(key(id), null);
        return parse(id, value);
    }

    public static List<Entry> all(Context ctx) {
        List<Entry> out = new ArrayList<>();
        for (Map.Entry<String, ?> item : prefs(ctx).getAll().entrySet()) {
            String name = item.getKey();
            if (!name.startsWith("alarm_")) {
                continue;
            }
            long id;
            try {
                id = Long.parseLong(name.substring("alarm_".length()));
            } catch (NumberFormatException e) {
                continue;
            }
            Entry entry = parse(id, item.getValue() instanceof String ? (String) item.getValue() : null);
            if (entry != null) {
                out.add(entry);
            }
        }
        return out;
    }

    private static Entry parse(long id, String value) {
        if (value == null) {
            return null;
        }
        String[] parts = value.split(SEP, -1);
        if (parts.length < 3) {
            return null;
        }
        long triggerAt;
        try {
            triggerAt = Long.parseLong(parts[0]);
        } catch (NumberFormatException e) {
            return null;
        }
        return new Entry(id, triggerAt, parts[1], parts[2]);
    }

    /** 分隔符出现在标题里会把解析搞乱，先替掉（提醒文案里几乎不会出现 \u0001）。 */
    private static String safe(String text) {
        return text == null ? "" : text.replace(SEP, " ");
    }
}
