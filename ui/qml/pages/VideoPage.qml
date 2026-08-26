import QtQuick
import QtQuick.Controls
import Nuvio.Mpv
import "../components"
import "../theme"

Item {
    id: page

    // `mpvController` resolves from engine context properties — set
    // unconditionally in main.cpp so this binding never sees undefined.
    MpvItem {
        id: mpv
        anchors.fill: parent
        controller: mpvController
        activeFocusOnTab: true
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
