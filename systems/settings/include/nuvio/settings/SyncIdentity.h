#pragma once

#include <QString>

namespace nuvio::settings {

class PropertiesStore;

/// Port of Compose core/sync/SyncClientIdentity (+ its desktop storage):
/// the per-device id rides sync_client_identity.properties /
/// client_instance_id in the EXACT upstream format so backend device rows
/// are indistinguishable between the two builds.
///
/// SERVER-FACING STRING NOTICE (AGENTS.md rebranding rule): the
/// "nuvio-mobile-" prefix is an UPSTREAM contract value - never rebrand it.
class SyncIdentity final {
public:
    static constexpr auto kStoreName = "sync_client_identity";
    static constexpr auto kKeyName   = "client_instance_id";
    static constexpr auto kPrefix    = "nuvio-mobile-";

    /// Loads the stored id when valid; otherwise generates one (prefix +
    /// 32 random [a-z0-9]) and persists it. Never returns an empty string.
    [[nodiscard]] static QString currentClientId(PropertiesStore& store);

    /// Clears the stored id (next currentClientId() generates fresh).
    static void resetClientId(PropertiesStore& store);

    /// Compose validity contract: length 16..96 over [A-Za-z0-9_-].
    [[nodiscard]] static bool isValidClientId(const QString& id);
};

} // namespace nuvio::settings