#include "app_settings.h"

#include <QSettings>
#include <QUuid>

namespace {
// 键名统一加前缀：以后要和"别的模块的偏好"混在同一个 QSettings 里也不会撞。
constexpr auto kServerUrl = "net/serverUrl";
constexpr auto kToken = "net/token";
constexpr auto kDeviceId = "net/deviceId";
constexpr auto kPaired = "net/paired";
constexpr auto kRole = "net/role";
constexpr auto kVoice = "voice/enabled";
constexpr auto kNightQuiet = "voice/nightQuiet";
constexpr auto kLastSync = "sync/lastSyncTs";

// 与 DeviceVM 里的默认值保持一致：语音开、夜间安静开。
constexpr auto kDefaultServerUrl = "https://api.yiban.example/v1";
} // namespace

AppSettings *AppSettings::instance()
{
    static AppSettings settings;
    return &settings;
}

QString AppSettings::readString(const QString &key, const QString &fallback) const
{
    const QSettings s;
    return s.value(key, fallback).toString();
}

bool AppSettings::readBool(const QString &key, bool fallback) const
{
    const QSettings s;
    return s.value(key, fallback).toBool();
}

void AppSettings::write(const QString &key, const QVariant &value) const
{
    QSettings s;
    s.setValue(key, value);
    s.sync();
}

QString AppSettings::serverUrl() const
{
    return readString(kServerUrl, QString::fromLatin1(kDefaultServerUrl));
}

void AppSettings::setServerUrl(const QString &url)
{
    write(kServerUrl, url);
}

QString AppSettings::token() const
{
    return readString(kToken);
}

void AppSettings::setToken(const QString &token)
{
    write(kToken, token);
}

void AppSettings::clearToken()
{
    QSettings s;
    s.remove(kToken);
    s.sync();
}

QString AppSettings::deviceId() const
{
    QString id = readString(kDeviceId);
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        write(kDeviceId, id);
    }
    return id;
}

bool AppSettings::paired() const
{
    return readBool(kPaired, false);
}

QString AppSettings::role() const
{
    return readString(kRole, QStringLiteral("elder"));
}

void AppSettings::setPairing(bool paired, const QString &role)
{
    write(kPaired, paired);
    if (!role.isEmpty())
        write(kRole, role);
    else if (!paired)
        write(kRole, QStringLiteral("elder"));
}

bool AppSettings::voiceEnabled() const
{
    return readBool(kVoice, true);
}

void AppSettings::setVoiceEnabled(bool on)
{
    write(kVoice, on);
}

bool AppSettings::nightQuiet() const
{
    return readBool(kNightQuiet, true);
}

void AppSettings::setNightQuiet(bool on)
{
    write(kNightQuiet, on);
}

qint64 AppSettings::lastSyncTs() const
{
    const QSettings s;
    return s.value(kLastSync, 0).toLongLong();
}

void AppSettings::setLastSyncTs(qint64 ts)
{
    write(kLastSync, ts);
}

void AppSettings::reset()
{
    QSettings s;
    s.clear();
    s.sync();
}
