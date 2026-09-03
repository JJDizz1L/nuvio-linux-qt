#pragma once

// QML-facing recorder: bridges the player (position/duration/ended signals)
// onto the Compose-parity WatchingStore. Drives resume persistence from the
// player surface; the shell passes content metadata via beginSession() so
// resume rows carry the right id/season/episode/title (cross-line readable).
//
// Record policy (mirrors WatchingPolicies.shouldStoreProgress / isProgressComplete):
//   * position must be >= 1s to persist
//   * only persist when the stored position changed by >= 10s (debounce)
//   * on completion (>= 90% fraction or ended) -> markWatched + drop resume

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <nuvio/settings/ActiveProfile.h>
#include "nuvio/watching/ContinueWatchingPrefs.h"
#include "nuvio/watching/WatchProgress.h"

namespace nuvio::watching {

class WatchingStore;

class WatchRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList continueWatching READ continueWatching
               NOTIFY continueWatchingChanged)
    /// Compose ContinueWatchingPreferences parity (continue_watching_preferences
    /// store). Map keys: visible, style ("Card"/"Wide"/"Poster"), sortMode
    /// ("DEFAULT"/"STREAMING_STYLE"/"SPLIT_UPCOMING"), episodeThumbnails,
    /// blurNextUp, unairedNextUp, resumePrompt, upNextFurthest.
    /// Style/next-up-specific knobs currently have no Qt rendering surface
    /// (no next-up candidate subsystem) — they persist for cross-line parity.
    Q_PROPERTY(QVariantMap cwPrefs READ cwPrefs NOTIFY cwPrefsChanged)
    Q_PROPERTY(bool hasResume READ hasResume NOTIFY resumeChanged)
    Q_PROPERTY(QString resumeTitle READ resumeTitle NOTIFY resumeChanged)
    Q_PROPERTY(qint64 resumePositionMs READ resumePositionMs NOTIFY resumeChanged)

public:
    explicit WatchRecorder(WatchingStore* store, QObject* parent = nullptr);

    /// Called when playback starts for an item. `season`/`episode` of -1
    /// mean "movie / no season". `nowEpochMs` stamps lastUpdatedEpochMs.
    Q_INVOKABLE void beginSession(const QString& contentType,
                                  const QString& parentMetaId,
                                  const QString& parentMetaType,
                                  const QString& videoId,
                                  const QString& title,
                                  int season, int episode,
                                  const QString& poster,
                                  qint64 nowEpochMs);

    /// Called from the player position pump: (positionMs, durationMs).
    Q_INVOKABLE void publishPosition(qint64 positionMs, qint64 durationMs);

    /// Call when playback reached the end (>= 90%) or the user finished it.
    Q_INVOKABLE void endSessionCompleted(qint64 nowEpochMs);

    /// Call when playback stops/abandons before completion (>= 5s persisted).
    Q_INVOKABLE void endSessionAbandoned();

        /// Reload the model from disk (e.g. on profile switch / app foreground).
    Q_INVOKABLE void refresh();

    /// Profile switches (P7): retargets the store + prefs, reloads the
    /// model and notifies every binding.
    void setProfileId(int profileId);

    /// Manual watched toggles (QML MetaPage button). Marks drop the resume
    /// row (Compose parity); both emit watchedChanged for the sync leg.
    Q_INVOKABLE void markWatched(const QString& type, const QString& id,
                                 int season, int episode, qint64 nowEpochMs);
    Q_INVOKABLE void unmarkWatched(const QString& type, const QString& id,
                                   int season, int episode);

    /// Resume position (ms) for a content identity, or 0 when none resumable.
    /// Mirrors Compose resume semantics: entry must be resumable (< 90 %).
    Q_INVOKABLE qint64 resumePositionMsFor(const QString& parentMetaId,
                                           int season = -1, int episode = -1);

    QVariantList continueWatching() const;

    bool hasResume() const;
    QString resumeTitle() const;
    qint64 resumePositionMs() const;

    Q_INVOKABLE bool isWatched(const QString& type, const QString& id,
                               int season = -1, int episode = -1) const;

    // ---- ContinueWatching preferences (Compose settings-page parity) ----
    Q_INVOKABLE QVariantMap cwPrefs() const;
    Q_INVOKABLE void setCwVisible(bool visible);
    Q_INVOKABLE void setCwStyle(const QString& styleName);
    Q_INVOKABLE void setCwSortMode(const QString& sortModeName);
    Q_INVOKABLE void setCwEpisodeThumbnails(bool on);
    Q_INVOKABLE void setCwBlurNextUp(bool on);
    Q_INVOKABLE void setCwUnairedNextUp(bool on);
    Q_INVOKABLE void setCwResumePrompt(bool on);
    Q_INVOKABLE void setCwUpNextFurthest(bool on);
    /// Re-reads the CW prefs store (remote-sync apply path) and emits
    /// cwPrefsChanged when anything flipped.
    void reloadContinueWatchingPrefs();

signals:
    void continueWatchingChanged();
    void resumeChanged();
    void cwPrefsChanged();
    /// Emitted whenever the local watched set gained/lost a row (drives the
    /// watched-items sync leg).
    void watchedChanged();

private:
    void rebuildModel();

    WatchingStore* m_store;
    ContinueWatchingPrefsStore m_cwPrefsStore{
        nuvio::settings::ActiveProfile::id()};
    ContinueWatchingPrefs m_cwPrefs{};
    bool m_hasSession = false;
    WatchEntry m_session{};     // current session identity + last position
    long long m_lastPersistedPositionMs = -1;
};

} // namespace nuvio::watching
