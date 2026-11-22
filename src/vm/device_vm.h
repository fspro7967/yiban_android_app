#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>
#include <QtQml/qqmlregistration.h>

class QTextToSpeech;
class QTimer;
class HttpClient;
class AlarmTtsRenderer;

// 设备 facade（B2 实现）：配对、TTS、权限、通知/闹钟、Kiosk、SOS。
//
// 分工口径（agent/三人组前后端分工.md §2.5）：
//   * 本类只"执行"系统动作：发通知、定闹钟、拨号、改静音状态；
//   * 不判业务状态——哪条提醒该响由 AppCore 的状态机决定，
//     本类只订阅 AppCore::reminderDue 去呈现；
//   * QML 只读属性、只调 Q_INVOKABLE，不接触 Android API。
class DeviceVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString role READ role NOTIFY pairingChanged)
    Q_PROPERTY(bool paired READ paired NOTIFY pairingChanged)
    Q_PROPERTY(bool audioReady READ audioReady NOTIFY audioChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY audioChanged)
    Q_PROPERTY(QStringList permissions READ permissions NOTIFY permissionChanged)
    // I3：逐项权限诊断 {key,label,granted,canOpenSettings}，F1 权限页直接当 model。
    Q_PROPERTY(QVariantList permissionDiagnostics READ permissionDiagnostics NOTIFY permissionChanged)
    Q_PROPERTY(bool kioskActive READ kioskActive NOTIFY kioskChanged)
    Q_PROPERTY(bool voiceEnabled READ voiceEnabled WRITE setVoiceEnabled NOTIFY voiceChanged)
    Q_PROPERTY(bool nightQuiet READ nightQuiet WRITE setNightQuiet NOTIFY voiceChanged)
    // 夜间安静此刻是否生效（开关打开且处于 22:00–07:00），设置页和提醒页据此提示。
    Q_PROPERTY(bool nightQuietActive READ nightQuietNow NOTIFY voiceChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY networkChanged)
    Q_PROPERTY(QString wifiName READ wifiName NOTIFY networkChanged)
    Q_PROPERTY(bool networkOnline READ networkOnline NOTIFY networkChanged)
    // 是否已经拿到令牌（配对成功或手工粘贴）；管理页据此提示要不要重新配对。
    Q_PROPERTY(bool hasToken READ hasToken NOTIFY tokenChanged)
    // 最近的网络交换日志（最多 80 行，格式见 http_client.h 的 NetworkLog）。
    // 管理页直接绑定它就能看到"发出去了没有、重试了几次、耗了多久"。
    Q_PROPERTY(QStringList networkLog READ networkLog NOTIFY networkLogChanged)
    Q_PROPERTY(bool demoMode READ demoMode WRITE setDemoMode NOTIFY demoModeChanged)

