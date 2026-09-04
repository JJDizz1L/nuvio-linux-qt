#include "nuvio/diagnostics/SentryEnvelope.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include "nuvio/diagnostics/SentrySanitizer.h"

namespace nuvio::diagnostics {

SentryDsn parseSentryDsn(const QString& dsn)
{
    SentryDsn out;
    const QString trimmed = dsn.trimmed();
    if (trimmed.isEmpty()) return out;
    const int schemeEnd = trimmed.indexOf("://");
    if (schemeEnd < 0) return out;
    out.scheme = trimmed.left(schemeEnd).toLower();
    if (out.scheme != "http" && out.scheme != "https") {
        out = SentryDsn{};
        return out;
    }
    const QString rest = trimmed.mid(schemeEnd + 3);
    const int atPos = rest.indexOf(u'@');
    if (atPos < 0) return SentryDsn{};
    out.publicKey = rest.left(atPos);
    const QString hostPath = rest.mid(atPos + 1);
    const int slashPos = hostPath.indexOf(u'/');
    if (slashPos < 0) return SentryDsn{};
    QString hostPort = hostPath.left(slashPos);
    out.projectId = hostPath.mid(slashPos + 1).trimmed();
    const int colonPos = hostPort.indexOf(u':');
    if (colonPos >= 0) {
        bool ok = false;
        out.port = hostPort.mid(colonPos + 1).toInt(&ok);
        if (!ok) return SentryDsn{};
        hostPort = hostPort.left(colonPos);
    }
    out.host = hostPort;
    if (!out.valid()) return SentryDsn{};
    return out;
}

QString sentryEnvelopeUrl(const SentryDsn& dsn)
{
    QString url = dsn.scheme + QStringLiteral("://") + dsn.host;
    if (dsn.port >= 0) url += u':' + QString::number(dsn.port);
    return url + QStringLiteral("/api/") + dsn.projectId +
           QStringLiteral("/envelope/");
}

QString sentryAuthHeader(const SentryDsn& dsn, const QString& clientId)
{
    return QStringLiteral("Sentry sentry_version=7, sentry_client=") +
           clientId + QStringLiteral(", sentry_key=") + dsn.publicKey;
}

QString newSentryEventId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
        .remove(u'-')
        .toLower();
}

namespace {

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QStringList eventTexts(const SentryEvent& event)
{
    QStringList texts;
    if (!event.message.isEmpty()) texts.append(event.message);
    if (!event.logger.isEmpty()) texts.append(event.logger);
    if (!event.transaction.isEmpty()) texts.append(event.transaction);
    if (!event.exceptionType.isEmpty()) texts.append(event.exceptionType);
    if (!event.exceptionValue.isEmpty())
        texts.append(event.exceptionValue);
    return texts;
}

} // namespace

QJsonObject buildSentryEventJson(const SentryEvent& event)
{
    if (shouldDropSentryEvent(eventTexts(event))) return {};
    QJsonObject out{
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"),
         event.timestamp.isEmpty() ? utcNow() : event.timestamp},
        {QStringLiteral("platform"), QStringLiteral("native")},
        {QStringLiteral("level"), event.level},
    };
    // No request / user / serverName keys ever (sanitizer parity).
    if (!event.logger.isEmpty())
        out.insert(QStringLiteral("logger"), event.logger);
    if (!event.transaction.isEmpty())
        out.insert(QStringLiteral("transaction"), event.transaction);
    if (!event.message.isEmpty())
        out.insert(QStringLiteral("message"), event.message);
    if (!event.exceptionType.isEmpty() || !event.exceptionValue.isEmpty()) {
        out.insert(QStringLiteral("exception"),
                   QJsonObject{
                       {QStringLiteral("values"),
                        QJsonArray{QJsonObject{
                            {QStringLiteral("type"), event.exceptionType},
                            {QStringLiteral("value"),
                             event.exceptionValue},
                            {QStringLiteral("mechanism"),
                             QJsonObject{
                                 {QStringLiteral("type"),
                                  QStringLiteral("generic")},
                                 {QStringLiteral("handled"), false},
                             }},
                        }}},
                   });
    }
    if (!event.tags.isEmpty())
        out.insert(QStringLiteral("tags"), event.tags);
    if (!event.release.isEmpty())
        out.insert(QStringLiteral("release"), event.release);
    if (!event.dist.isEmpty())
        out.insert(QStringLiteral("dist"), event.dist);
    if (!event.environment.isEmpty())
        out.insert(QStringLiteral("environment"), event.environment);
    if (!event.breadcrumbs.isEmpty()) {
        QJsonArray crumbs;
        for (const SentryBreadcrumb& crumb : event.breadcrumbs) {
            crumbs.append(QJsonObject{
                {QStringLiteral("timestamp"), crumb.timestamp},
                {QStringLiteral("type"), QStringLiteral("default")},
                {QStringLiteral("category"), crumb.category},
                {QStringLiteral("message"), crumb.message},
                {QStringLiteral("level"), QStringLiteral("info")},
            });
        }
        out.insert(QStringLiteral("breadcrumbs"),
                   QJsonObject{{QStringLiteral("values"), crumbs}});
    }
    return out;
}

QByteArray buildSentryEnvelope(const QJsonObject& eventJson)
{
    if (eventJson.isEmpty()) return {};
    const QByteArray payload =
        QJsonDocument(eventJson).toJson(QJsonDocument::Compact);
    const QString eventId =
        eventJson.value(QStringLiteral("event_id")).toString();
    QByteArray envelope =
        QJsonDocument(QJsonObject{
                          {QStringLiteral("event_id"), eventId},
                          {QStringLiteral("sent_at"), utcNow()},
                      })
            .toJson(QJsonDocument::Compact);
    envelope.append('\n');
    envelope.append(QJsonDocument(QJsonObject{
                                      {QStringLiteral("type"),
                                       QStringLiteral("event")},
                                      {QStringLiteral("content_type"),
                                       QStringLiteral("application/json")},
                                      {QStringLiteral("length"),
                                       payload.size()},
                                  })
                        .toJson(QJsonDocument::Compact));
    envelope.append('\n');
    envelope.append(payload);
    return envelope;
}

} // namespace nuvio::diagnostics
