#include "nuvio/watching/WatchingStore.h"

#include <algorithm>
#include <map>

#include "nuvio/watching/WatchCodec.h"

namespace nuvio::watching {

WatchingStore::WatchingStore(const int profileId)
    : m_profileId(profileId),
      m_progressStore(std::make_unique<nuvio::settings::PropertiesStore>(
          std::filesystem::path(
              nuvio::settings::PropertiesStore::defaultPath("watch_progress")))),
      m_watchedStore(std::make_unique<nuvio::settings::PropertiesStore>(
          std::filesystem::path(
              nuvio::settings::PropertiesStore::defaultPath("watched"))))
{}

WatchingStore::WatchingStore(const QString& progressFile,
                             const QString& watchedFile,
                             const int profileId)
    : m_profileId(profileId),
      m_progressStore(std::make_unique<nuvio::settings::PropertiesStore>(
          std::filesystem::path(progressFile.toStdString()))),
      m_watchedStore(std::make_unique<nuvio::settings::PropertiesStore>(
          std::filesystem::path(watchedFile.toStdString())))
{}

std::string WatchingStore::profileKey(const char* base) const
{
    return std::string(base) + "_" + std::to_string(m_profileId);
}

StoredProgressPayload WatchingStore::loadProgressPayload()
{
    const auto raw = m_progressStore->getString(profileKey("watch_progress"));
    return WatchCodec::decodeProgress(
        raw ? QString::fromStdString(*raw) : QString());
}

void WatchingStore::saveProgressPayload(const StoredProgressPayload& p)
{
    m_progressStore->putString(profileKey("watch_progress"),
        WatchCodec::encodeProgressPayload(p).toStdString());
}

StoredWatchedPayload WatchingStore::loadWatchedPayload()
{
    const auto raw = m_watchedStore->getString(profileKey("watched"));
    return WatchCodec::decodeWatchedPayload(
        raw ? QString::fromStdString(*raw) : QString());
}

void WatchingStore::saveWatchedPayload(const StoredWatchedPayload& p)
{
    m_watchedStore->putString(profileKey("watched"),
        WatchCodec::encodeWatchedPayload(p).toStdString());
}

// ---- sync envelope ----------------------------------------------------------

WatchingStore::ProgressEnvelope WatchingStore::loadProgressEnvelope()
{
    const auto p = loadProgressPayload();
    ProgressEnvelope env;
    env.lastSuccessfulPushEpochMs = p.lastSuccessfulPushEpochMs;
    env.deltaCursorEventId        = p.deltaCursorEventId;
    env.deltaInitialized          = p.deltaInitialized;
    env.dirtyProgressKeys         = p.dirtyProgressKeys;
    return env;
}

void WatchingStore::markProgressDirty(const std::string& progressKey)
{
    auto p = loadProgressPayload();
    if (std::find(p.dirtyProgressKeys.begin(), p.dirtyProgressKeys.end(),
                  progressKey) == p.dirtyProgressKeys.end()) {
        p.dirtyProgressKeys.push_back(progressKey);
    }
    saveProgressPayload(p);
}

void WatchingStore::clearProgressDirty(
    const std::vector<std::string>& keys)
{
    auto p = loadProgressPayload();
    for (const auto& k : keys)
        p.dirtyProgressKeys.erase(
            std::remove(p.dirtyProgressKeys.begin(),
                        p.dirtyProgressKeys.end(), k),
            p.dirtyProgressKeys.end());
    saveProgressPayload(p);
}

void WatchingStore::setDeltaCursor(long long eventId, bool initialized)
{
    auto p = loadProgressPayload();
    p.deltaCursorEventId = eventId;
    p.deltaInitialized   = initialized;
    saveProgressPayload(p);
}

void WatchingStore::setLastSuccessfulPush(long long epochMs)
{
    auto p = loadProgressPayload();
    p.lastSuccessfulPushEpochMs = epochMs;
    saveProgressPayload(p);
}

std::vector<std::string> WatchingStore::dirtyWatchedKeys()
{
    return loadWatchedPayload().dirtyWatchedKeys;
}

void WatchingStore::clearWatchedDirtyKeys(
    const std::vector<std::string>& keys)
{
    auto p = loadWatchedPayload();
    for (const auto& k : keys)
        p.dirtyWatchedKeys.erase(
            std::remove(p.dirtyWatchedKeys.begin(),
                        p.dirtyWatchedKeys.end(), k),
            p.dirtyWatchedKeys.end());
    saveWatchedPayload(p);
}

void WatchingStore::setWatchedCursor(long long eventId, bool initialized)
{
    auto p = loadWatchedPayload();
    p.deltaCursorEventId = eventId;
    p.deltaInitialized   = initialized;
    saveWatchedPayload(p);
}

long long WatchingStore::watchedDeltaCursorEventId()
{
    return loadWatchedPayload().deltaCursorEventId;
}

bool WatchingStore::watchedDeltaInitialized()
{
    return loadWatchedPayload().deltaInitialized;
}

void WatchingStore::upsertWatchedRemote(const WatchedItem& item)
{
    auto p = loadWatchedPayload();
    const std::string key =
        buildWatchedKey(item.type, item.id, item.season, item.episode);
    bool replaced = false;
    for (auto& w : p.items) {
        if (buildWatchedKey(w.type, w.id, w.season, w.episode) == key) {
            w = item;
            replaced = true;
            break;
        }
    }
    if (!replaced) p.items.push_back(item);
    saveWatchedPayload(p);
}

void WatchingStore::removeWatchedByContentId(
    const std::string& contentId, std::optional<int> season,
    std::optional<int> episode)
{
    auto p = loadWatchedPayload();
    p.items.erase(
        std::remove_if(p.items.begin(), p.items.end(),
                       [&](const WatchedItem& w) {
                           return w.id == contentId && w.season == season &&
                                  w.episode == episode;
                       }),
        p.items.end());
    saveWatchedPayload(p);
}

std::vector<WatchEntry> WatchingStore::loadEntries()
{
    const auto raw = m_progressStore->getString(profileKey("watch_progress"));
    if (!raw) return {};
    StoredProgressPayload payload =
        WatchCodec::decodeProgress(QString::fromStdString(*raw));
    // Normalize (resolved key + completed/percent) and collapse to newest per
    // key, sorted newest-first — mirrors Compose decodePayload().
    std::map<std::string, WatchEntry> byKey;
    for (auto& e : payload.entries) {
        e.progressKey = e.resolvedProgressKey();
        e.isCompleted = e.isEffectivelyCompleted();
        if (e.isCompleted && !e.progressPercent) e.progressPercent = 100.0f;
        if (e.isCompleted) e.lastPositionMs = e.durationMs; // completed pins at end
        auto it = byKey.find(e.progressKey.value_or(""));
        if (it == byKey.end() || progressFreshnessLess(it->second, e))
            byKey.insert_or_assign(e.progressKey.value_or(""), e);
    }
    std::vector<WatchEntry> out;
    out.reserve(byKey.size());
    for (auto& kv : byKey) out.push_back(std::move(kv.second));
    std::sort(out.begin(), out.end(),
              [](const WatchEntry& a, const WatchEntry& b) {
                  return progressFreshnessLess(b, a);
              });
    return out;
}

void WatchingStore::upsert(const WatchEntry& entry)
{
    auto payload = loadProgressPayload();
    const std::string key = entry.resolvedProgressKey();
    bool replaced = false;
    for (auto& e : payload.entries) {
        if (e.resolvedProgressKey() == key) {
            e = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        payload.entries.push_back(entry);
    if (std::find(payload.dirtyProgressKeys.begin(),
                  payload.dirtyProgressKeys.end(), key) ==
        payload.dirtyProgressKeys.end())
        payload.dirtyProgressKeys.push_back(key);
    saveProgressPayload(payload);
}

void WatchingStore::upsertRemote(const WatchEntry& entry)
{
    auto payload = loadProgressPayload();
    const std::string key = entry.resolvedProgressKey();
    bool replaced = false;
    for (auto& e : payload.entries) {
        if (e.resolvedProgressKey() == key) {
            e = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced) payload.entries.push_back(entry);
    saveProgressPayload(payload);
}

void WatchingStore::remove(const std::string& progressKey)
{
    auto payload = loadProgressPayload();
    payload.entries.erase(
        std::remove_if(payload.entries.begin(), payload.entries.end(),
                       [&](const WatchEntry& e) {
                           return e.resolvedProgressKey() == progressKey;
                       }),
        payload.entries.end());
    if (std::find(payload.dirtyProgressKeys.begin(),
                  payload.dirtyProgressKeys.end(), progressKey) ==
        payload.dirtyProgressKeys.end())
        payload.dirtyProgressKeys.push_back(progressKey);
    saveProgressPayload(payload);
}

// --- watched ----------------------------------------------------------------

std::vector<WatchedItem> WatchingStore::loadWatchedItems()
{
    const auto raw = m_watchedStore->getString(profileKey("watched"));
    if (!raw) return {};
    return WatchCodec::decodeWatched(QString::fromStdString(*raw));
}

std::vector<std::string> WatchingStore::watchedKeys()
{
    std::vector<std::string> keys;
    for (const auto& w : loadWatchedItems())
        keys.push_back(buildWatchedKey(w.type, w.id, w.season, w.episode));
    return keys;
}

bool WatchingStore::isWatched(const std::string& type, const std::string& id,
                              const std::optional<int> season,
                              const std::optional<int> episode)
{
    const std::string key = buildWatchedKey(type, id, season, episode);
    for (const auto& k : watchedKeys())
        if (k == key) return true;
    return false;
}

void WatchingStore::markWatched(const std::string& type, const std::string& id,
                                const std::optional<int> season,
                                const std::optional<int> episode,
                                const long long markedAtEpochMs)
{
    auto payload = loadWatchedPayload();
    const std::string key = buildWatchedKey(type, id, season, episode);
    for (const auto& w : payload.items)
        if (buildWatchedKey(w.type, w.id, w.season, w.episode) == key)
            return; // idempotent: don't duplicate
    WatchedItem w;
    w.type = type;
    w.id = id;
    w.season = season;
    w.episode = episode;
    w.markedAtEpochMs = markedAtEpochMs;
    payload.items.push_back(std::move(w));
    if (std::find(payload.dirtyWatchedKeys.begin(),
                  payload.dirtyWatchedKeys.end(), key) ==
        payload.dirtyWatchedKeys.end())
        payload.dirtyWatchedKeys.push_back(key);
    saveWatchedPayload(payload);
}

void WatchingStore::unmarkWatched(const std::string& type, const std::string& id,
                                  const std::optional<int> season,
                                  const std::optional<int> episode)
{
    const std::string key = buildWatchedKey(type, id, season, episode);
    auto payload = loadWatchedPayload();
    payload.items.erase(std::remove_if(payload.items.begin(),
                        payload.items.end(),
                        [&](const WatchedItem& w) {
                            return buildWatchedKey(w.type, w.id, w.season,
                                                   w.episode) == key;
                        }),
                        payload.items.end());
    if (std::find(payload.dirtyWatchedKeys.begin(),
                  payload.dirtyWatchedKeys.end(), key) ==
        payload.dirtyWatchedKeys.end())
        payload.dirtyWatchedKeys.push_back(key);
    saveWatchedPayload(payload);
}

} // namespace nuvio::watching

