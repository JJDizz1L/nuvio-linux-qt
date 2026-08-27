#include "nuvio/watching/WatchProgress.h"

#include <algorithm>
#include <limits>
#include <map>

namespace nuvio::watching {

// Compose resolvedProgressKey(): explicit progressKey wins, else the synthetic
// buildProgressKey(parentMetaId, season, episode).
std::string WatchEntry::resolvedProgressKey() const
{
    if (progressKey && !progressKey->empty())
        return *progressKey;
    return buildProgressKey(parentMetaId, season, episode);
}

// Compose progressFraction: explicit percent if present, else position/duration.
float WatchEntry::progressFraction() const
{
    if (progressPercent) {
        const float p = *progressPercent / 100.0f;
        return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    }
    if (durationMs > 0) {
        const float f = static_cast<float>(lastPositionMs) /
                        static_cast<float>(durationMs);
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }
    return 0.0f;
}

// Compose isEffectivelyCompleted: completed flag || 90%-fraction || 100% pos.
bool WatchEntry::isEffectivelyCompleted() const
{
    if (isCompleted) return true;
    const float f = progressFraction();
    if (f >= kCompletionThresholdFraction) return true;
    if (durationMs > 0 && lastPositionMs >= durationMs) return true;
    return false;
}

// --- freshness comparator (mirrors watchProgressEntryFreshnessComparator) ---

static int cmpOptInt(const std::optional<int>& a, const std::optional<int>& b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return (*a < *b) ? -1 : 1;
}

static int cmpStrOpt(const std::optional<std::string>& a,
                     const std::optional<std::string>& b)
{
    const std::string av = a.value_or("");
    const std::string bv = b.value_or("");
    if (av == bv) return 0;
    return av < bv ? -1 : 1;
}

static int cmpStrVal(const std::string& a, const std::string& b)
{
    if (a == b) return 0;
    return a < b ? -1 : 1;
}

bool progressFreshnessLess(const WatchEntry& a, const WatchEntry& b)
{
    auto tie = [](long long x, long long y) -> int {
        if (x == y) return 0;
        return x < y ? -1 : 1;
    };
    int c = 0;
    if ((c = tie(a.lastUpdatedEpochMs, b.lastUpdatedEpochMs))) return c < 0;
    if ((c = tie(a.lastPositionMs, b.lastPositionMs))) return c < 0;
    if ((c = tie(a.durationMs, b.durationMs))) return c < 0;
    if ((c = cmpStrVal(a.videoId, b.videoId))) return c < 0;
    if ((c = cmpStrVal(a.parentMetaId, b.parentMetaId))) return c < 0;
    if ((c = cmpStrVal(a.contentType, b.contentType))) return c < 0;
    if ((c = cmpOptInt(a.season, b.season))) return c < 0;
    if ((c = cmpOptInt(a.episode, b.episode))) return c < 0;
    if ((c = (a.isCompleted < b.isCompleted) ? -1 :
            (a.isCompleted > b.isCompleted) ? 1 : 0))
        return c < 0;
    if ((c = (a.progressPercent < b.progressPercent) ? -1 :
            (a.progressPercent > b.progressPercent) ? 1 : 0))
        return c < 0;
    if ((c = cmpStrVal(a.source, b.source))) return c < 0;
    if ((c = cmpStrVal(a.parentMetaType, b.parentMetaType))) return c < 0;
    if ((c = cmpStrVal(a.title, b.title))) return c < 0;
    if ((c = cmpStrOpt(a.logo, b.logo))) return c < 0;
    if ((c = cmpStrOpt(a.poster, b.poster))) return c < 0;
    if ((c = cmpStrOpt(a.background, b.background))) return c < 0;
    if ((c = cmpStrOpt(a.episodeTitle, b.episodeTitle))) return c < 0;
    if ((c = cmpStrOpt(a.episodeThumbnail, b.episodeThumbnail))) return c < 0;
    if ((c = cmpStrOpt(a.providerName, b.providerName))) return c < 0;
    if ((c = cmpStrOpt(a.providerAddonId, b.providerAddonId))) return c < 0;
    // tracking fields elided (Qt line never populates provider rows)
    return cmpStrVal(a.progressKey.value_or(""), b.progressKey.value_or(""));
}

static bool freshnessMore(const WatchEntry& a, const WatchEntry& b)
{
    return progressFreshnessLess(b, a);
}

// --- continue-watching selection (mirrors Compose newestByProgressKey) -------

struct LogicalIdentity {
    std::string parentMetaId;
    std::optional<int> season;
    std::optional<int> episode;
    bool operator<(const LogicalIdentity& o) const
    {
        if (parentMetaId != o.parentMetaId) return parentMetaId < o.parentMetaId;
        if (season != o.season) {
            if (!season) return true;
            if (!o.season) return false;
            return *season < *o.season;
        }
        if (episode != o.episode) {
            if (!episode) return true;
            if (!o.episode) return false;
            return *episode < *o.episode;
        }
        return false;
    }
};

static LogicalIdentity identityOf(const WatchEntry& e)
{
    return {e.parentMetaId, e.season, e.episode};
}

std::vector<WatchEntry> continueWatchingSelection(std::vector<WatchEntry> entries,
                                                   const int limit)
{
    // 1) newest-per-logical-identity (Compose newestByProgressKey).
        std::map<LogicalIdentity, WatchEntry> newest;
    for (auto& e : entries) {
        auto it = newest.find(identityOf(e));
        if (it == newest.end() || progressFreshnessLess(it->second, e))
            newest.insert_or_assign(identityOf(e), std::move(e));
    }

    // 2) resumable only (!isEffectivelyCompleted).
    std::vector<WatchEntry> resumable;
    for (auto& kv : newest)
        if (kv.second.isResumable())
            resumable.push_back(std::move(kv.second));

    // 3) newest-first, 4) limit.
    std::sort(resumable.begin(), resumable.end(), freshnessMore);
    if (static_cast<int>(resumable.size()) > limit)
        resumable.resize(limit);
    return resumable;
}

} // namespace nuvio::watching

