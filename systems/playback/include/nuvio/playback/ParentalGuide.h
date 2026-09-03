#pragma once

// Parental guide (P3a): verbatim port of Compose ParentalGuideRepository
// wire shapes (tiffara /titles/<tt>/parentsGuide, parentsGuide list,
// severityBreakdowns[{severityLevel,voteCount}], dominant-non-none-wins
// rule, severe->mild sort, max 5) with plain-English labels (Compose feeds
// localized strings; this line has no l10n infra yet).
// Fetching is a cached QObject (one in-flight guard per id); parsing is
// pure + headless-tested.

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

#include <optional>

namespace nuvio::playback {

struct ParentalWarning {
    QString label;      // Nudity | Violence | Profanity | Alcohol | Frightening
    QString severity;   // Severe | Moderate | Mild (unknown passes through)
};

/// Parses a parentsGuide response body into sorted warnings ({} when the
/// body carries no usable categories). Tolerant: unknown keys ignored,
/// missing breakdowns drop the category.
[[nodiscard]] QList<ParentalWarning> parseParentalGuide(
    const QByteArray& body);

class ParentalGuideResolver final : public QObject {
    Q_OBJECT

public:
    explicit ParentalGuideResolver(QObject* parent = nullptr);

    /// Fetches warnings for any id containing a tt token (composite ok);
    /// emits resolved() from cache when warm. No-tt ids emit failed().
    Q_INVOKABLE void fetch(const QString& id);

signals:
    void resolved(const QVariantList& warnings);  // [{label,severity}...]
    void failed();

private:
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, QVariantList> m_cache;
    bool m_inFlight = false;
};

} // namespace nuvio::playback
