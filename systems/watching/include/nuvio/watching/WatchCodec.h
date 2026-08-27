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

    /// Decode a StoredWatchedPayload -> watched items (Compose derives keys).
    static std::vector<WatchedItem> decodeWatched(const QString& json);

    /// Encode watched items to the Compose StoredWatchedPayload JSON form.
    static QString encodeWatched(const std::vector<WatchedItem>& items);
};

} // namespace nuvio::watching
