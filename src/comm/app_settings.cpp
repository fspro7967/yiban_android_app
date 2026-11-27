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
constexpr auto kSpeechRate = "voice/rate";
constexpr auto kSpeechPitch = "voice/pitch";
constexpr auto kLastSync = "sync/lastSyncTs";
constexpr auto kStateSeq = "sync/stateSeq";
constexpr auto kPlansSeq = "sync/plansSeq";

// CareMate 云端默认地址（组内部署），设置页可改。
constexpr auto kDefaultServerUrl = "http://101.245.109.50:8000/v1";
} // namespace

QString normalizeServerUrl(const QString &url)
{
    QString trimmed = url.trimmed();
    if (!trimmed.startsWith(QStringLiteral("https://"))
        && !trimmed.startsWith(QStringLiteral("http://")))
        return QString();
    if (trimmed.length() < 12 || trimmed.length() > 200)
        return QString();
    while (trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    if (!trimmed.endsWith(QStringLiteral("/v1")))
        trimmed += QStringLiteral("/v1");
    return trimmed;
}

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

double AppSettings::readDouble(const QString &key, double fallback) const
{
    const QSettings s;
    bool ok = false;
    const double v = s.value(key, fallback).toDouble(&ok);
    return ok ? v : fallback;
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

double AppSettings::speechRate() const
{
    const double rate = readDouble(kSpeechRate, 0.9);
    return rate >= 0.5 && rate <= 2.0 ? rate : 0.9; // 越界值当没设过
}

void AppSettings::setSpeechRate(double rate)
{
    if (rate < 0.5 || rate > 2.0)
        return;
    write(kSpeechRate, rate);
}

double AppSettings::speechPitch() const
{
    const double pitch = readDouble(kSpeechPitch, 1.0);
    return pitch >= 0.5 && pitch <= 2.0 ? pitch : 1.0;
}

void AppSettings::setSpeechPitch(double pitch)
{
    if (pitch < 0.5 || pitch > 2.0)
        return;
    write(kSpeechPitch, pitch);
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

qint64 AppSettings::stateSeq() const
{
    const QSettings s;
    return s.value(kStateSeq, 0).toLongLong();
}

void AppSettings::setStateSeq(qint64 seq)
{
    write(kStateSeq, seq);
}

qint64 AppSettings::plansSeq() const
{
    const QSettings s;
    return s.value(kPlansSeq, 0).toLongLong();
}

void AppSettings::setPlansSeq(qint64 seq)
{
    write(kPlansSeq, seq);
}

void AppSettings::reset()
{
    QSettings s;
    s.clear();
    s.sync();
}
