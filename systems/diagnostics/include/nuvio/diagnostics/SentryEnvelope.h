#pragma once

// Sentry envelope wire format (Appendix A, Sentry): minimal builder for
// the SDK-less transport (error/message events only). Envelope shape per
// the Sentry protocol: header line, item header line, event payload.
// DSN form: {protocol}://{publicKey}@{host}[:port]/{projectId}.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace nuvio::diagnostics {

struct SentryDsn {
    QString scheme = QStringLiteral("https");
    QString host;
    int port = -1;
    QString publicKey;
    QString projectId;
    [[nodiscard]] bool valid() const
    {
        return !host.isEmpty() && !publicKey.isEmpty() &&
               !projectId.isEmpty();
    }
};

[[nodiscard]] SentryDsn parseSentryDsn(const QString& dsn);
[[nodiscard]] QString sentryEnvelopeUrl(const SentryDsn& dsn);
[[nodiscard]] QString sentryAuthHeader(const SentryDsn& dsn,
                                      const QString& clientId);
[[nodiscard]] QString newSentryEventId();   // 32 lowercase hex chars

struct SentryBreadcrumb {
    QString timestamp;   // ISO-8601 UTC
    QString category;
    QString message;
};

struct SentryEvent {
    QString eventId = newSentryEventId();
    QString timestamp;   // ISO-8601 UTC, defaults to now at build time
    QString level = QStringLiteral("error");
    QString logger;
    QString transaction;
    QString message;     // plain message event (empty for exceptions)
    QString exceptionType;
    QString exceptionValue;
    QJsonObject tags;
    QString release;
    QString dist;
    QString environment;
    QList<SentryBreadcrumb> breadcrumbs;
};

/// Empty object when the sanitizer drops the event (envelope skipped).
[[nodiscard]] QJsonObject buildSentryEventJson(const SentryEvent& event);
[[nodiscard]] QByteArray buildSentryEnvelope(const QJsonObject& eventJson);

} // namespace nuvio::diagnostics