public:
    explicit DeviceVM(QObject *parent = nullptr);

    QString role() const { return m_role; }
    bool paired() const { return m_paired; }
    bool audioReady() const;
    bool speaking() const;
    QStringList permissions() const;
    QVariantList permissionDiagnostics() const { return m_permissionDiagnostics; }
    bool kioskActive() const { return m_kioskActive; }
    bool voiceEnabled() const { return m_voiceEnabled; }
    void setVoiceEnabled(bool on);
    bool nightQuiet() const { return m_nightQuiet; }
    void setNightQuiet(bool on);
    bool nightQuietNow() const;

    QString serverUrl() const { return m_serverUrl; }
    QString wifiName() const { return m_wifiName; }
    bool networkOnline() const { return m_networkOnline; }
    bool hasToken() const { return m_hasToken; }
    QStringList networkLog() const { return m_networkLog; }
    bool demoMode() const;
    void setDemoMode(bool on);

    // 管理后台：保存服务器地址，并落盘（重启后仍然生效）。
    Q_INVOKABLE bool saveNetwork(const QString &serverUrl);
    // 管理后台：手工填入服务端签发的令牌。配对码失效时的兜底通道。
    // 传空串表示清除令牌（同时解除配对）。
    Q_INVOKABLE bool saveToken(const QString &token);
    // 管理后台：真实探测 /v1/state，结果用 connectionTested 回给页面。
    // 假连接一旦点下去就报"正常"，联调时根本分不清是没发出去还是服务器拒了。
    Q_INVOKABLE void testConnection();
    // 管理后台：清空网络日志（发一版对比日志之前先清一次，看起来干净）
    Q_INVOKABLE void clearNetworkLog();

    Q_INVOKABLE void pair(const QString &code, const QString &role);
    Q_INVOKABLE void unpair();
    // tag 以 "manual" 开头表示用户主动点的（再听一遍、读给我听），夜间安静不拦；
    // "sos" 是呼救播报，任何时段都必须出声；其余（到点播报）在安静时段一律不出声。
    Q_INVOKABLE void speak(const QString &text, const QString &tag);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void sos(int level);
    Q_INVOKABLE void enterKiosk();
    Q_INVOKABLE void exitKiosk();
    Q_INVOKABLE void resetDemo(); // 仅演示用

    // 设置页"检查权限"入口用；桌面构建是空操作
    Q_INVOKABLE void requestNotificationPermission();
    Q_INVOKABLE void requestExactAlarmPermission();
    // I3：按 key 跳到对应系统设置页，返回 false 表示桌面构建或不认识的 key。
    Q_INVOKABLE bool openSystemSettings(const QString &key);
    // Android 14+ 的"全屏通知"开关：不开的话锁屏只响不亮
    Q_INVOKABLE void requestFullScreenPermission();

signals:
    void pairingChanged();
    void audioChanged();
    void permissionChanged();
    void kioskChanged();
    void voiceChanged();
    void networkChanged();
    void networkLogChanged();
    void tokenChanged();
    // 连接自检结果：ok + 面向管理员的中文结论（含具体失败原因）。
    void connectionTested(bool ok, const QString &message);
    void demoModeChanged();
    void sosTriggered(int level);
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    void tick();                 // 2s：夜间窗口 / 权限 / 联网状态
    void refreshPermissions();
    void refreshNetwork();
    void rescheduleAlarms();     // 按今天的 occurrence 重排精确闹钟
    void onReminderDue(const QVariantMap &occurrence);
    void handlePairReply(quint64 requestId, int status, const QJsonObject &body);
    void handlePairFailed(quint64 requestId, const QString &code);
    void handleTestReply(quint64 requestId, int status, const QJsonObject &body);
    void handleTestFailed(quint64 requestId, const QString &code);
    void finishPair(bool ok, const QString &role, const QString &reason);

    QTextToSpeech *m_tts = nullptr;   // 仅桌面用：Android 上走系统 TTS（Platform::speak）
    QTimer *m_tickTimer = nullptr;
    QTimer *m_speakTimer = nullptr;  // 估算播报时长，用来把 speaking 复位
    HttpClient *m_http = nullptr;
    AlarmTtsRenderer *m_alarmTts = nullptr; // Android：离线 TTS 预渲染到点播报 WAV

    QStringList m_permissionCache;
    QVariantList m_permissionDiagnostics;
    QStringList m_networkLog;
    quint64 m_pairRequestId = 0;
    quint64 m_testRequestId = 0;
    bool m_hasToken = false;

    bool m_quietActive = false;
    bool m_paired = false;
    bool m_pairing = false;
    bool m_kioskActive = false;
    bool m_voiceEnabled = true;
    bool m_nightQuiet = true;
    bool m_warnedExactAlarm = false;
    bool m_warnedFullScreen = false;
    bool m_speaking = false;
    QString m_role = QStringLiteral("elder");
    QString m_serverUrl = QStringLiteral("http://101.245.109.50:8000/v1");
    QString m_wifiName = QStringLiteral("Home-2.4G");
    bool m_networkOnline = true;
};
