#pragma once

// Provider registry + scrobble coordinator (T1): mirrors Compose's
// TrackingProviderRegistry (auth/list/history/scrobble/library/watched/
// progress slots, connected-set observation) and TrackingScrobbleCoordinator
// (active-profile guard, supervisor fan-out collecting per-provider
// failures). Scrobblers register as callables (no inheritance ceremony);
// auth state is pushed in by the provider repos (setProviderAuthenticated).

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

#include "nuvio/tracking/TrackingTypes.h"

namespace nuvio::tracking {

struct ScrobbleFailure {
    TrackingProvider provider;
    QString message;
};

class TrackingRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList connectedProviders READ connectedProviderIds
                   NOTIFY connectedChanged)

public:
    using ScrobbleFn = std::function<bool(
        int profileId, ScrobbleAction action, const ScrobbleEvent& event)>;

    using QObject::QObject;

    /// Registers a scrobbler (idempotent per provider).
    Q_INVOKABLE void registerScrobbler(TrackingProvider provider,
                                       SeekScrobblePolicy seekPolicy,
                                       ScrobbleFn fn);
    /// Pushed by provider auth repos (sign-in/out, profile switches).
    Q_INVOKABLE void setProviderAuthenticated(TrackingProvider provider,
                                              bool authenticated);
    [[nodiscard]] bool isProviderConnected(TrackingProvider provider) const;

    [[nodiscard]] QStringList connectedProviderIds() const;
    /// Fan-out over CONNECTED scrobblers (failures collected, never thrown).
    QList<ScrobbleFailure> dispatch(int profileId, ScrobbleAction action,
                                    const ScrobbleEvent& event,
                                    bool seekRestartOnly) const;

signals:
    void connectedChanged();

private:
    struct Entry {
        SeekScrobblePolicy policy = SeekScrobblePolicy::None;
        ScrobbleFn fn;
    };
    QHash<int, Entry> m_scrobblers;   // key = int(provider)
    QSet<int> m_connected;
};

/// Coordinator entry points (Compose TrackingScrobbleCoordinator parity):
/// active-profile guard + registry fan-out. Returns per-provider failures.
class ScrobbleCoordinator final {
public:
    /// activeProfileId is read at call time (no stale profile scrobbles).
    [[nodiscard]] static QList<ScrobbleFailure> scrobble(
        TrackingRegistry& registry, int activeProfileId, int profileId,
        ScrobbleAction action, const ScrobbleEvent& event);
    /// Seek path: only STOP_AND_RESTART scrobblers run.
    [[nodiscard]] static QList<ScrobbleFailure> scrobbleSeek(
        TrackingRegistry& registry, int activeProfileId, int profileId,
        ScrobbleAction action, const ScrobbleEvent& event);
};

} // namespace nuvio::tracking

Q_DECLARE_METATYPE(nuvio::tracking::TrackingProvider)
