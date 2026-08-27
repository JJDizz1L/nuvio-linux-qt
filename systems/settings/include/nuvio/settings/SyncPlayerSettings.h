#pragma once

#include <QJsonObject>

namespace nuvio::settings {

class PropertiesStore;

/// Port of the PLAYER_SETTINGS feature of the Compose profile-settings sync
/// blob (MobileProfileSettingsBlob v3), restricted to the keys this line
/// owns today (AppSettings blob-parity storage). Export/apply semantics
/// mirror PlayerSettingsStorage.exportToSyncPayload / replaceFromSyncPayload:
///
///  - EXPORT: only PRESENT keys land in the payload (envelope-wrapped).
///  - APPLY:  per-key decode->write; ABSENT/invalid remote keys leave local
///            values untouched, so partial blobs from either build merge.
///
/// All writes go to the profile-scoped COMPOSE parity keys (<key>_1) in
/// player_settings.properties. Key list grows with feature parity work.
class PlayerSettingsSync final {
public:
    [[nodiscard]] static QJsonObject exportSyncPayload(
        PropertiesStore& playerStore);
    static void applyRemotePayload(PropertiesStore& playerStore,
                                   const QJsonObject& payload);
};

} // namespace nuvio::settings