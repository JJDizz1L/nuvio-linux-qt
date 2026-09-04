#pragma once

// Debrid stream display values (D2): builds the template value map from
// OUR resolved-stream shape (title/url/source + provider id). The full
// Compose formatter feeds filename-parser facts (resolution/codec/tags);
// those ride the metadata backfill backlog — unknown fields render empty
// (engine semantics), so user templates degrade gracefully rather than
// breaking. formatName/formatDescription apply the Compose post-steps
// (line join + whitespace collapse / blank-line filtering).

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace nuvio::debrid {

class StreamTemplateEngine;

/// Value map for one stream: stream.title, stream.type ("Debrid" for
/// debrid-resolved rows, else the source), service.shortName/name
/// (provider short/display name, "" when unknown), addon.name.
[[nodiscard]] QVariantMap debridStreamValues(const QString& title,
                                             const QString& sourceName,
                                             const QString& providerId);

[[nodiscard]] QString formatStreamName(const StreamTemplateEngine& engine,
                                       const QString& nameTemplate,
                                       const QVariantMap& values,
                                       const QString& fallback);
[[nodiscard]] QString formatStreamDescription(
    const StreamTemplateEngine& engine, const QString& descriptionTemplate,
    const QVariantMap& values, const QString& fallback);

} // namespace nuvio::debrid
