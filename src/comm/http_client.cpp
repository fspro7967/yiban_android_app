#include "http_client.h"

#include <QTimer>

OfflineHttpClient::OfflineHttpClient(QObject *parent)
    : HttpClient(parent)
{
}

quint64 OfflineHttpClient::postJson(const QString &path, const QJsonObject &body)
{
    Q_UNUSED(path)
    Q_UNUSED(body)
    const quint64 id = m_nextId++;
    QTimer::singleShot(0, this, [this, id] {
        emit failed(id, QStringLiteral("offline"));
    });
    return id;
}

quint64 OfflineHttpClient::getJson(const QString &path, const QUrlQuery &query)
{
    Q_UNUSED(path)
    Q_UNUSED(query)
    const quint64 id = m_nextId++;
    QTimer::singleShot(0, this, [this, id] {
        emit failed(id, QStringLiteral("offline"));
    });
    return id;
}
