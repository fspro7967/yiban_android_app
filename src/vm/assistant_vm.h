#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class MsgSender;
class QJSEngine;
class QQmlEngine;

// AI 助手 facade：把一句话发给大模型，拿回一句回复。
//
// 底下是 include/msgsender.h 的 MsgSender，支持两种接入：OpenAI 兼容的云端 API，
// 和局域网里的 Ollama。配置由 MsgSender 存进 QSettings，重启后还在。
//
// 这一层只管「问一句、答一句」，不碰提醒状态——对话与执行分层（R6-6）：
// 以后语音链路接进来，也是 ASR 文本 → ask() → replyReady → TTS，
// 要改提醒必须另走 ReminderVM 的受控动作，不能从这里绕过去。
class AssistantVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // 0 = OpenAI 兼容 API，1 = 本地 Ollama（与 MsgSender::Provider 一致）
    // 切接入方式单独通知：只发 configChanged 的话，界面上绑定着地址/密钥的输入框会被
    // 一起刷新，没保存的输入就丢了。
    Q_PROPERTY(int provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl NOTIFY configChanged)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY configChanged)
    Q_PROPERTY(QString apiModel READ apiModel NOTIFY configChanged)
    Q_PROPERTY(QString ollamaUrl READ ollamaUrl NOTIFY configChanged)
    Q_PROPERTY(QString ollamaModel READ ollamaModel NOTIFY configChanged)
    // 当前接入方式需要的几项是否都填了（只查有没有填，不查能不能连上）。
    Q_PROPERTY(bool configured READ configured NOTIFY configuredChanged)
    // 有一句话发出去了还没回来。
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    // C++ 侧与 QML 用的是同一个对象。
    static AssistantVM *instance();
    static AssistantVM *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    int provider() const;
    void setProvider(int provider);
    QString apiBaseUrl() const;
    QString apiKey() const;
    QString apiModel() const;
    QString ollamaUrl() const;
    QString ollamaModel() const;
    bool configured() const;
    bool busy() const { return m_busy; }

    // 保存并持久化；缺项或地址不是 http(s) 时返回 false 并发 commandRejected(bad_ai_config)。
    Q_INVOKABLE bool saveApiConfig(const QString &baseUrl, const QString &apiKey,
                                   const QString &model);
    Q_INVOKABLE bool saveOllamaConfig(const QString &serverUrl, const QString &model);
    // 异步。成功走 replyReady，失败走 failed；上一句没回完时再问会被拒绝（ai_busy）。
    Q_INVOKABLE void ask(const QString &text);

signals:
    void providerChanged();
    void configChanged();
    void configuredChanged();
    void busyChanged();
    void replyReady(const QString &question, const QString &reply);
    void failed(const QString &message);
    void commandRejected(const QString &code, const QString &message);
    void toast(const QString &text);

private:
    explicit AssistantVM(QObject *parent = nullptr);
    void setBusy(bool busy);

    MsgSender *m_sender = nullptr;
    bool m_busy = false;
    QString m_question;
};
