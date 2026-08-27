#include "nuvio/playback/PlaybackSession.h"

#include "nuvio/p2p/P2pEngine.h"
#include "nuvio/playback/StreamResolver.h"

namespace nuvio::playback {

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
                    m_title = m_pendingTitle;
                    m_url   = url;
                    m_type  = m_pendingType;
                    m_id    = m_pendingId;
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
        m_title = sourceTitle.isEmpty() ? m_pendingTitle : sourceTitle;
        m_url   = url;
        m_type  = m_pendingType;
        m_id    = m_pendingId;
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