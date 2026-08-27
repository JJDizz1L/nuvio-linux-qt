#pragma once

// Storage-backed watch-state repository. Reads/writes the Compose-shared
// `watch_progress.properties` / `watched.properties` files under the active
// profile key (Compose `watch_progress_<profileId>` / `watched_<profileId>`;
// the Qt line's default active profile is 1 — Compose's default
// activeProfileIndex). Profile selection isn't wired in the Qt shell yet
// (P4); mirroring the primary profile keeps files cross-line readable.
//
// Byte-for-byte compatible with Compose's DesktopStorage.Properties format
// (UTF-16 \uXXXX escapes, key/value separation, atomic 0600 rewrite).

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>

#include <nuvio/settings/PropertiesStore.h>
#include "nuvio/watching/WatchProgress.h"

namespace nuvio::watching {

class WatchingStore {
public:
    /// Production: reads/writes the Compose shared files for `profileId`.
    explicit WatchingStore(int profileId = kDefaultProfileId);
    /// Test: explicit files (keeps tests out of the live XDG config dir).
    WatchingStore(const QString& progressFile,
                  const QString& watchedFile,
                  int profileId);

    /// Decode + normalize (resolved progressKey, newest-per-key, freshness-sorted).
    std::vector<WatchEntry> loadEntries();
    /// Full upsert: replaces or appends by resolved progressKey, normalized.
    void upsert(const WatchEntry& entry);
    /// Drop a resume row (e.g. on completion).
    void remove(const std::string& progressKey);

    /// Watched items (Compose StoredWatchedPayload.items shape).
    std::vector<WatchedItem> loadWatchedItems();
    /// Derived watched keys (Compose WatchedUiState.watchedKeys).
    std::vector<std::string> watchedKeys();
    bool isWatched(const std::string& type, const std::string& id,
                   std::optional<int> season = std::nullopt,
                   std::optional<int> episode = std::nullopt);
    void markWatched(const std::string& type, const std::string& id,
                     std::optional<int> season, std::optional<int> episode,
                     long long markedAtEpochMs);
    void unmarkWatched(const std::string& type, const std::string& id,
                       std::optional<int> season,
                       std::optional<int> episode);

private:
        std::string profileKey(const char* base) const;

    int m_profileId = kDefaultProfileId;
    std::unique_ptr<nuvio::settings::PropertiesStore> m_progressStore;
    std::unique_ptr<nuvio::settings::PropertiesStore> m_watchedStore;
};

} // namespace nuvio::watching
