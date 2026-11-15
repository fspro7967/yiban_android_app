#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class ConnectionProbe;
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
    // 手机当前的联网状态，读自系统（QNetworkInformation），不是服务器是否可达——那个看 testConnection。
    //   networkStatus: online（能上外网）/ limited（连着网但只到局域网）/ portal（要网页登录的 Wi-Fi）
    //                  / offline（没联网）/ unknown（这台设备读不到）
    //   networkType:   Wi-Fi / 移动数据 / 有线网络 / 蓝牙，读不到为空串
    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkChanged)
    Q_PROPERTY(QString networkType READ networkType NOTIFY networkChanged)
    Q_PROPERTY(bool networkOnline READ networkOnline NOTIFY networkChanged)
    Q_PROPERTY(bool demoMode READ demoMode WRITE setDemoMode NOTIFY demoModeChanged)
    Q_PROPERTY(bool testingConnection READ testingConnection NOTIFY testingConnectionChanged)

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
    QString networkStatus() const;
    QString networkType() const;
    bool networkOnline() const { return networkStatus() == QStringLiteral("online"); }
    bool demoMode() const;
    void setDemoMode(bool on);

    // 管理后台：保存服务器地址（演示版只保存在内存）。
    Q_INVOKABLE bool saveNetwork(const QString &serverUrl);
    // 管理后台：对这个地址真发一次请求看能不能连上（不要求先保存）。
    // 异步，结果走 connectionTested；地址不合法时也从 connectionTested 回来。
    Q_INVOKABLE void testConnection(const QString &serverUrl);
    bool testingConnection() const;

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
    void testingConnectionChanged();
    void connectionTested(bool ok, const QString &message);
    void sosTriggered(int level);
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    void checkQuietWindow();

    QTextToSpeech *m_tts = nullptr;
    QTimer *m_quietTimer = nullptr;
    ConnectionProbe *m_probe = nullptr;
    QString m_probeUrl;
    bool m_quietActive = false;
    QString m_role = QStringLiteral("elder");
    bool m_paired = false;
    bool m_kioskActive = false;
    bool m_voiceEnabled = true;
    bool m_nightQuiet = true;
    QString m_serverUrl = QStringLiteral("https://api.yiban.example/v1");
    bool m_networkInfoReady = false; // 系统联网状态后端是否加载成功
};
