#include "nuvio/playback/PlaybackSession.h"

#include <QDateTime>

#include "nuvio/p2p/P2pEngine.h"
#include "nuvio/playback/NextEpisodeRules.h"
#include "nuvio/playback/StreamLinkCache.h"
#include "nuvio/playback/StreamResolver.h"

namespace nuvio::playback {

namespace {
// Accepts the payload into the session properties (single funnel so the
// S/E parse can never drift between call sites).
void acceptSession(QString& title, QString& url, QString& type, QString& id,
                   int& season, int& episode, bool& isRelay,
                   const QString& newTitle, const QString& newUrl,
                   const QString& newType, const QString& newId,
                   bool newIsRelay)
{
    title = newTitle;
    url = newUrl;
    type = newType;
    id = newId;
    const CompositeId parts = splitCompositeId(newId);
    season = parts.season;
    episode = parts.episode;
    isRelay = newIsRelay;
}
} // namespace

PlaybackSession::PlaybackSession(StreamResolver* resolver, QObject* parent)
    : PlaybackSession(resolver, nullptr, parent) {}

PlaybackSession::PlaybackSession(StreamResolver* resolver,
                                 nuvio::p2p::P2pEngine* p2p, QObject* parent)
    : QObject(parent), m_resolver(resolver), m_p2p(p2p)
{
    Q_ASSERT(m_resolver);
    connect(m_resolver, &StreamResolver::resolutionComplete, this,
            [this](const QString& type, const QString& imdbId,
                   const QVariantMap&) {
                // Late completion for a SUPERSEDED request: drop it.
                if (type == m_pendingType && imdbId == m_pendingId)
                    decide();
            });

    if (m_p2p) {
        // The engine emits QUEUED (never re-entrantly), so token checks
        // against m_activeToken are race-free.
                connect(m_p2p, &nuvio::p2p::P2pEngine::streamReady, this,
                [this](quint64 token, const QString& url) {
                    if (!m_awaitingP2p || token != m_activeToken) return;
                    m_awaitingP2p = false;
                    // P2P-relay urls are transient: accepted, never cached.
                    acceptSession(m_title, m_url, m_type, m_id, m_season,
                                  m_episode, m_isRelay, m_pendingTitle, url,
                                  m_pendingType, m_pendingId, true);
                    emit sessionChanged();
                    emit playbackReady(m_title, m_url);
                });
        connect(m_p2p, &nuvio::p2p::P2pEngine::streamFailed, this,
                [this](quint64 token, const QString&) {
                    if (!m_awaitingP2p || token != m_activeToken) return;
                    m_awaitingP2p = false;
                    emit playbackUnavailable(m_pendingTitle);
                });
        // statsUpdated is deliberately not relayed yet: the player chrome
        // has no torrent-progress surface in this phase (plan P3 UI).
    }
}

void PlaybackSession::requestPlay(const QString& type, const QString& imdbId,
                                  const QString& title)
{
    m_pendingType  = type;
    m_pendingId    = imdbId;
    m_pendingTitle = title.isEmpty() ? imdbId : title;

    // Reuse-link fast path (Compose StreamDestination parity): a fresh
    // cached direct link skips resolution entirely. Torrent-relay urls are
    // never cached (transient localhost), so a hit is always directly
    // playable.
    if (m_reuseProvider) {
        const ReusePolicy policy = m_reuseProvider();
        if (policy.enabled && policy.cacheHours > 0) {
            const CompositeId parts = splitCompositeId(imdbId);
            const QString key = streamLinkContentKey(
                type, imdbId,
                parts.isEpisode() ? parts.parent : QString(),
                parts.season, parts.episode);
            const qint64 maxAgeMs =
                qint64(policy.cacheHours) * 3600LL * 1000LL;
            StreamLinkCache cache;
            if (const auto hit = cache.getValid(
                    key, maxAgeMs, QDateTime::currentMSecsSinceEpoch())) {
                acceptSession(m_title, m_url, m_type, m_id, m_season,
                              m_episode, m_isRelay, m_pendingTitle, hit->url,
                              type, imdbId, false);
                emit sessionChanged();
                emit playbackReady(m_title, m_url);
                return;
            }
        }
    }

    // No-op (and silent) when already answered completely; hits the
    // network otherwise.
    m_resolver->resolve(type, imdbId);

    // Cache-hit case: resolve() returned early with no future signal in
    // flight - finish synchronously or the second click does nothing.
    if (m_resolver->isComplete(type, imdbId))
        decide();
}

void PlaybackSession::decide()
{
    const QVariantMap best = m_resolver->bestFor(m_pendingType, m_pendingId);

    const QString url =
        best.value(QLatin1String("url")).toString();
    const QString sourceTitle =
        best.value(QLatin1String("title")).toString();
    if (!best.isEmpty() && !url.isEmpty()) {
        // Direct resolutions refresh the reuse cache (Compose parity).
        // Addon display names are not carried by the resolver map, so the
        // source id doubles as the name (display-only field). The key is
        // episode-aware exactly like the requestPlay read path.
        CachedLink link;
        link.url = url;
        link.streamName = sourceTitle;
        link.addonName = best.value(QLatin1String("source")).toString();
        link.addonId = link.addonName;
        const CompositeId keyParts = splitCompositeId(m_pendingId);
        StreamLinkCache().save(
            streamLinkContentKey(m_pendingType, m_pendingId,
                                 keyParts.isEpisode() ? keyParts.parent
                                                      : QString(),
                                 keyParts.season, keyParts.episode),
            link, QDateTime::currentMSecsSinceEpoch());
        acceptSession(m_title, m_url, m_type, m_id, m_season, m_episode,
                      m_isRelay,
                      sourceTitle.isEmpty() ? m_pendingTitle : sourceTitle,
                      url, m_pendingType, m_pendingId, false);
        emit sessionChanged();
        emit playbackReady(m_title, m_url);
        return;
    }

    // Tier 2: nothing direct, but a torrent entry exists -> route through
    // the local engine when one is attached. Its completion comes back as
    // this object's own terminal signal (queued on both sides).
    const QVariantMap tor =
        m_resolver->bestTorrent(m_pendingType, m_pendingId);
    const QString torHash = tor.value(QLatin1String("infoHash")).toString();
    if (m_p2p && !torHash.isEmpty()) {
        const QString torTitle = tor.value(QLatin1String("title")).toString();
        m_pendingTitle = torTitle.isEmpty() ? m_pendingTitle : torTitle;
        m_activeToken  = m_p2p->startStream(torHash);
        m_awaitingP2p  = true;
        return;
    }

    // No state mutation: an unavailable result must not clobber a
    // still-valid previous session the player route may show again.
    emit playbackUnavailable(m_pendingTitle);
}

} // namespace nuvio::playback