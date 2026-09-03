import QtQuick
import QtQuick.Controls
import Nuvio.Mpv
import "../components"
import "../theme"

Item {
    id: page
    focus: true            // media keyboard ownership while playback is up

    // Chrome title for the CURRENT session (set by the shell at launch;
    // episodes carry an "S1 E2 · Title" label).
    property string mediaTitle: ""

    // Episode-list snapshot for next-episode continuation (set by the shell
    // at playbackReady; series only, [] otherwise). A snapshot, not a live
    // meta binding: hover previews mutate meta.current mid-browse.
    property var episodeList: []

    // Next-episode card state (per session key; countdown mirrors Compose's
    // 3-2-1 after selection when auto-play is on).
    QtObject {
        id: nextEp
        property string sessionKey: ""
        property var info: null          // {id,season,episode,name}
        property bool dismissed: false
        property int countdown: -1       // -1 idle, else seconds left
    }
    // Parental-guide state (fetched once per tt session when enabled).
    QtObject {
        id: pg
        property string sessionKey: ""
        property var warnings: []
        property bool shown: false
    }
    // Skip-intro state (P3c): intervals for the session, one auto-skip
    // per segment (completion key mirrors the C++ helper's shape).
    QtObject {
        id: skipState
        property string sessionKey: ""
        property string pendingKey: ""
        property var intervals: []
        property string lastSkipped: ""
    }
    readonly property var activeSkip: {
        if (!mpv.hasMedia || skipState.intervals.length === 0) return null
        const pos = mpv.positionMs / 1000.0
        for (const s of skipState.intervals) {
            if (pos >= s.startSec && pos < s.endSec) return s
        }
        return null
    }
    function skipKey(s) {
        return s.provider + ":" + s.type + ":" + s.startSec + ":" + s.endSec
    }
    function segLabel(type) {
        const c = page.segCategory(type)
        if (c === "recap") return qsTr("Skip recap")
        if (c === "ending") return qsTr("Skip outro")
        return qsTr("Skip intro")
    }
    // Provider types collapse to the three stored categories exactly like
    // the C++ merge (intro|op|mixed-op -> intro, outro family -> outro).
    function segCategory(type) {
        const t = (type || "").toLowerCase()
        if (t === "intro" || t === "op" || t === "mixed-op") return "intro"
        if (t === "outro" || t === "ed" || t === "mixed-ed"
            || t === "credits" || t === "ending") return "outro"
        if (t === "recap") return "recap"
        return ""
    }
    function evaluateSkip() {
        const s = page.activeSkip
        if (!s) return
        if (appsettings.autoSkipSegmentTypes.indexOf(
                page.segCategory(s.type)) < 0) return
        const k = page.skipKey(s)
        if (skipState.lastSkipped === k) return
        skipState.lastSkipped = k
        mpv.seekToSeconds(s.endSec)
    }

    // `mpvController` resolves from engine context properties — set
    // unconditionally in main.cpp so this binding never sees undefined.
    MpvItem {
        id: mpv
        anchors.fill: parent
        controller: mpvController
        activeFocusOnTab: true
    }

    // ---- directive W2: mpv owns the media keyboard --------------------------
    // Real key events are forwarded VERBATIM by name; mpv resolves them
    // against its defaults + the user's input.conf (the file wins inside
    // mpv). Keys nothing claims no-op inside mpv — we never build a second
    // media key map here, and app-level shortcuts (F11/Esc) live above.
    function mpvKeyName(ev) {
        switch (ev.key) {
        case Qt.Key_Space:  return "Space"
        case Qt.Key_Left:   return "Left"
        case Qt.Key_Right:  return "Right"
        case Qt.Key_Up:     return "Up"
        case Qt.Key_Down:   return "Down"
        case Qt.Key_Return:
        case Qt.Key_Enter:  return "Enter"
        default: break
        }
        // Printable ASCII forwards 1:1 PRESERVING CASE (mpv bindings are
        // case-sensitive: "s"=screenshot, "S"=screenshot-each-frame — the
        // old uppercase mapping silently inverted them).
        if (ev.text.length === 1) {
            const c = ev.text.charCodeAt(0)
            if ((c >= 65 && c <= 90) || (c >= 97 && c <= 122)
                || (c >= 48 && c <= 57))
                return ev.text
        }
        return ""              // everything else: not ours to translate
    }
    Keys.onPressed: (ev) => {
        const name = mpvKeyName(ev)
        if (name.length > 0 && mpv.sendKey(name))
            ev.accepted = true
    }

    /**
     * Entry point from the shell's playFromLaunch hook. `audioUrl` is the
     * optional separate-audio stream (trailer adaptive_separate path); empty
     * means the source is muxed and mpv plays its own audio. `startMs` is the
     * optional resume position (watch-state); -1/0 means play from the start.
     */
    function launchMedia(source, audioUrl, startMs) {
        mpv.play(source, audioUrl || "", startMs || -1)
    }

    // ---- watch-state recorder (systems/watching) ------------------------------
    // 1 Hz position pump: debounced >=10 s advances persist resume rows into
    // the Compose-shared watch_progress store; reaching >= 90 % completes the
    // session (marks watched + drops the resume row). Leaving the route
    // abandons the session, persisting the last >= 1 s position.
    // The same tick evaluates the next-episode card (P3a).
    Timer {
        interval: 1000
        running: mpv.hasMedia && !mpv.paused
        repeat: true
        onTriggered: {
            if (mpv.durationMs > 0 &&
                mpv.positionMs >= mpv.durationMs * 0.9) {
                watching.endSessionCompleted(Date.now())
            } else {
                watching.publishPosition(mpv.positionMs, mpv.durationMs)
            }
            page.evaluateNextEpisode()
            page.evaluateSkip()
        }
    }
    onVisibleChanged: {
        if (!visible) {
            watching.endSessionAbandoned()
            mpv.setSpeed(1.0)   // hold-to-speed never leaks across routes
        } else {
            page.maybeFetchParentalGuide()
            page.maybeResolveSkip()
        }
    }

    // ---- next-episode continuation (P3a) ------------------------------------
    // Composite series ids split exactly like the shell's beginSession.
    // No released dates ride our episode rows, so the aired gate treats
    // unknown as aired (Compose default). Intervals arrive with the skip
    // leg (P3c); until then the plain settings threshold decides.
    function currentParts() { return playback.currentId.split(":") }
    function evaluateNextEpisode() {
        if (!page.visible || !mpv.hasMedia) return
        const parts = page.currentParts()
        if (parts.length !== 3 || page.episodeList.length === 0) return
        const key = playback.currentId
        if (nextEp.sessionKey !== key) {
            nextEp.sessionKey = key
            nextEp.dismissed = false
            nextEp.countdown = -1
            page.cardVisible = false
            nextEp.info = nextep.nextEpisode(page.episodeList, parts[0],
                                             parseInt(parts[1], 10),
                                             parseInt(parts[2], 10))
            if (!nextEp.info) return
        }
        if (!nextEp.info || nextEp.dismissed || nextEp.countdown >= 0) return
        const show = nextep.shouldShowCard(
            mpv.positionMs, mpv.durationMs, skipState.intervals,
            appsettings.nextEpisodeThresholdMode,
            appsettings.nextEpisodeThresholdPercent,
            appsettings.nextEpisodeThresholdMinutesBeforeEnd)
        if (!show) return
        if (appsettings.streamAutoPlayNextEpisodeEnabled)
            nextEp.countdown = 3
        else
            page.cardVisible = true
    }
    property bool cardVisible: false
    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        running: nextEp.countdown >= 0 && page.visible
        onTriggered: {
            if (nextEp.countdown > 0) {
                nextEp.countdown -= 1
            } else {
                nextEp.countdown = -1
                page.playNextEpisode()
            }
        }
    }
    function playNextEpisode() {
        if (!nextEp.info) return
        nextEp.dismissed = true
        page.cardVisible = false
        playback.requestPlay(playback.currentType, nextEp.info.id,
                             nextEp.info.name || "")
    }
    function dismissNextEpisode() {
        nextEp.dismissed = true
        nextEp.countdown = -1
        page.cardVisible = false
    }

    // ---- parental guide (P3a) ------------------------------------------------
    // Fetched once per tt session while the page is up; shown once as a
    // dismissible card. Failures and non-tt ids stay silent.
    Connections {
        target: playback
        function onSessionChanged() {
            if (!page.visible) return
            page.maybeFetchParentalGuide()
            page.maybeResolveSkip()
        }
    }
    // Skip intervals resolve once per session (P3c); cached keys answer
    // fast. Gated on the master switch like Compose's repository.
    function maybeResolveSkip() {
        if (!appsettings.skipIntroEnabled || !playback.hasSession) return
        const key = playback.currentId
        if (skipState.sessionKey === key) return
        skipState.sessionKey = key
        skipState.pendingKey = key
        skipState.intervals = []
        skipState.lastSkipped = ""
        skip.resolve(playback.currentId, playback.currentSeason,
                     playback.currentEpisode)
    }
    Connections {
        target: skip
        function onIntervals(segments) {
            // Stale delivery guard: a session that never issues its own
            // lookup (switch off mid-flight) must not inherit another's.
            if (skipState.pendingKey !== playback.currentId) return
            skipState.intervals = segments
        }
        function onSubmitted(ok) {
            submitResult.text = ok ? qsTr("Submitted, thanks!")
                                    : qsTr("Submit failed")
        }
    }
    function maybeFetchParentalGuide() {
        if (!appsettings.showParentalGuide || !playback.hasSession) return
        const key = playback.currentId
        if (pg.sessionKey === key) return
        pg.sessionKey = key
        pg.warnings = []
        pg.shown = false
        parental.fetch(key)
    }
    Connections {
        target: parental
        function onResolved(warnings) {
            if (warnings.length === 0 || pg.shown) return
            pg.warnings = warnings
            pg.shown = true
        }
    }

    // ---- chrome auto-hide --------------------------------------------------
    // Display-layer convenience ONLY (plan directive: nothing here may
    // influence media timing). A coarse clock bumps `now` purely so the two
    // bindings re-evaluate; interaction updates `lastActivity`, mirroring the
    // Compose runtime's 3.5 s idle-hide UX.
    MouseArea {
        id: overlay
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton          // clicks handled by MpvItem
        cursorShape: page.idleChrome ? Qt.BlankCursor : Qt.ArrowCursor

        property real lastActivity: -3500     // chrome visible initially
        property real now: 0

        onPressed: lastActivity = now
        onPositionChanged: { lastActivity = now }
        onContainsMouseChanged: if (!containsMouse && page.idleChrome) lastActivity = now - 3500

        Timer {                               // coarse 250 ms display tick
            interval: 250
            repeat: true
            running: true
            onTriggered: overlay.now += interval
        }
    }

    readonly property bool idleChrome:
        mpv.hasMedia && (overlay.now - overlay.lastActivity) > 3500

    // ---- torrent download telemetry -----------------------------------------
    // Display-layer only: mirrors statsUpdated from the p2p engine with a
    // 3 s staleness fade. Nothing here may influence media timing.
    QtObject {
        id: tor
        property double downBps: 0
        property int    peers: 0
        property int    seeds: 0
        property double preloaded: 0
        property double total: 0
        property double lastSeen: -1e9
        property double now: 0                  // seconds, monotonic-ish
    }
    Timer {
        interval: 500
        running: true
        repeat: true
        onTriggered: tor.now += interval / 1000.0
    }
    Connections {
        target: p2p
        function onStatsUpdated(token, preloadedBytes, torrentSize,
                                downloadSpeedBps, peers, seeds) {
            tor.downBps   = downloadSpeedBps
            tor.peers     = peers
            tor.seeds     = seeds
            tor.preloaded = preloadedBytes
            tor.total     = torrentSize
            tor.lastSeen  = tor.now
        }
    }
    function fmtSpeed(bps) {
        if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MB/s"
        if (bps >= 1024)    return (bps / 1024).toFixed(0) + " KB/s"
        return bps.toFixed(0) + " B/s"
    }

    Rectangle {
        visible: mpv.hasMedia && (tor.now - tor.lastSeen) < 3 && tor.total > 0
        width: torText.width + 24
        height: torText.height + 12
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bar.top
        anchors.bottomMargin: 10

        Text {
            id: torText
            anchors.centerIn: parent
            color: Theme.textSecondary
            font.pixelSize: 11
            text: qsTr("torrent %1 · buffer %2% · %3 seeders / %4 peers")
                .arg(fmtSpeed(tor.downBps))
                .arg(tor.total > 0 ? Math.round(100 * tor.preloaded / tor.total) : 0)
                .arg(tor.seeds)
                .arg(tor.peers)
        }
    }

    // Session title above the bar (chrome-gated like the transport).
    Text {
        anchors.bottom: bar.top
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 64, bar.width)
        horizontalAlignment: Text.AlignHCenter
        visible: page.idleChrome ? false : (page.mediaTitle.length > 0)
        text: page.mediaTitle
        color: Theme.textPrimary
        font.pixelSize: 15
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    // Loading overlay (P3a): demuxer-stall indicator, gated by preference.
    Rectangle {
        visible: appsettings.showLoadingOverlay && mpv.buffering
                 && !page.idleChrome
        anchors.centerIn: parent
        width: loadingText.width + 32
        height: loadingText.height + 20
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        Text {
            id: loadingText
            anchors.centerIn: parent
            text: qsTr("Loading…")
            color: Theme.textPrimary
            font.pixelSize: 15
        }
    }

    // Parental-guide card (P3a): once per session, dismissible, silent
    // when the lookup fails or returns no warnings.
    Rectangle {
        visible: pg.warnings.length > 0 && !page.idleChrome
        anchors.top: parent.top
        anchors.topMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 64, 480)
        height: pgCol.height + 24
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        Column {
            id: pgCol
            anchors.top: parent.top
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 4
            Text {
                text: qsTr("Parental guidance")
                color: Theme.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Repeater {
                model: pg.warnings
                delegate: Text {
                    required property var modelData
                    text: modelData.label + " · " + modelData.severity
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }
            Button {
                text: qsTr("Dismiss")
                flat: true
                onClicked: pg.warnings = []
            }
        }
    }

    // Next-episode card (P3a): threshold/outro card with a 3-2-1 auto-play
    // countdown when enabled, manual Play otherwise. Dismiss per session.
    Rectangle {
        visible: (page.cardVisible || nextEp.countdown >= 0)
                 && !page.idleChrome && nextEp.info
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.bottom: bar.top
        anchors.bottomMargin: 16
        width: 300
        height: nextCol.height + 24
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        Column {
            id: nextCol
            anchors.top: parent.top
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 6
            Text {
                text: qsTr("Up next")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Text {
                text: nextEp.info
                      ? qsTr("S%1 E%2 · %3").arg(nextEp.info.season)
                            .arg(nextEp.info.episode).arg(nextEp.info.name)
                      : ""
                color: Theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                width: parent.width
            }
            Text {
                visible: nextEp.countdown >= 0
                text: qsTr("Playing in %1…").arg(
                    Math.max(nextEp.countdown, 0) + 1)
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            Row {
                spacing: Theme.spacingSm
                Button {
                    text: nextEp.countdown >= 0 ? qsTr("Play now")
                                                : qsTr("Play")
                    onClicked: {
                        nextEp.countdown = -1
                        page.playNextEpisode()
                    }
                }
                Button {
                    text: qsTr("Dismiss")
                    flat: true
                    onClicked: page.dismissNextEpisode()
                }
            }
        }
    }

    // Skip-intro button (P3c): visible inside a known segment; manual
    // tap seeks past it, auto-skip types jump in the pump instead. Rides
    // above the next-episode card when both show.
    Button {
        visible: page.activeSkip !== null && !page.idleChrome
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.bottom: bar.top
        anchors.bottomMargin: (page.cardVisible || nextEp.countdown >= 0)
                              ? 220 : 16
        text: page.activeSkip ? page.segLabel(page.activeSkip.type)
                              : qsTr("Skip")
        onClicked: {
            if (!page.activeSkip) return
            skipState.lastSkipped = page.skipKey(page.activeSkip)
            mpv.seekToSeconds(page.activeSkip.endSec)
        }
    }

    // IntroDb submit dialog (P3c): user-marked segment for the current tt
    // episode; needs the submit switch + an API key (key never syncs).
    property bool submitOpen: false
    Button {
        visible: appsettings.introSubmitEnabled
                 && appsettings.introDbApiKey !== ""
                 && playback.currentSeason >= 0 && !page.idleChrome
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.bottom: bar.top
        anchors.bottomMargin: 16
        text: qsTr("Submit intro")
        onClicked: {
            submitStart.text = Math.max(0, mpv.positionMs / 1000 - 30)
            submitEnd.text = mpv.positionMs / 1000 + 60
            submitResult.text = ""
            page.submitOpen = true
        }
    }
    Rectangle {
        visible: page.submitOpen
        anchors.centerIn: parent
        width: 340
        height: submitCol.height + 32
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        border.color: Theme.border
        border.width: 1
        Column {
            id: submitCol
            anchors.top: parent.top
            anchors.topMargin: 16
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 8
            Text {
                text: qsTr("Submit intro segment (seconds)")
                color: Theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Row {
                spacing: Theme.spacingSm
                Text {
                    text: qsTr("Start")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: submitStart
                    width: 100
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                }
                Text {
                    text: qsTr("End")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: submitEnd
                    width: 100
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                }
            }
            Text {
                id: submitResult
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            Row {
                spacing: Theme.spacingSm
                Button {
                    text: qsTr("Submit")
                    onClicked: skip.submit(Number(submitStart.text),
                                           Number(submitEnd.text))
                }
                Button {
                    text: qsTr("Close")
                    flat: true
                    onClicked: page.submitOpen = false
                }
            }
        }
    }

    TransportBar {
        id: bar
        mpv: mpv
        width: Math.min(parent.width - 32, 760)
        height: Theme.barHeight
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        opacity: page.idleChrome ? 0 : 1
        enabled: opacity > 0.5
        Behavior on opacity {
            NumberAnimation { duration: Theme.fadeMs; easing.type: Easing.OutQuad }
        }
        onSourcesPressed: sourcesPanel.visible = !sourcesPanel.visible
    }

    // Streams scope panel (P3d): reads the resolver cache for the current
    // key; scope edits apply to future resolutions only.
    StreamsPanel {
        id: sourcesPanel
        visible: false
        mediaType: playback.currentType
        mediaId: playback.currentId
        width: Math.min(parent.width - 48, 420)
        height: 420
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.bottom: bar.top
        anchors.bottomMargin: 16
        onClosed: visible = false
    }
}
