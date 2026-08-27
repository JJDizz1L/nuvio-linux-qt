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

    /** Entry point from the shell's playFromLaunch hook. */
    function launchMedia(source) {
        mpv.play(source)
    }

    // ---- idle brand mark (until first media arrives) -----------------------
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingMd
        visible: !mpv.hasMedia

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Nuvio")
            color: Theme.textPrimary
            font.pixelSize: 44
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Linux — exploration build")
            color: Theme.textSecondary
            font.pixelSize: 15
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("usage: nuvio-linux-qt <file|url>   ·   F11 fullscreen")
            color: Theme.textDisabled
            font.pixelSize: 12
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
