#pragma once

// Active profile id (P7): process-wide current profile index (1..6,
// Compose MAX_PROFILES parity), defaulting to 1. Mirrors Compose's
// ProfileRepository.activeProfileId global that every ProfileScopedKey
// resolves through.
//
// Why a global instead of plumbing: ~15 stores compute profile-suffixed
// keys per ACCESS (not per instance), so switching profiles requires no
// store reconstruction - only id reads flip. Objects that DO cache
// profile-bound state (WatchingStore, LibraryStore, ...) get explicit
// setProfileId()+reload slots driven by ProfileManager::activeProfileChanged.

#include <atomic>

namespace nuvio::settings {

class ActiveProfile final {
public:
    static constexpr int kDefault = 1;
    static constexpr int kMax = 6;   // server-validated 1..6 (Tier-1)

    [[nodiscard]] static int id()
    {
        const int v = s_id.load(std::memory_order_relaxed);
        return (v >= 1 && v <= kMax) ? v : kDefault;
    }

    static void setId(int profileId)
    {
        if (profileId < 1 || profileId > kMax) return;
        s_id.store(profileId, std::memory_order_relaxed);
    }

private:
    inline static std::atomic<int> s_id{kDefault};
};

} // namespace nuvio::settings
