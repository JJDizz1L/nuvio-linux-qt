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
        visible: navigation.currentRoute === "video" || smokeActive
    }
    Pages.HomePage {
        anchors.fill: parent
        visible: !smokeActive && navigation.currentRoute === "home"
    }
    Pages.WelcomePage {
        anchors.fill: parent
        visible: navigation.currentRoute === "welcome"
    }
    Pages.SettingsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings"
    }
    Pages.SettingsAppearancePage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-appearance"
    }
    Pages.SettingsPlaybackPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-playback"
    }
    Pages.SettingsSubtitlesPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-subtitles"
    }
    Pages.SettingsStreamsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-streams"
    }
    Pages.SettingsContinueWatchingPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-continuewatching"
    }
    Pages.SettingsIntegrationsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-integrations"
    }
    Pages.SettingsAccountPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-account"
    }
    Pages.AddonsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "addons"
    }
    Pages.LibraryPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "library"
    }
    Pages.SearchPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "search"
    }
    Pages.MetaPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "meta"
    }

    // ---- no-source toast (shell level) ---------------------------------------
    // Surfaces honest negatives on ANY route the user might be on - a card
    // click in Library or an episode/Play click on the detail page both land
    // here. Auto-fades; never blocks navigation.
    Rectangle {
        id: toast
        opacity: 0
        z: 50
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        width: Math.min(parent ? parent.width - 64 : 320,
                        toastText.width + 32)
        height: toastText.height + 18
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 42

        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.textPrimary
            font.pixelSize: 13
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.fadeMs }
        }
        Timer {
            id: toastTimer
            interval: 2600
            onTriggered: toast.opacity = 0
        }
    }

    Connections {
        target: playback
        function onPlaybackUnavailable(title) {
            toastText.text =
                qsTr("No playable source found for %1").arg(title)
            toast.opacity = 1
            toastTimer.restart()
        }
    }

    // Trailer resolution: the extracted googlevideo url lands in the SAME
    // player route as regular playback (single consumer surface). Failures
    // reuse the shell toast.
    Connections {
        target: trailer
        function onTrailerResolved(url, audioUrl) {
            if (smokeActive) return
            navigation.push("video")
            pageItem.launchMedia(url, audioUrl)
        }
        function onTrailerFailed() {
            toastText.text = qsTr("Trailer unavailable right now")
            toast.opacity = 1
            toastTimer.restart()
        }
    }

    // Playback-session wiring (plan §8 P1): a resolved card lands on the
    // player route; the harness keeps exclusive play rights in smoke mode.
    Connections {
        target: playback
        function onPlaybackReady(title, url) {
            if (smokeActive) return
            navigation.push("video")
            // Watch-state recording (systems/watching): session keyed by the
            // resolved content identity; composite series ids "tt:S:E" split
            // into parent + season/episode (Compose resume semantics).
            const parts = playback.currentId.split(":")
            const isEpisode = parts.length === 3
            const parent = isEpisode ? parts[0] : playback.currentId
            watching.beginSession(
                playback.currentType,
                parent,
                playback.currentType,
                playback.currentId, title,
                isEpisode ? parseInt(parts[1], 10) : -1,
                isEpisode ? parseInt(parts[2], 10) : -1,
                "", Date.now())
            // Resume from the persisted position (if any resumable row
            // exists for this exact identity in the shared store).
            const resumeMs = watching.resumePositionMsFor(
                parent,
                isEpisode ? parseInt(parts[1], 10) : -1,
                isEpisode ? parseInt(parts[2], 10) : -1)
            // Chrome title: episodes carry an "S1 E2 · Title" label.
            pageItem.mediaTitle = isEpisode
                ? qsTr("S%1 E%2 · %3")
                      .arg(parseInt(parts[1], 10))
                      .arg(parseInt(parts[2], 10))
                      .arg(title)
                : title
            pageItem.launchMedia(url, "", resumeMs)
        }
        // Unavailable results are surfaced by LibraryPage's toast.
    }

    // Launch-hook used by main.cpp. In smoke mode the HARNESS owns playback
    // (single source of truth); without smoke this is the CLI path and the
    // stack jumps straight to the player route.
    function playFromLaunch(url) {
        if (smokeActive) return
        if (url && url.length > 0) {
            navigation.replaceTop("home")   // deterministic launch origin
            navigation.push("video")
            pageItem.launchMedia(url)
        }
    }

    // Set by the bootstrap so the harness gets exclusive play rights.
    property bool smokeActive: false
    function setSmokeActive(v) { smokeActive = v }
}
