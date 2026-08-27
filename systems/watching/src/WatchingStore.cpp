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
    auto entries = loadEntries();
    const std::string key = entry.resolvedProgressKey();
    bool replaced = false;
    for (auto& e : entries) {
        if (e.resolvedProgressKey() == key) {
            e = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        entries.push_back(entry);
    m_progressStore->putString(
        profileKey("watch_progress"),
        WatchCodec::encodeProgress(entries).toStdString());
}

void WatchingStore::remove(const std::string& progressKey)
{
    auto entries = loadEntries();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                 [&](const WatchEntry& e) {
                     return e.resolvedProgressKey() == progressKey;
                 }), entries.end());
    m_progressStore->putString(
        profileKey("watch_progress"),
        WatchCodec::encodeProgress(entries).toStdString());
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
    auto items = loadWatchedItems();
    const std::string key = buildWatchedKey(type, id, season, episode);
    for (const auto& w : items)
        if (buildWatchedKey(w.type, w.id, w.season, w.episode) == key)
            return; // idempotent: don't duplicate
    WatchedItem w;
    w.type = type;
    w.id = id;
    w.season = season;
    w.episode = episode;
    w.markedAtEpochMs = markedAtEpochMs;
    items.push_back(std::move(w));
    m_watchedStore->putString(
        profileKey("watched"),
        WatchCodec::encodeWatched(items).toStdString());
}

void WatchingStore::unmarkWatched(const std::string& type, const std::string& id,
                                  const std::optional<int> season,
                                  const std::optional<int> episode)
{
    const std::string key = buildWatchedKey(type, id, season, episode);
    auto items = loadWatchedItems();
    items.erase(std::remove_if(items.begin(), items.end(),
                 [&](const WatchedItem& w) {
                     return buildWatchedKey(w.type, w.id, w.season, w.episode) == key;
                 }), items.end());
    m_watchedStore->putString(
        profileKey("watched"),
        WatchCodec::encodeWatched(items).toStdString());
}

} // namespace nuvio::watching

