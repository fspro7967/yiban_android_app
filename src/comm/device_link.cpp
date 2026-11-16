#include "device_link.h"

#include <QJsonDocument>

QByteArray DeviceLink::encodeLine(const QString &topic, const QJsonObject &payload)
{
    QJsonObject message;
    message.insert(QStringLiteral("topic"), topic);
    message.insert(QStringLiteral("payload"), payload);
    // Compact 输出里不会有裸换行（字符串里的换行会被转义成 \n），所以一行就是一条。
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

void DeviceLink::feed(const QByteArray &bytes)
{
    m_rx.append(bytes);

    for (;;) {
        const qsizetype newline = m_rx.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = m_rx.left(newline);
        m_rx.remove(0, newline + 1);
        if (m_discarding) {
            m_discarding = false;   // 超长那行到此为止，下一行重新正常解析
            continue;
        }
        handleLine(line);
    }

    if (m_rx.size() > kMaxLineBytes) {
        m_rx.clear();
        m_discarding = true;
    }
}

void DeviceLink::resetFraming()
{
    m_rx.clear();
    m_discarding = false;
}

void DeviceLink::handleLine(const QByteArray &rawLine)
{
    const QByteArray line = rawLine.trimmed();   // 顺带吃掉 "\r\n" 的 '\r'
    if (line.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(line);
    const QJsonObject message = doc.object();
    const QString topic = message.value(QStringLiteral("topic")).toString();
    const QJsonValue payload = message.value(QStringLiteral("payload"));
    // payload 可以省略（当作空对象），但写了就必须是对象。
    if (!doc.isObject() || topic.isEmpty() || !(payload.isObject() || payload.isUndefined())) {
        emit malformed(line);
        return;
    }
    emit received(topic, payload.toObject());
}
