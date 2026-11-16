#include "assistant_vm.h"

#include "app_core.h"
#include "facade_errors.h"
#include "msgsender.h"

#include <QCoreApplication>
#include <QQmlEngine>
#include <QUrl>

namespace {

bool isHttpUrl(const QString &text)
{
    const QUrl url(text.trimmed());
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}

} // namespace

AssistantVM *AssistantVM::instance()
{
    static AssistantVM *vm = new AssistantVM(QCoreApplication::instance());
    return vm;
}

AssistantVM *AssistantVM::create(QQmlEngine *, QJSEngine *)
{
    AssistantVM *vm = instance();
    // 对象挂在 QCoreApplication 下，不能让 QML 引擎在退出时再删一次。
    QQmlEngine::setObjectOwnership(vm, QQmlEngine::CppOwnership);
    return vm;
}

AssistantVM::AssistantVM(QObject *parent)
    : QObject(parent)
    , m_sender(new MsgSender(this))
{
    // MsgSender 对每次 sendMessage 恰好给一个结果：成功是 responseReceived，
    // 失败是 errorOccurred（配置缺失这类问题会在 sendMessage 里当场同步发出）。
    connect(m_sender, &MsgSender::responseReceived, this, [this](const QString &reply) {
        const QString question = m_question;
        m_question.clear();
        setBusy(false);
        emit replyReady(question, reply);
    });
    connect(m_sender, &MsgSender::errorOccurred, this, [this](const QString &error) {
        m_question.clear();
        setBusy(false);
        emit failed(error);
    });
}

int AssistantVM::provider() const
{
    return int(m_sender->provider());
}

void AssistantVM::setProvider(int provider)
{
    const MsgSender::Provider next = provider == int(MsgSender::ProviderOllama)
        ? MsgSender::ProviderOllama : MsgSender::ProviderApi;
    if (m_sender->provider() == next)
        return;
    m_sender->setProvider(next);
    AppCore::instance()->logOperation(
        QStringLiteral("管理"),
        next == MsgSender::ProviderOllama ? QStringLiteral("AI 接入方式改为本地 Ollama")
                                          : QStringLiteral("AI 接入方式改为云端 API"));
    emit providerChanged();
    emit configuredChanged();
}

QString AssistantVM::apiBaseUrl() const { return m_sender->apiBaseUrl(); }
QString AssistantVM::apiKey() const { return m_sender->apiKey(); }
QString AssistantVM::apiModel() const { return m_sender->apiModelName(); }
QString AssistantVM::ollamaUrl() const { return m_sender->ollamaUrl(); }
QString AssistantVM::ollamaModel() const { return m_sender->ollamaModelName(); }

bool AssistantVM::configured() const
{
    if (m_sender->provider() == MsgSender::ProviderOllama)
        return !m_sender->ollamaUrl().isEmpty() && !m_sender->ollamaModelName().isEmpty();
    return !m_sender->apiBaseUrl().isEmpty() && !m_sender->apiKey().isEmpty()
        && !m_sender->apiModelName().isEmpty();
}

bool AssistantVM::saveApiConfig(const QString &baseUrl, const QString &apiKey, const QString &model)
{
    if (!isHttpUrl(baseUrl) || apiKey.trimmed().isEmpty() || model.trimmed().isEmpty()) {
        emit commandRejected(QStringLiteral("bad_ai_config"),
                             facadeErrorText(QStringLiteral("bad_ai_config")));
        return false;
    }
    m_sender->setApiConfig(baseUrl, apiKey, model);
    // 密钥不进操作日志。
    AppCore::instance()->logOperation(
        QStringLiteral("管理"),
        QStringLiteral("保存云端 API 设置：%1（模型 %2）").arg(baseUrl.trimmed(), model.trimmed()));
    emit configChanged();
    emit configuredChanged();
    return true;
}

bool AssistantVM::saveOllamaConfig(const QString &serverUrl, const QString &model)
{
    if (!isHttpUrl(serverUrl) || model.trimmed().isEmpty()) {
        emit commandRejected(QStringLiteral("bad_ai_config"),
                             facadeErrorText(QStringLiteral("bad_ai_config")));
        return false;
    }
    m_sender->setOllamaConfig(serverUrl, model);
    AppCore::instance()->logOperation(
        QStringLiteral("管理"),
        QStringLiteral("保存 Ollama 设置：%1（模型 %2）").arg(serverUrl.trimmed(), model.trimmed()));
    emit configChanged();
    emit configuredChanged();
    return true;
}

void AssistantVM::ask(const QString &text)
{
    if (m_busy) {
        emit commandRejected(QStringLiteral("ai_busy"), facadeErrorText(QStringLiteral("ai_busy")));
        return;
    }
    // 先置忙再发：出错的回调可能在 sendMessage 返回之前就到，它会把忙标志清回去。
    m_question = text.trimmed();
    setBusy(true);
    m_sender->sendMessage(m_question);
}

void AssistantVM::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
