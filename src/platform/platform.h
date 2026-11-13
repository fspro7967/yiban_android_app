#pragma once

#include <QString>
#include <QStringList>

// 平台层（B2）：全仓库唯一允许碰 Android 系统服务的地方。
//
// 约定：
//   * 桌面构建下所有函数都是安全空实现（返回 false / 空列表），
//     所以 QML、单元测试、离屏截图脚本在 PC 上照常跑，不需要 #ifdef；
//   * 这里只做"开权限、发通知、定闹钟、打电话、锁任务"这类系统动作，
//     不判业务状态、不写业务表——那是 AppCore / VM 的事；
//   * 所有 JNI 调用都包在 Q_OS_ANDROID 里，Java 侧实现见
//     android/src/com/yiban/elder/。
namespace Platform {

enum class Permission {
    Notifications,  // POST_NOTIFICATIONS（Android 13+ 才需要，13 以下安装即授权）
    ExactAlarm,     // SCHEDULE_EXACT_ALARM（Android 12+ 需要用户/系统给）
    Phone,          // CALL_PHONE
    Sms,            // SEND_SMS
    Microphone,     // RECORD_AUDIO（语音记一笔，P1，先留口）
};

bool isAndroid();

bool hasPermission(Permission p);
// 发起授权：通知/电话/短信/麦克风走系统弹窗；精确闹钟跳系统设置页
// （Android 12+ 没有可编程弹窗，只能让用户在设置里打开）。
void requestPermission(Permission p);

// 当前已授权项的中文标签，给 DeviceVM.permissions 用。
// 桌面构建返回全部三项（demo 里没有权限概念，全给）。
QStringList grantedLabels();

bool canScheduleExactAlarms();
void openExactAlarmSettings();
// 引导用户把本应用加入电池优化白名单，否则部分机型会杀后台导致闹钟不响。
void requestIgnoreBatteryOptimizations();

void notify(int id, const QString &title, const QString &text, bool fullScreen = false);
void cancelNotification(int id);

// 精确闹钟：到点由 AlarmReceiver 弹全屏通知（锁屏也能亮）。
// 闹钟信息同时写进 SharedPreferences，开机由 BootReceiver 重建。
bool scheduleAlarm(qint64 occurrenceId, qint64 triggerAtMs,
                   const QString &title, const QString &text);
void cancelAlarm(qint64 occurrenceId);
// 清空全部闹钟（含 SharedPreferences 里的记录）。重新排程前先清一次，保证幂等。
void cancelAllAlarms();

// ACTION_CALL 直接拨号；没拿到 CALL_PHONE 时返回 false，调用方降级短信。
bool call(const QString &number);
bool sendSms(const QString &number, const QString &text);

// 老人机模式：startLockTask（无设备管理员时会弹系统"屏幕固定"确认）。
bool enterKiosk();
void exitKiosk();

} // namespace Platform
