#pragma once

// Track preference selection kernel - faithful parity port of the Compose
// line's PlayerTrackSelection.kt + PlayerLanguagePreferences.kt.
//
// Everything here is PURE: no sockets, no process, no GUI. Fixtures feed the
// offline suite exactly like every other system in this tree.
//
// Porting notes / deliberate deviations (keep visible during P4 sync-parity):
//  * LanguageCodeAliases + LanguageNameAliases are copied VERBATIM from
//    PlayerLanguagePreferences.kt (2026-08-27 snapshot). Update both sides
//    together if upstream edits them.
//  * contentOriginalLanguage is plumbed as an optional argument; the Qt
//    detail route does not populate it yet -> ORIGINAL preferences fall
//    back to device languages (same as Compose's own fallback).
//  * Subtitle track "forced-ness" combines mpv's forced flag with the
//    label-heuristic (inferForcedSubtitleTrack: 'forced', and
//    songs+signs = SDH/sign-song tracks treated specially by Compose).

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

#include "nuvio/mpv/MpvTypes.h"

namespace nuvio::mpv {
namespace tracksel {

// Reserved preference option values (Compose constants verbatim).
constexpr auto kDefault  = "default";
constexpr auto kDevice   = "device";
constexpr auto kOriginal = "original";
constexpr auto kNone     = "none";
constexpr auto kForced   = "forced";

/// Kotlin String?-style normalizer: EMPTY string == null. Applies the
/// special-case PT/ES handlers plus both alias tables; returns "" when the
/// input carries no usable language information.
[[nodiscard]] QString normalizeLanguageCode(const QString& raw);

[[nodiscard]] bool languageMatchesPreference(const QString& trackLanguage,
                                             const QString& targetLanguage);

/// True when the normalized value is one of the settings-list codes
/// (AvailableLanguageOptions membership used by the audio label fallback).
[[nodiscard]] bool isKnownLanguageCode(const QString& normalizedCode);

/// resolveAudioTrackLanguageTarget: direct normalized language unless
/// und/unknown; otherwise label/id IF they normalize into known options.
[[nodiscard]] QString resolveAudioTrackLanguageTarget(const TrackInfo& track,
                                                      bool* ok = nullptr);

// ---- target-chain builders -------------------------------------------------

struct LanguagePrefs {
    QString preferredAudio;      // "device" | "original" | code | ...
    QString secondaryAudio;
    QString preferredSubtitle;   // "none" | "forced" | code | ...
    QString secondarySubtitle;
    QStringList deviceLanguages; // BCP47-ish codes, most preferred first
};

/// Chains in Compose target order (primary/secondary/device/original rules).
[[nodiscard]] QStringList resolvePreferredAudioTargets(
    const LanguagePrefs& prefs, const QString& contentOriginalLang = {});
[[nodiscard]] QStringList resolvePreferredSubtitleTargets(
    const LanguagePrefs& prefs);

// ---- subtitle planning -------------------------------------------------------

enum class SubtitleSelectionMode : quint8 { ForcedOnly, NormalOnly };

struct SubtitlePlan {
    QStringList          targets;
    SubtitleSelectionMode mode = SubtitleSelectionMode::NormalOnly;
};

/// nullopt == "auto-selection impossible right now" (do nothing, stay open).
[[nodiscard]] std::optional<SubtitlePlan> resolveSubtitleAutoSelectionPlan(
    const QString& selectedAudioLang, const QStringList& audioTargets,
    const QStringList& subtitleTargets, bool useForcedSubtitles);

[[nodiscard]] bool inferForcedSubtitleTrack(const QString& label,
                                            const QString& language,
                                            const QString& trackId,
                                            bool mpvForcedFlag);

// ---- finders -------------------------------------------------------------------

[[nodiscard]] int findPreferredAudioTrackIndex(
    const QVector<TrackInfo>& tracks, const QStringList& targets);

[[nodiscard]] int findPreferredSubtitleTrackIndex(
    const QVector<TrackInfo>& tracks, const QStringList& targets,
    SubtitleSelectionMode mode);

} // namespace tracksel
} // namespace nuvio::mpv