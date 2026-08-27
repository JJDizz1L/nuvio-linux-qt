#include "nuvio/mpv/TrackAutoSelector.h"

#include <QStringList>
#include <QDebug>

namespace nuvio::mpv {

using namespace tracksel;

void TrackAutoSelector::setPreferencesProvider(PrefsProvider provider)
{
    m_prefs = std::move(provider);
}

void TrackAutoSelector::setContentOriginalLanguage(QString lang)
{
    m_contentOriginal = std::move(lang);
}

void TrackAutoSelector::handleFileLoaded()
{
    // Selection window (re)opens per file - the Compose FILE_LOADED latch.
    m_haveFileLoaded   = true;
    m_audioApplied     = false;
    m_subtitlesApplied = false;
    runOnce();                     // tracks may already be cached
}

void TrackAutoSelector::handleTracks(QVector<TrackInfo> tracks)
{
    m_tracks = std::move(tracks);
    runOnce();
}

void TrackAutoSelector::runOnce()
{
    if (!m_haveFileLoaded || !m_prefs) return;

    const LanguagePrefs prefs = m_prefs();
    const auto isAudio  = [](const TrackInfo& t) {
        return t.kind == TrackKind::Audio;
    };
    const auto isSubtitle = [](const TrackInfo& t) {
        return t.kind == TrackKind::Subtitle;
    };
    QVector<TrackInfo> audio, subs;
    for (const auto& t : m_tracks) {
        if (isAudio(t)) audio.append(t);
        else if (isSubtitle(t)) subs.append(t);
    }

    const QStringList audioTargets =
        resolvePreferredAudioTargets(prefs, m_contentOriginal);

    // ---- audio tier --------------------------------------------------------
    if (!m_audioApplied) {
        if (audioTargets.isEmpty()) {
            qDebug("PlayerTracks: audio skipped - no targets");
            m_audioApplied = true;                 // latched, no flap
        } else if (!audio.isEmpty()) {
            const int idx = findPreferredAudioTrackIndex(audio, audioTargets);
            if (idx >= 0) {
                emit commandReady({QStringLiteral("set"),
                                   QStringLiteral("aid"),
                                   QString::number(audio[idx].id)});
                qDebug("PlayerTracks: audio applied aid=%lld target-chained",
                       audio[idx].id);
            } else {
                qDebug("PlayerTracks: audio no match - container default kept");
            }
            m_audioApplied = true;
        }
    }

    // ---- subtitle tier -----------------------------------------------------
    if (m_subtitlesApplied) return;

    const QStringList subTargets =
        resolvePreferredSubtitleTargets(prefs);

    // Currently-selected audio language feeds the forced-subtitle branch.
    QString selectedAudioLang;
    for (const auto& t : m_tracks)
        if (t.kind == TrackKind::Audio) {
            bool ok = false;
            selectedAudioLang = resolveAudioTrackLanguageTarget(t, &ok);
            if (ok) break;
            selectedAudioLang.clear();
        }

    const auto plan = resolveSubtitleAutoSelectionPlan(
        selectedAudioLang, audioTargets, subTargets,
        /*useForcedSubtitles=*/ true);
    // NOTE: useForced default true mirrors Compose's subtitleStyle toggle;
    // the setting plumb arrives with the player-chrome phase.

    if (!plan.has_value()) {
        m_subtitlesApplied = true;   // stay open, retry next file only
        return;
    }
    if (plan->targets.isEmpty()) {
        emit commandReady({QStringLiteral("set"), QStringLiteral("sid"),
                           QStringLiteral("no")});
        m_subtitlesApplied = true;
        return;
    }
    if (!subs.isEmpty()) {
        const int idx = findPreferredSubtitleTrackIndex(subs, plan->targets,
                                                        plan->mode);
        if (idx >= 0) {
            emit commandReady({QStringLiteral("set"), QStringLiteral("sid"),
                               QString::number(subs[idx].id)});
            qDebug("PlayerTracks: subtitles applied sid=%lld mode=%s",
                   subs[idx].id,
                   plan->mode == SubtitleSelectionMode::ForcedOnly
                       ? "forced" : "normal");
        } else if (plan->mode == SubtitleSelectionMode::ForcedOnly) {
            emit commandReady({QStringLiteral("set"), QStringLiteral("sid"),
                               QStringLiteral("no")});
        }
        m_subtitlesApplied = true;
    }
}

} // namespace nuvio::mpv
