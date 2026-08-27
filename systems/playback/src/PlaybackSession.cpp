#include "nuvio/playback/PlaybackSession.h"

#include "nuvio/playback/StreamResolver.h"

namespace nuvio::playback {

PlaybackSession::PlaybackSession(StreamResolver* resolver, QObject* parent)
    : QObject(parent), m_resolver(resolver)
{
    Q_ASSERT(m_resolver);
    connect(m_resolver, &StreamResolver::resolutionComplete, this,
            [this](const QString& type, const QString& imdbId,
                   const QVariantMap&) {
                // Late completion for a SUPERSEDED request: drop it.
                if (type == m_pendingType && imdbId == m_pendingId)
                    decide();
            });
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
        emit sessionChanged();
        emit playbackReady(m_title, m_url);
    } else {
        // No state mutation: an unavailable result must not clobber a
        // still-valid previous session the player route may show again.
        emit playbackUnavailable(m_pendingTitle);
    }
}

} // namespace nuvio::playback