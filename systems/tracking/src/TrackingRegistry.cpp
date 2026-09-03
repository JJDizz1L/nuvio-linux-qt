#include "nuvio/tracking/TrackingRegistry.h"

namespace nuvio::tracking {

void TrackingRegistry::registerScrobbler(TrackingProvider provider,
                                         SeekScrobblePolicy seekPolicy,
                                         ScrobbleFn fn)
{
    Entry e;
    e.policy = seekPolicy;
    e.fn = std::move(fn);
    m_scrobblers.insert(int(provider), e);
}

void TrackingRegistry::setProviderAuthenticated(TrackingProvider provider,
                                                bool authenticated)
{
    const int key = int(provider);
    const bool had = m_connected.contains(key);
    if (authenticated) m_connected.insert(key);
    else m_connected.remove(key);
    if (m_connected.contains(key) != had) emit connectedChanged();
}

bool TrackingRegistry::isProviderConnected(TrackingProvider provider) const
{
    return m_connected.contains(int(provider));
}

QStringList TrackingRegistry::connectedProviderIds() const
{
    QStringList out;
    for (int key : m_connected) {
        out.append(
            providerStorageId(static_cast<TrackingProvider>(key)));
    }
    out.sort();
    return out;
}

QList<ScrobbleFailure> TrackingRegistry::dispatch(
    int profileId, ScrobbleAction action, const ScrobbleEvent& event,
    bool seekRestartOnly) const
{
    QList<ScrobbleFailure> failures;
    for (auto it = m_scrobblers.constBegin();
         it != m_scrobblers.constEnd(); ++it) {
        if (!m_connected.contains(it.key())) continue;
        if (seekRestartOnly &&
            it.value().policy != SeekScrobblePolicy::StopAndRestart)
            continue;
        const bool ok = it.value().fn
                            ? it.value().fn(profileId, action, event)
                            : false;
        if (!ok) {
            failures.append(
                {static_cast<TrackingProvider>(it.key()),
                 QStringLiteral("scrobble failed")});
        }
    }
    return failures;
}

QList<ScrobbleFailure> ScrobbleCoordinator::scrobble(
    TrackingRegistry& registry, int activeProfileId, int profileId,
    ScrobbleAction action, const ScrobbleEvent& event)
{
    if (profileId != activeProfileId) return {};
    return registry.dispatch(profileId, action, event, false);
}

QList<ScrobbleFailure> ScrobbleCoordinator::scrobbleSeek(
    TrackingRegistry& registry, int activeProfileId, int profileId,
    ScrobbleAction action, const ScrobbleEvent& event)
{
    if (profileId != activeProfileId) return {};
    return registry.dispatch(profileId, action, event, true);
}

} // namespace nuvio::tracking
