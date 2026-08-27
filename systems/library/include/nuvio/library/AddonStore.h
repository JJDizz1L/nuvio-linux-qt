#pragma once

// Add-on persistence parity layer (P4 storage sweep).
//
// TRUTH (shared with Compose, `<config>/nuvio-linux/addons.properties`,
// profile-scoped exactly like com.nuviolinux.app AddonStorage.desktop):
//   installed_addon_urls_<id>   String: JSON array of normalized manifest URLs
//                               (kotlinx List<String>, compact).
//   addon_enabled_states_<id>   String: JSON object url->bool
//                               (kotlinx Map<String,Boolean>). Missing entry
//                               means ENABLED (Compose default).
//
// CACHE (Qt-local, qt-addons.properties): fetched manifest bodies keyed by
// sha256(url) hex - opaque optimization data any side may drop. Legacy
// "addon_<n>" full-manifest entries migrate into hashed keys on first load.
//
// All codecs here are PURE/offline-testable; JSON shapes are asserted
// literally against kotlinx.serialization output in AddonRegistryTests.

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace nuvio::settings {
class PropertiesStore;
}

namespace nuvio::library {

class AddonStore final {
public:
    static constexpr int kDefaultProfileId = 1;

    /// Port of Compose AddonRepository.normalizeManifestUrl: trim, scheme map
    /// (stremio:// -> https://, bare host -> https://host), strip fragment,
    /// ensure "/manifest.json" suffix on the path (trailing slashes dropped),
    /// re-append query, percent-encode the unsafe-char table verbatim.
    [[nodiscard]] static QString normalizeManifestUrl(const QString& raw);

    [[nodiscard]] static QStringList loadInstalledUrls(
        nuvio::settings::PropertiesStore& truth,
        int profileId                                   = kDefaultProfileId);
    static void saveInstalledUrls(nuvio::settings::PropertiesStore& truth,
                                  const QStringList& urls,
                                  int profileId         = kDefaultProfileId);

    using EnabledMap = QMap<QString, bool>;   // key = normalized manifest URL

    [[nodiscard]] static EnabledMap loadEnabledStates(
        nuvio::settings::PropertiesStore& truth,
        int profileId                                   = kDefaultProfileId);
    static void saveEnabledStates(nuvio::settings::PropertiesStore& truth,
                                  const EnabledMap& states,
                                  int profileId         = kDefaultProfileId);

    [[nodiscard]] static QString cacheKeyFor(const QString& url);
    [[nodiscard]] static QByteArray loadCachedManifest(
        nuvio::settings::PropertiesStore& cache, const QString& url);
    static void saveCachedManifest(nuvio::settings::PropertiesStore& cache,
                                   const QString& url, const QByteArray& body);
    static void removeCachedManifest(nuvio::settings::PropertiesStore& cache,
                                     const QString& url);

    /// One-time port of legacy indexed "addon_<n>" manifest JSON entries into
    /// hashed cache keys (uses each entry's own "url" field). Returns true
    /// when anything was migrated; caller persists if so.
    [[nodiscard]] static bool migrateLegacyIndexedEntries(
        nuvio::settings::PropertiesStore& cache);
};

} // namespace nuvio::library