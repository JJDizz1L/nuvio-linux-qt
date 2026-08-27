#pragma once

// Runtime glue for the pure selection kernel (TrackSelection.h). Mirrors the
// Compose refreshTracks contract: on every FILE_LOADED the selection window
// reopens; track-list arrivals while a window is open run the policy once
// per tier with applied-latches; final "no match / empty targets" states are
// latched too so repeated track events never flap aid/sid.
//
// Output: single-mpv-command argument lists via commandsReady, intended to
// be connected to MpvController::enqueueCommand. Explicit aid/sid ONLY -
// no alang/slang is ever set (AGENTS.md rule; masked failures otherwise).

#include <functional>
#include <QObject>
#include <QStringList>

#include "nuvio/mpv/MpvTypes.h"
#include "nuvio/mpv/TrackSelection.h"

namespace nuvio::mpv {

class TrackAutoSelector final : public QObject {
    Q_OBJECT
public:
    using PrefsProvider = std::function<tracksel::LanguagePrefs()>;

    void setPreferencesProvider(PrefsProvider provider);
    /// Extra per-file content hint ("original" preference fallback).
    void setContentOriginalLanguage(QString lang);

public slots:
    void handleFileLoaded();
    void handleTracks(QVector<nuvio::mpv::TrackInfo> tracks);

signals:
    void commandReady(const QStringList& args);

private:
    void runOnce();

    PrefsProvider        m_prefs;
    QString              m_contentOriginal;
    QVector<TrackInfo>   m_tracks;
    bool                 m_audioApplied    = false;
    bool                 m_subtitlesApplied = false;
    bool                 m_haveFileLoaded  = false;
};

} // namespace nuvio::mpv