#pragma once

#include <QString>

// B2-9：语音确认的意图判定（纯逻辑，无 Android / 麦克风 / QML 依赖）。
// 识别结果交给 classify()，只有明确命中词表才动作；不确定一律 Unknown，
// 调用方只提示"没听清，请再说一遍"，绝不猜测确认——宁可漏确认，不可误确认。
namespace VoiceIntent {

enum class Intent {
    Unknown, // 没听清 / 无关话 / 疑问句：不动作
    Confirm, // 吃了 / 好了 / 喝了：confirm
    Snooze,  // 等一下 / 待会儿：snooze
};

// 归一化：去首尾空白、去掉所有标点与空格，只保留汉字/字母/数字。
// SpeechRecognizer 返回的中文常带"。，！？"和空格，先归一化再匹配。
QString normalize(const QString &text);

// 意图分类。不确定返回 Unknown。
Intent classify(const QString &text);

// 日志/测试用：意图名。
QString name(Intent intent);

} // namespace VoiceIntent
