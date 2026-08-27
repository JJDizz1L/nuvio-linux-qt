import QtQuick
import QtQuick.Controls
import Nuvio.Mpv
import "../components"
import "../theme"

Item {
    id: page
    focus: true            // media keyboard ownership while playback is up

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
        // Printable ASCII forwards 1:1 under its display name ("f" -> f).
        if (ev.text.length === 1) {
            const c = ev.text.toUpperCase().charCodeAt(0)
            if ((c >= 65 && c <= 90) || (c >= 48 && c <= 57))
                return ev.text.toUpperCase()
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
        }
    }
    onVisibleChanged: if (!visible) watching.endSessionAbandoned()

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
    }
}
