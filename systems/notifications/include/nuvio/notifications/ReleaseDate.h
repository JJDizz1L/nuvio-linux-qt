#pragma once

// Episode-release date kernel (verbatim port of the fork's
// core/time/EpisodeReleaseDateParser rules): plain ISO calendar dates
// pass through untouched (no timezone attached); zoned timestamps map
// to the viewer's local calendar date; zone-less timestamps contribute
// their date part; anything else falls back to an embedded ISO date.

#include <QString>

namespace nuvio::notifications {

// Strict ^\d{4}-\d{2}-\d{2}$ + real calendar validity, echoed back.
[[nodiscard]] QString parseIsoCalendarDate(const QString& value);
// Full rule chain; empty when nothing parses.
[[nodiscard]] QString parseEpisodeReleaseLocalDate(const QString& raw);
// Viewer's local today / local date of an instant (CurrentDateProvider +
// EpisodeReleaseNotificationsClock desktop parity).
[[nodiscard]] QString todayIsoDate();
[[nodiscard]] QString isoDateFromEpochMs(qint64 epochMs);

} // namespace nuvio::notifications
