#pragma once

// Stream reuse-link cache (P3a): verbatim port of Compose
// features/streams/StreamLinkCacheRepository (contentKey shapes, Java-fold
// hashed storage keys, CachedStreamLink camelCase JSON, expiring-credential
// table, maxAge<=0 disables, corrupt/expired entries evicted on read).
// Store file "stream_link_cache" (bare name, nuvio-linux dir); entry keys
// carry the profile suffix ("stream_link_<ulong>_<profileId>", live-proven
// against the Compose-written cache).

#include <QObject>
#include <QString>

#include <optional>

namespace nuvio::playback {

struct CachedLink {
    QString url;
    QString streamName;
    QString addonName;
    QString addonId;
    qint64 cachedAtMs = 0;
};

/// "type|videoId" or "type|parentMeta|sS|eE|videoId" (type lowercased).
[[nodiscard]] QString streamLinkContentKey(
    const QString& type, const QString& videoId,
    const QString& parentMetaId = {}, int season = -1, int episode = -1);

/// Java String.hashCode fold (*31 over UTF-16 units, 64-bit accumulator)
/// rendered ulong — ASCII-identical to Kotlin for real content ids.
[[nodiscard]] QString streamLinkHashedKey(const QString& contentKey);

/// Query-param keys/framgments that mark a playback URL as expiring
/// (verbatim PlaybackUrlCredentials table) — such urls are never cached.
[[nodiscard]] bool urlHasExpiringCredentials(const QString& url);

/// Profile-scoped entry access over the shared store.
class StreamLinkCache final {
public:
    explicit StreamLinkCache(int profileId = 1);

    void save(const QString& contentKey, const CachedLink& link,
              qint64 nowEpochMs);
    /// Fresh entry or nullopt (missing/corrupt/expired/expiring/blank
    /// entries are evicted on read, Compose parity).
    [[nodiscard]] std::optional<CachedLink> getValid(
        const QString& contentKey, qint64 maxAgeMs, qint64 nowEpochMs);

private:
    int m_profileId;
};

} // namespace nuvio::playback
