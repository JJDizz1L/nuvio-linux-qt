#include "nuvio/authsync/SyncRpcClient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

namespace nuvio::authsync {

SyncRpcClient::SyncRpcClient(AuthConfig cfg, TokenProvider token,
                             QObject* parent)
    : QObject(parent),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_nam(new QNetworkAccessManager(this))
{
}

SyncRpcClient::~SyncRpcClient() = default;

void SyncRpcClient::call(const QString& fnName, const QJsonObject& params)
{
    QNetworkRequest req{QUrl(QString::fromUtf8(
        m_cfg.rpcUrl(fnName.toUtf8().constData())))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("apikey", m_cfg.anonKey);

    const QByteArray userJwt = m_token ? m_token() : QByteArray();
    const QByteArray bearer =
        !userJwt.isEmpty() ? userJwt : m_cfg.anonKey;
    if (!bearer.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + bearer);

    const QByteArray body =
        QJsonDocument(params).toJson(QJsonDocument::Compact);

    auto* rep = m_nam->post(req, body);
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const int status =
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = rep->readAll();

        if (rep->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300) {
            qWarning("SyncRpc: rpc failed (http=%d): %s", status,
                     raw.constData());
            emit finished(false, status, QJsonDocument(QJsonObject{
                {QStringLiteral("error"), QString::fromUtf8(raw)}}));
            return;
        }

        emit finished(true, status, QJsonDocument::fromJson(raw));
    });
}

} // namespace nuvio::authsync