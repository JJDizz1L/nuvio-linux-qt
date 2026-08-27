#pragma once

#include <QString>
#include <QStringList>

namespace nuvio::watching {

// Compose parity: WatchProgressModels.kt enum NAME strings on the wire.
enum class CwStyle { Card, Wide, Poster };
enum class CwSortMode { Default, StreamingStyle, SplitUpcoming };

/// Port of Compose's private StoredContinueWatchingPreferences — field for
/// field, @SerialName renames included. Defaults are Compose's defaults.
struct ContinueWatchingPrefs {
    bool isVisible = true;
    CwStyle style = CwStyle::Card;
    bool upNextFromFurthestEpisode = true;
    bool useEpisodeThumbnails = true;
    bool showUnairedNextUp = true;
    bool blurNextUp = false;
    QStringList dismissedNextUpKeys;
    bool showResumePromptOnLaunch = true;
    CwSortMode sortMode = CwSortMode::Default;

    friend bool operator==(const ContinueWatchingPrefs& a,
                           const ContinueWatchingPrefs& b)
    {
        return a.isVisible == b.isVisible && a.style == b.style &&
               a.upNextFromFurthestEpisode ==
                   b.upNextFromFurthestEpisode &&
               a.useEpisodeThumbnails == b.useEpisodeThumbnails &&
               a.showUnairedNextUp == b.showUnairedNextUp &&
               a.blurNextUp == b.blurNextUp &&
               a.dismissedNextUpKeys == b.dismissedNextUpKeys &&
               a.showResumePromptOnLaunch == b.showResumePromptOnLaunch &&
               a.sortMode == b.sortMode;
    }
};

/// kotlinx-parity JSON codec (encodeDefaults=true, ignoreUnknownKeys).
/// Compose decodes under runCatching: ANY malformed piece (bad JSON,
/// unknown enum name) discards the whole payload to defaults — decode
/// mirrors that all-or-nothing behavior exactly.
class ContinueWatchingPrefsCodec {
public:
    [[nodiscard]] static ContinueWatchingPrefs decode(const QString& json);
    [[nodiscard]] static QString encode(const ContinueWatchingPrefs& prefs);
};

/// Profile-scoped access to the Compose store
/// `continue_watching_preferences` (DesktopStorage parity: properties file
/// of the same name, key `continue_watching_preferences_<profileId>`).
class ContinueWatchingPrefsStore {
public:
    explicit ContinueWatchingPrefsStore(int profileId);

    [[nodiscard]] ContinueWatchingPrefs load() const;
    void save(const ContinueWatchingPrefs& prefs);

private:
    int m_profileId;
};

} // namespace nuvio::watching