#pragma once

// Compose-parity JSON codec for the on-disk watch_progress / watched payloads.
//
// kotlin serialization rules we must match:
//   * camelCase keys (kotlinx.serialization default naming)
//   * encodeDefaults=true  -> required-with-default fields are always emitted
//   * ignoreUnknownKeys=true -> decode is tolerant of unknown/extra fields
//   * nullable fields (val x: String? = null) are OMITTED when null (not
//     written as "null") — kotlinx only omits nulls when `encodeDefaults` is
//     true AND the field is truly optional-with-null-default. We replicate that
//     by only emitting optionals that have a value, and always emitting the
//     required defaults.
//   * booleans are lower-case `true`/`false`, ints as numbers, longs as
//     numbers (epoch millis), floats as numbers.

#include <QString>
#include <QJsonObject>
#include <vector>

#include "nuvio/watching/WatchProgress.h"

namespace nuvio::watching {

class WatchCodec {
public:
    /// Decode the full StoredWatchProgressPayload shape. Tolerant of missing
    /// fields / unknown keys (mirrors ignoreUnknownKeys). On parse failure
    /// returns an empty payload (mirrors Compose's runCatching.getOr).
    static StoredProgressPayload decodeProgress(const QString& json);

    /// Encode entries to the Compose StoredWatchProgressPayload JSON form.
    /// lastSuccessfulPush/delta fields are written at their zero/false
    /// defaults (matches WatchProgressCodec.encodeEntries).
    static QString encodeProgress(const std::vector<WatchEntry>& entries);

    /// PREFERRED: full-payload encode — PRESERVES the sync bookkeeping
    /// envelope (lastSuccessfulPushEpochMs / deltaCursorEventId /
    /// deltaInitialized / dirtyProgressKeys). The repository owns these; a
    /// plain entries encode would wipe Compose's sync state on every Qt
    /// write (latently broke cross-line delta sync before this fix).
    static QString encodeProgressPayload(const StoredProgressPayload& p);

    /// Decode a StoredWatchedPayload -> watched items (Compose derives keys).
    static std::vector<WatchedItem> decodeWatched(const QString& json);

    /// Full-payload watched encode/decode — PRESERVES the sync envelope
    /// (cursor / deltaInitialized / dirtyWatchedKeys / providerPayloads).
    static StoredWatchedPayload decodeWatchedPayload(const QString& json);
    static QString encodeWatchedPayload(const StoredWatchedPayload& p);
    static WatchedItem decodeWatchedItem(const QJsonObject& o);

    /// Encode watched items to the Compose StoredWatchedPayload JSON form.
    static QString encodeWatched(const std::vector<WatchedItem>& items);
};

} // namespace nuvio::watching
