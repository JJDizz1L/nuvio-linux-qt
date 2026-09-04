import QtQuick
import QtQuick.Controls
import "../theme"

// Settings root (P2): a plain route list; every leaf is its own route
// ("settings-<leaf>") so Back returns here via pop(). All leaves bind only
// to real state (AppSettings / watching / auth) - pages without a backend
// do not exist yet by design (tracking/TMDB/MDBList/notifications land
// with their features in P3+).
Item {
    id: settingsRoot

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Settings")
            color: Theme.textPrimary
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            text: qsTr("Back")
            onClicked: navigation.pop()
        }
    }

    Column {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingSm

        Repeater {
            model: [
                { label: qsTr("Appearance"),       route: "settings-appearance",
                  hint: qsTr("Theme, hover previews, overlay") },
                { label: qsTr("Playback"),         route: "settings-playback",
                  hint: qsTr("Decoder, cache, speed, external player") },
                { label: qsTr("Subtitles & tracks"), route: "settings-subtitles",
                  hint: qsTr("Languages, style, forced/SDH") },
                { label: qsTr("Streams & autoplay"), route: "settings-streams",
                  hint: qsTr("Autoplay, next episode, skip intro") },
                { label: qsTr("Continue Watching"), route: "settings-continuewatching",
                  hint: qsTr("Rail visibility and style") },
                { label: qsTr("Homescreen"), route: "settings-homescreen",
                  hint: qsTr("Hero, rails, rail order") },
                { label: qsTr("Tracking"), route: "settings-tracking",
                  hint: qsTr("Trakt, SIMKL scrobbling") },
                { label: qsTr("Debrid"), route: "settings-debrid",
                  hint: qsTr("Providers, resolver, templates") },
                { label: qsTr("Notifications"), route: "settings-notifications",
                  hint: qsTr("Episode release alerts") },
                { label: qsTr("TMDB"), route: "settings-tmdb",
                  hint: qsTr("Enrichment, API key, modules") },
                { label: qsTr("MDBList"), route: "settings-mdblist",
                  hint: qsTr("External ratings, providers") },
                { label: qsTr("Downloads"), route: "downloads",
                  hint: qsTr("Offline files, active and completed") },
                { label: qsTr("Integrations"),     route: "settings-integrations",
                  hint: qsTr("Discord, torrent cache") },
                { label: qsTr("Account"),          route: "settings-account",
                  hint: qsTr("Sign-in state") }
            ]
            delegate: Item {
                required property var modelData
                width: parent.width
                height: 56

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: Theme.surface
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: navigation.push(modelData.route)
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -10
                    text: modelData.label
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 12
                    text: modelData.hint
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: Theme.textSecondary
                    font.pixelSize: 20
                }
            }
        }

        Text {
            text: qsTr("Values persist to the shared Nuvio profile "
                       + "(~/.config/nuvio-linux).")
            color: Theme.textDisabled
            font.pixelSize: 11
        }
    }
}
