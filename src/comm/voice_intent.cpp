#include "voice_intent.h"

#include <QChar>
#include <cstddef>

namespace VoiceIntent {

QString normalize(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &ch : text) {
        // 标点、符号、空白、控制字符全部丢掉，只留 letter/number（含汉字）。
        if (ch.isLetterOrNumber())
            out.append(ch);
    }
    return out;
}

namespace {

// 白名单：只有完整、肯定的短句才算确认。刻意不收"吃""喝""好了吗"这类
// 可能出现在疑问句/否定句里的片段，保证 10 次无关话 0 误确认。
const char *const kConfirmPhrases[] = {
    "吃了",   "吃过了", "吃过",     "吃完",   "吃完了", "吃好了",
    "我已吃", "我吃了", "已经吃了", "吃过药", "吃药了", "吃了药",
    "喝过",   "喝过了", "喝了",     "喝完了", "喝完",
    "好了",   "好了好了", "弄好了", "搞定了", "搞定", "完成了",
};

const char *const kSnoozePhrases[] = {
    "等一下", "等一等", "等会儿", "等会", "待会儿", "待会",
    "稍后",   "稍等",   "过会儿", "过会", "一会儿", "待一下", "晚点再说",
};

// 句首语气词/主语，剥掉后再匹配（"嗯，吃了" / "那个吃了"）。
const char *const kLeadingFillers[] = {
    "嗯", "呃", "额", "那个", "这个", "就是说", "然后", "我", "我已经", "我在",
};

// 句尾语气词，剥掉后再匹配（"吃了呀" / "等一下啊"）。
const char *const kTrailingFillers[] = {
    "呀", "啊", "吧", "呢", "哦", "噢", "哈", "啦", "了呀", "的话", "了",
};

bool inList(const QString &s, const char *const *list, int count)
{
    for (int i = 0; i < count; ++i) {
        // 中文词表是 UTF-8 字节串，不能用 QLatin1String（会按 Latin-1 解码）。
        if (s == QString::fromUtf8(list[i]))
            return true;
    }
    return false;
}

template <std::size_t N>
int sizeOf(const char *const (&)[N])
{
    return static_cast<int>(N);
}

// 只做完整短句匹配，绝不子串匹配——"没吃""吃了吗"绝不能命中"吃了"。
Intent exact(const QString &s)
{
    if (inList(s, kConfirmPhrases, sizeOf(kConfirmPhrases)))
        return Intent::Confirm;
    if (inList(s, kSnoozePhrases, sizeOf(kSnoozePhrases)))
        return Intent::Snooze;
    return Intent::Unknown;
}

// 剥掉一个句尾语气词后再完整匹配（"吃了呀"→"吃了"）。
// 只剥一个：连剥多个会把"好了"→"好"、"吃了"→"吃"，丢掉核心词。
Intent withTrailingStripped(const QString &s)
{
    const Intent direct = exact(s);
    if (direct != Intent::Unknown)
        return direct;

    for (const char *const *f = kTrailingFillers;
         f < kTrailingFillers + sizeOf(kTrailingFillers); ++f) {
        const QString filler = QString::fromUtf8(*f);
        if (s.size() > filler.size() && s.endsWith(filler)) {
            QString body = s;
            body.chop(filler.size());
            return exact(body);
        }
    }
    return Intent::Unknown;
}

// 核心匹配：原文、逐级剥句首语气词后的每个候选，各试一次"完整 + 剥一个句尾"。
// 每一步都先判完整再剥，避免"那个好了"在剥完前缀后又把"了"剥掉变成"好"。
Intent matchPhrase(const QString &s)
{
    const Intent direct = withTrailingStripped(s);
    if (direct != Intent::Unknown)
        return direct;

    QString body = s;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const char *const *f = kLeadingFillers;
             f < kLeadingFillers + sizeOf(kLeadingFillers); ++f) {
            const QString filler = QString::fromUtf8(*f);
            if (body.size() > filler.size() && body.startsWith(filler)) {
                body.remove(0, filler.size());
                changed = true;
                break;
            }
        }
        if (!changed)
            break;
        const Intent hit = withTrailingStripped(body);
        if (hit != Intent::Unknown)
            return hit;
    }
    return Intent::Unknown;
}

} // namespace

Intent classify(const QString &text)
{
    const QString s = normalize(text);
    if (s.isEmpty())
        return Intent::Unknown;
    return matchPhrase(s);
}

QString name(Intent intent)
{
    switch (intent) {
    case Intent::Confirm:
        return QStringLiteral("confirm");
    case Intent::Snooze:
        return QStringLiteral("snooze");
    case Intent::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

} // namespace VoiceIntent
