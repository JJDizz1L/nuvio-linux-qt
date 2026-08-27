#include "nuvio/watching/WatchRecorder.h"

#include <QDateTime>

#include "nuvio/watching/WatchingStore.h"

namespace nuvio::watching {

WatchRecorder::WatchRecorder(WatchingStore* store, QObject* parent)
    : QObject(parent), m_store(store),
      m_cwPrefsStore{kDefaultProfileId}
{
    m_cwPrefs = m_cwPrefsStore.load();
}

void WatchRecorder::beginSession(const QString& contentType,
                                 const QString& parentMetaId,
                                 const QString& parentMetaType,
                                 const QString& videoId,
                                 const QString& title,
                                 const int season, const int episode,
                                 const QString& poster, const qint64 nowEpochMs)
{
    m_session = WatchEntry{};
    m_session.contentType    = contentType.toStdString();
    m_session.parentMetaId   = parentMetaId.toStdString();
    m_session.parentMetaType = parentMetaType.toStdString();
    m_session.videoId        = videoId.toStdString();
    m_session.title          = title.toStdString();
    m_session.poster         = poster.toStdString().empty() ? std::nullopt
                                                            : std::optional<std::string>(poster.toStdString());
    m_session.season  = (season >= 0) ? std::optional<int>(season) : std::nullopt;
    m_session.episode = (episode >= 0) ? std::optional<int>(episode) : std::nullopt;
    m_session.source  = kLocalProgressSource;
    m_session.lastUpdatedEpochMs = nowEpochMs;
    m_lastPersistedPositionMs = -1;
    m_hasSession = true;
    rebuildModel();
}

void WatchRecorder::publishPosition(const qint64 positionMs,
                                    const qint64 durationMs)
{
    if (!m_hasSession) return;
    // shouldStoreProgress: only persist once the user has watched >= 1s.
    if (positionMs < kResumeStoreThresholdMs) return;

    m_session.lastPositionMs = positionMs;
    m_session.durationMs     = durationMs;
    m_session.lastUpdatedEpochMs =
        QDateTime::currentMSecsSinceEpoch();

    // debounce: only persist on a >= 10s advance (>= 10 s drift).
    const long long delta = positionMs - m_lastPersistedPositionMs;
    if (delta >= 10'000 || m_lastPersistedPositionMs < 0) {
        // Compose only persists when a position is actually stored; mirror the
        // >=1s threshold and the 10s hysteresis in one upsert.
        m_session.lastPositionMs = positionMs;
        m_store->upsert(m_session);
        m_lastPersistedPositionMs = positionMs;
        emit resumeChanged();
    }
}

void WatchRecorder::markWatched(const QString& type, const QString& id,
                                int season, int episode, qint64 nowEpochMs)
{
    const auto s = season < 0 ? std::optional<int>() : std::optional<int>(season);
    const auto e = episode < 0 ? std::optional<int>() : std::optional<int>(episode);
    m_store->markWatched(type.toStdString(), id.toStdString(), s, e,
                         nowEpochMs);
    // Compose parity: marking a movie watched drops its resume row.
    m_store->remove(nuvio::watching::buildProgressKey(id.toStdString(), s, e));
    rebuildModel();
    emit watchedChanged();
}

void WatchRecorder::unmarkWatched(const QString& type, const QString& id,
                                  int season, int episode)
{
    const auto s = season < 0 ? std::optional<int>() : std::optional<int>(season);
    const auto e = episode < 0 ? std::optional<int>() : std::optional<int>(episode);
    m_store->unmarkWatched(type.toStdString(), id.toStdString(), s, e);
    rebuildModel();
    emit watchedChanged();
}

void WatchRecorder::endSessionCompleted(const qint64 nowEpochMs)
{
    if (!m_hasSession) return;
    m_session.lastPositionMs = m_session.durationMs > 0
                                   ? m_session.durationMs
                                   : m_session.lastPositionMs;
    m_session.lastUpdatedEpochMs = nowEpochMs;
    m_session.isCompleted = true;
    m_session.progressPercent = 100.0f;
    m_store->upsert(m_session);
    // Mark watched via the content identity key (Compose WatchedRepository.local).
    m_store->markWatched(
        m_session.contentType, m_session.parentMetaId,
        m_session.season, m_session.episode, nowEpochMs);
    // Drop the resume row — the item is now in the watched set.
    m_store->remove(m_session.resolvedProgressKey());
    emit watchedChanged();
    m_hasSession = false;
    m_session = WatchEntry{};
    m_lastPersistedPositionMs = -1;
    rebuildModel();
}

void WatchRecorder::endSessionAbandoned()
{
    if (!m_hasSession) return;
    // Persist whatever >=1s position we have, then close the session.
    if (m_session.lastPositionMs >= kResumeStoreThresholdMs) {
        m_session.lastUpdatedEpochMs =
            QDateTime::currentMSecsSinceEpoch();
        m_store->upsert(m_session);
    }
    m_hasSession = false;
    m_session = WatchEntry{};
    m_lastPersistedPositionMs = -1;
    rebuildModel();
}

void WatchRecorder::refresh()
{
    rebuildModel();
}

void WatchRecorder::rebuildModel()
{
    emit continueWatchingChanged();
    emit resumeChanged();
}

qint64 WatchRecorder::resumePositionMsFor(const QString& parentMetaId,
                                          const int season, const int episode)
{
    const std::string id = parentMetaId.toStdString();
    const std::optional<int> s = season >= 0 ? std::optional<int>(season)
                                             : std::nullopt;
    const std::optional<int> e = episode >= 0 ? std::optional<int>(episode)
                                              : std::nullopt;
    for (const auto& entry : m_store->loadEntries()) {
        if (entry.parentMetaId != id || entry.season != s
            || entry.episode != e)
            continue;
        // Resumable only (Compose drops >=90 % rows from resume).
        if (!entry.isResumable()) return 0;
        // Guard near-complete positions (< 90 % of duration).
        return entry.lastPositionMs;
    }
    return 0;
}

QVariantList WatchRecorder::continueWatching() const
{
    QVariantList out;
    for (const auto& e : m_store->loadEntries()) {
        if (!e.isResumable()) continue;
        QVariantMap m;
        m.insert("title", QString::fromStdString(e.title));
        m.insert("poster", e.poster ? QString::fromStdString(*e.poster) : QString());
        m.insert("type", QString::fromStdString(e.contentType));
        m.insert("id", QString::fromStdString(e.parentMetaId));
        m.insert("season", e.season.value_or(-1));
        m.insert("episode", e.episode.value_or(-1));
        m.insert("positionMs", static_cast<qlonglong>(e.lastPositionMs));
        m.insert("durationMs", static_cast<qlonglong>(e.durationMs));
        m.insert("fraction", e.progressFraction());
        m.insert("progressKey", QString::fromStdString(e.resolvedProgressKey()));
        out.append(m);
    }
    return out;
}

bool WatchRecorder::hasResume() const
{
    return m_store->loadEntries().size() > 0;
}

QString WatchRecorder::resumeTitle() const
{
    const auto entries = m_store->loadEntries();
    return entries.empty() ? QString()
                           : QString::fromStdString(entries.front().title);
}

qint64 WatchRecorder::resumePositionMs() const
{
    const auto entries = m_store->loadEntries();
    if (entries.empty()) return 0;
    // Resume position mirrors Compose's resolveResumePosition.
    return entries.front().lastPositionMs;
}

bool WatchRecorder::isWatched(const QString& type, const QString& id,
                              int season, int episode) const
{
    return m_store->isWatched(type.toStdString(), id.toStdString(),
                              season >= 0 ? std::optional<int>(season) : std::nullopt,
                              episode >= 0 ? std::optional<int>(episode) : std::nullopt);
}


// ---- ContinueWatching preferences (Compose settings-page parity) ----------

QVariantMap WatchRecorder::cwPrefs() const
{
    const auto styleName = [](CwStyle v) {
        switch (v) {
        case CwStyle::Wide:   return QStringLiteral("Wide");
        case CwStyle::Poster: return QStringLiteral("Poster");
        case CwStyle::Card:   return QStringLiteral("Card");
        }
        return QStringLiteral("Card");
    };
    const auto sortName = [](CwSortMode v) {
        switch (v) {
        case CwSortMode::StreamingStyle: return QStringLiteral("STREAMING_STYLE");
        case CwSortMode::SplitUpcoming:  return QStringLiteral("SPLIT_UPCOMING");
        case CwSortMode::Default:        return QStringLiteral("DEFAULT");
        }
        return QStringLiteral("DEFAULT");
    };
    return QVariantMap{
        {QStringLiteral("visible"), m_cwPrefs.isVisible},
        {QStringLiteral("style"), styleName(m_cwPrefs.style)},
        {QStringLiteral("sortMode"), sortName(m_cwPrefs.sortMode)},
        {QStringLiteral("episodeThumbnails"), m_cwPrefs.useEpisodeThumbnails},
        {QStringLiteral("blurNextUp"), m_cwPrefs.blurNextUp},
        {QStringLiteral("unairedNextUp"), m_cwPrefs.showUnairedNextUp},
        {QStringLiteral("resumePrompt"), m_cwPrefs.showResumePromptOnLaunch},
        {QStringLiteral("upNextFurthest"), m_cwPrefs.upNextFromFurthestEpisode},
    };
}

void WatchRecorder::setCwVisible(const bool visible)
{
    if (m_cwPrefs.isVisible == visible) return;
    m_cwPrefs.isVisible = visible;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwStyle(const QString& styleName)
{
    CwStyle v = CwStyle::Card;
    if (styleName == QLatin1String("Wide")) v = CwStyle::Wide;
    else if (styleName == QLatin1String("Poster")) v = CwStyle::Poster;
    else if (styleName != QLatin1String("Card")) return;
    if (m_cwPrefs.style == v) return;
    m_cwPrefs.style = v;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwSortMode(const QString& sortModeName)
{
    CwSortMode v = CwSortMode::Default;
    if (sortModeName == QLatin1String("STREAMING_STYLE"))
        v = CwSortMode::StreamingStyle;
    else if (sortModeName == QLatin1String("SPLIT_UPCOMING"))
        v = CwSortMode::SplitUpcoming;
    else if (sortModeName != QLatin1String("DEFAULT")) return;
    if (m_cwPrefs.sortMode == v) return;
    m_cwPrefs.sortMode = v;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwEpisodeThumbnails(const bool on)
{
    if (m_cwPrefs.useEpisodeThumbnails == on) return;
    m_cwPrefs.useEpisodeThumbnails = on;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwBlurNextUp(const bool on)
{
    if (m_cwPrefs.blurNextUp == on) return;
    m_cwPrefs.blurNextUp = on;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwUnairedNextUp(const bool on)
{
    if (m_cwPrefs.showUnairedNextUp == on) return;
    m_cwPrefs.showUnairedNextUp = on;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwResumePrompt(const bool on)
{
    if (m_cwPrefs.showResumePromptOnLaunch == on) return;
    m_cwPrefs.showResumePromptOnLaunch = on;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

void WatchRecorder::setCwUpNextFurthest(const bool on)
{
    if (m_cwPrefs.upNextFromFurthestEpisode == on) return;
    m_cwPrefs.upNextFromFurthestEpisode = on;
    m_cwPrefsStore.save(m_cwPrefs);
    emit cwPrefsChanged();
}

} // namespace nuvio::watching
