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
    Pages.ProfilesPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "profiles"
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
    Pages.SettingsHomescreenPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-homescreen"
    }
    Pages.SettingsTrackingPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-tracking"
    }
    Pages.SettingsDebridPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-debrid"
    }
    Pages.SettingsNotificationsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-notifications"
    }
    Pages.SettingsTmdbPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-tmdb"
    }
    Pages.SettingsMdbListPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-mdblist"
    }
    Pages.SettingsPluginsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "settings-plugins"
    }
    Pages.CommunityPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "community"
    }
    Pages.CloudPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "cloud"
    }
    Pages.DownloadsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "downloads"
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
    Pages.CollectionsPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "collections"
    }
    Pages.CollectionDetailPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "collectiondetail"
    }
    Pages.CollectionFolderPage {
        anchors.fill: parent
        visible: navigation.currentRoute === "collectionfolder"
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

    // In-app updater (Appendix A): transient notices reuse the shell
    // toast; the banner below is a top overlay (Compose pushes content
    // down, but every route here is a fill-anchored overlay, so pushing
    // would mean restructuring all 25 pages for a transient strip).
    Connections {
        target: updater
        function onNotice(message) {
            toastText.text = message
            toast.opacity = 1
            toastTimer.restart()
        }
    }
    Rectangle {
        id: updateBanner
        z: 60
        visible: updater.bannerVisible
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64
        color: Theme.surface
        function formatSize(bytes) {
            if (bytes <= 0) return ""
            var units = ["B", "KB", "MB", "GB"]
            var value = bytes
            var unit = 0
            while (value >= 1024 && unit < units.length - 1) {
                value /= 1024
                unit += 1
            }
            var rounded = (value >= 10 || unit === 0) ? Math.floor(value)
                        : Math.floor(value * 10) / 10
            return rounded + " " + units[unit]
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: updater.downloading && updater.downloadProgress >= 0
                   ? parent.width * updater.downloadProgress : 0
            color: Theme.accent
            opacity: 0.35
            visible: updater.downloading
        }
        Column {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.right: updateBannerActions.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                elide: Text.ElideRight
                color: Theme.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                text: {
                    var label = updater.updateTag
                    var size = updateBanner.formatSize(updater.assetSize)
                    return size !== "" ? label + " • " + size : label
                }
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: updater.errorMessage !== "" ? "#ff9a9a"
                                                  : Theme.textSecondary
                text: updater.errorMessage !== ""
                      ? updater.errorMessage
                      : updater.downloading
                      ? updater.downloadProgress >= 0
                        ? qsTr("Downloading update… %1%").arg(
                              Math.round(updater.downloadProgress * 100))
                        : qsTr("Preparing download…")
                      : updater.downloadedPath !== ""
                      ? qsTr("Update downloaded — ready to install")
                      : qsTr("A new version is available")
            }
        }
        Row {
            id: updateBannerActions
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm
            Button {
                text: qsTr("Notes")
                flat: true
                enabled: updater.updateNotes !== ""
                onClicked: updateNotesOverlay.visible = true
            }
            Button {
                visible: !updater.downloading
                text: updater.downloadedPath !== "" ? qsTr("Install")
                      : updater.errorMessage !== "" ? qsTr("Retry")
                                                   : qsTr("Download")
                onClicked: updater.downloadedPath !== ""
                           ? updater.installDownloadedUpdate()
                           : updater.downloadUpdate()
            }
            Button {
                visible: !updater.downloading
                text: qsTr("Later")
                flat: true
                onClicked: updater.dismissBanner()
            }
        }
    }
    Rectangle {
        id: updateNotesOverlay
        z: 70
        visible: false
        anchors.fill: parent
        color: "#80000000"
        MouseArea {
            anchors.fill: parent
            onClicked: updateNotesOverlay.visible = false
        }
        Rectangle {
            width: Math.min(parent.width - 80, 560)
            height: Math.min(parent.height - 120, 420)
            anchors.centerIn: parent
            radius: Theme.radiusMd
            color: Theme.surface
            Text {
                id: updateNotesTitle
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingMd
                color: Theme.textPrimary
                font.pixelSize: 16
                font.weight: Font.DemiBold
                text: updater.updateTitle
            }
            Flickable {
                anchors.top: updateNotesTitle.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: updateNotesClose.top
                anchors.margins: Theme.spacingMd
                contentHeight: updateNotesText.height
                clip: true
                Text {
                    id: updateNotesText
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    text: updater.updateNotes
                }
            }
            Button {
                id: updateNotesClose
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: Theme.spacingMd
                text: qsTr("Close")
                flat: true
                onClicked: updateNotesOverlay.visible = false
            }
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

    // Cloud playback (D3): resolved provider urls play like trailers
    // (no watch session - cloud ids are provider-scoped).
    Connections {
        target: cloud
        function onPlaybackResolved(url) {
            if (smokeActive) return
            navigation.push("video")
            pageItem.launchMedia(url, "")
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
            // Episode-list snapshot for next-episode continuation (P3a):
            // series only; a snapshot because hover previews mutate meta.
            pageItem.episodeList = (isEpisode && typeof meta !== "undefined"
                                    && meta.current
                                    && meta.current.videos)
                ? meta.current.videos : []
            // Scrobble pump (T1): re-arm per-item latches for the session.
            scrobble.beginItem(playback.currentType, playback.currentId,
                               title)
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
