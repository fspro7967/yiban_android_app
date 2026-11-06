#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QTextToSpeech;

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

    Q_INVOKABLE void pair(const QString &code, const QString &role);
    Q_INVOKABLE void unpair();
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
    void sosTriggered(int level);
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    bool nightQuietNow() const;

    QTextToSpeech *m_tts = nullptr;
    QString m_role = QStringLiteral("elder");
    bool m_paired = false;
    bool m_kioskActive = false;
    bool m_voiceEnabled = true;
    bool m_nightQuiet = true;
};
