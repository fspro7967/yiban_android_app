#pragma once

#include <QString>
#include <QVariant>

// 端上持久化设置（B2）：跨重启必须记住的东西都在这里。
//
// 只放"设备自己知道"的状态：服务器地址、配对 token、device_id、
// 语音/夜间开关、上次同步时间。业务数据（计划、记录、事件）一律不进这里，
// 那是 AppCore 的库。
//
// 键名固定且带前缀，方便复现问题时直接看：
//   adb shell run-as com.yiban.elder ls shared_prefs
class AppSettings
{
public:
    static AppSettings *instance();

    QString serverUrl() const;
    void setServerUrl(const QString &url);

    QString token() const;
    void setToken(const QString &token);
    void clearToken();

    // 首次运行生成并持久化；配对请求和 /v1/alarm 的 device_id 都用它。
    QString deviceId() const;

    bool paired() const;
    QString role() const;
    void setPairing(bool paired, const QString &role);

    bool voiceEnabled() const;
    void setVoiceEnabled(bool on);

    bool nightQuiet() const;
    void setNightQuiet(bool on);

    qint64 lastSyncTs() const;
    void setLastSyncTs(qint64 ts);

    // 仅演示/测试用：清空所有键（含 device_id，下次会重新生成）
    void reset();

private:
    AppSettings() = default;

    QString readString(const QString &key, const QString &fallback = QString()) const;
    bool readBool(const QString &key, bool fallback) const;
    void write(const QString &key, const QVariant &value) const;
};
