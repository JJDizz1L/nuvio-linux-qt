import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import "./pages" as Pages
import "./theme"

ApplicationWindow {
    id: root

    visible: true
    width: 1280
    height: 720
    minimumWidth: 800
    minimumHeight: 480
    title: qsTr("Nuvio Linux")
    color: Theme.background

    function toggleFullscreen() {
        root.visibility = (root.visibility === Window.FullScreen)
                ? Window.Windowed : Window.FullScreen
    }

    Shortcut {
        sequence: "F11"
        context: Qt.ApplicationShortcut
        onActivated: root.toggleFullscreen()
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: root.visibility === Window.FullScreen
        onActivated: root.visibility = Window.Windowed
    }

    Pages.VideoPage {
        id: pageItem
        anchors.fill: parent
    }

    // Launch-hook used by main.cpp. In smoke mode the HARNESS owns playback
    // (single source of truth); without smoke this is the CLI path.
    function playFromLaunch(url) {
        if (!smokeActive && url && url.length > 0)
            pageItem.launchMedia(url)
    }

    // Set by the bootstrap so the harness gets exclusive play rights.
    property bool smokeActive: false
    function setSmokeActive(v) { smokeActive = v }
}
