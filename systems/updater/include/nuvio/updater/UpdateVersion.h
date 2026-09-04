#pragma once

// Update version compare (Appendix A, updater): verbatim port of Compose's
// private VersionUtils (normalize v/V prefix; dot/dash/underscore split
// with leading-digit tokens; zero-padded numeric compare; unparseable
// fallback is a plain inequality of the normalized strings).

#include <QList>
#include <QString>

namespace nuvio::updater {

[[nodiscard]] QString normalizeVersion(const QString& raw);
[[nodiscard]] QList<int> parseVersionParts(const QString& raw,
                                          bool* ok = nullptr);
[[nodiscard]] bool isRemoteNewer(const QString& remote, const QString& local);

} // namespace nuvio::updater
