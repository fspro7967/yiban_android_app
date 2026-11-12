#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QTextToSpeech;
class QTimer;

// 设备 facade（B2 实现）：配对、TTS、勿扰、Kiosk、SOS。
// voiceEnabled / nightQuiet 是用户设置，持久化由 B2 负责。
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
    Q_PROPERTY(bool kioskActive READ kioskActive NOTIFY kioskChanged)
    Q_PROPERTY(bool voiceEnabled READ voiceEnabled WRITE setVoiceEnabled NOTIFY voiceChanged)
    Q_PROPERTY(bool nightQuiet READ nightQuiet WRITE setNightQuiet NOTIFY voiceChanged)
    // 夜间安静此刻是否生效（开关打开且处于 22:00–07:00），设置页和提醒页据此提示。
    Q_PROPERTY(bool nightQuietActive READ nightQuietNow NOTIFY voiceChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY networkChanged)
    Q_PROPERTY(QString wifiName READ wifiName NOTIFY networkChanged)
    Q_PROPERTY(bool networkOnline READ networkOnline NOTIFY networkChanged)
    Q_PROPERTY(bool demoMode READ demoMode WRITE setDemoMode NOTIFY demoModeChanged)

public:
    explicit DeviceVM(QObject *parent = nullptr);

    QString role() const { return m_role; }
    bool paired() const { return m_paired; }
    bool audioReady() const;
    bool speaking() const;
    QStringList permissions() const;
    bool kioskActive() const { return m_kioskActive; }
    bool voiceEnabled() const { return m_voiceEnabled; }
    void setVoiceEnabled(bool on);
    bool nightQuiet() const { return m_nightQuiet; }
    void setNightQuiet(bool on);
    bool nightQuietNow() const;

    QString serverUrl() const { return m_serverUrl; }
    QString wifiName() const { return m_wifiName; }
    bool networkOnline() const { return m_networkOnline; }
    bool demoMode() const;
    void setDemoMode(bool on);

    // 管理后台：保存服务器地址（演示版只保存在内存）。
    Q_INVOKABLE bool saveNetwork(const QString &serverUrl);

    Q_INVOKABLE void pair(const QString &code, const QString &role);
    Q_INVOKABLE void unpair();
    // tag 以 "manual" 开头表示用户主动点的（再听一遍、读给我听），夜间安静不拦；
    // 其余（到点自动播报、呼救）在安静时段一律不出声。
    Q_INVOKABLE void speak(const QString &text, const QString &tag);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void sos(int level);
    Q_INVOKABLE void enterKiosk();
    Q_INVOKABLE void exitKiosk();
    Q_INVOKABLE void resetDemo(); // 仅演示用

signals:
    void pairingChanged();
    void audioChanged();
    void permissionChanged();
    void kioskChanged();
    void voiceChanged();
    void networkChanged();
    void demoModeChanged();
    void sosTriggered(int level);
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    void checkQuietWindow();

    QTextToSpeech *m_tts = nullptr;
    QTimer *m_quietTimer = nullptr;
    bool m_quietActive = false;
    QString m_role = QStringLiteral("elder");
    bool m_paired = false;
    bool m_kioskActive = false;
    bool m_voiceEnabled = true;
    bool m_nightQuiet = true;
    QString m_serverUrl = QStringLiteral("https://api.yiban.example/v1");
    QString m_wifiName = QStringLiteral("Home-2.4G");
    bool m_networkOnline = true;
};
