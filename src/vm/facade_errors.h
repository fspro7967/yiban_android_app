#pragma once

#include <QObject>
#include <QString>

// facade 错误码 → 用户可见文案。
// 代码（code）是稳定契约，文案可以随时改；QML 只展示文案、不解析 code。
inline QString facadeErrorText(const QString &code)
{
    if (code == QStringLiteral("occurrence_not_found"))
        return QStringLiteral("找不到这条提醒");
    if (code == QStringLiteral("already_confirmed"))
        return QStringLiteral("这条已经确认过了");
    if (code == QStringLiteral("snooze_limit"))
        return QStringLiteral("已经推迟 3 次了，请先吃药或联系家人");
    if (code == QStringLiteral("nothing_to_undo"))
        return QStringLiteral("没有可以撤销的操作");
    if (code == QStringLiteral("not_escalatable"))
        return QStringLiteral("当前状态不能通知家人");
    if (code == QStringLiteral("plan_not_found"))
        return QStringLiteral("找不到这条计划");
    if (code == QStringLiteral("record_not_found"))
        return QStringLiteral("找不到这条记录");
    if (code == QStringLiteral("empty_title"))
        return QStringLiteral("请先填写提醒内容");
    if (code == QStringLiteral("bad_rule_kind"))
        return QStringLiteral("重复规则不合法");
    if (code == QStringLiteral("empty_times"))
        return QStringLiteral("请至少设置一个提醒时间");
    if (code == QStringLiteral("late_not_allowed"))
        return QStringLiteral("这条提醒已经过时，按要求不能再补做");
    if (code == QStringLiteral("empty_weekdays"))
        return QStringLiteral("每周重复请至少选一天");
    if (code == QStringLiteral("nothing_today"))
        return QStringLiteral("这条提醒今天本来就没有");
    if (code == QStringLiteral("water_goal_reached"))
        return QStringLiteral("今天的水已经喝够了");
    if (code == QStringLiteral("audio_unavailable"))
        return QStringLiteral("这台设备暂时没有可用的语音引擎");
    if (code == QStringLiteral("bad_pair_code"))
        return QStringLiteral("配对码不对，请重新输入");
    if (code == QStringLiteral("offline"))
        return QStringLiteral("当前是离线演示，云端接口还没有接入");
    return code;
}
