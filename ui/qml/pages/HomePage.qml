import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: home

    // ---- continue-watching rail (shared Compose profile store) ----------------
    // Rows recorded by the player (>=1 s positions, not completed); clicking
    // relaunches playback for that item (episode ids recomposed as "tt:S:E").
    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd
        visible: watching.cwPrefs.visible !== false
                 && watching.continueWatching.length > 0

        Text {
            text: qsTr("Continue Watching")
            color: Theme.textSecondary
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        ListView {
            id: cwRail
            width: parent.width
            height: 190
            orientation: ListView.Horizontal
            clip: true
            spacing: Theme.spacingMd
            model: watching.continueWatching

            delegate: Item {
                required property var modelData
                width: 150
                height: 190

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: cwArea.containsMouse
                           ? Theme.surfaceHigh : Theme.surface
                }
                MouseArea {
                    id: cwArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        const m = modelData
                        const key = m.season >= 0
                            ? m.id + ":" + m.season + ":" + m.episode
                            : m.id
                        playback.requestPlay(m.type, key, m.title)
                    }
                }
                Text {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    width: parent.width - 20
                    text: modelData.title
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    anchors.bottom: cwBar.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: modelData.season >= 0
                          ? qsTr("S%1 E%2").arg(modelData.season)
                                             .arg(modelData.episode)
                          : ""
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                // resume progress bar (fraction of duration watched)
                Rectangle {
                    id: cwBar
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 10
                    width: parent.width - 20
                    height: 4
                    radius: 2
                    color: Theme.surface
                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        width: Math.max(4, parent.width * modelData.fraction)
                        radius: 2
                        color: Theme.accent
                    }
                }
            }
        }
    }

    // ---- idle brand mark (until media launches from CLI/hooks) -------------
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingMd

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
    }

    // ---- navigation skeleton entry (Phase 3 demo wiring) -------------------
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 90
        spacing: Theme.spacingSm

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Browse Library")
            onClicked: navigation.pushIfDifferent("library")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Search")
            onClicked: navigation.pushIfDifferent("search")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Settings")
            onClicked: navigation.pushIfDifferent("settings")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Add-ons")
            onClicked: navigation.pushIfDifferent("addons")
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("usage: nuvio-linux-qt <file|url>   ·   F11 fullscreen")
            color: Theme.textDisabled
            font.pixelSize: 12
        }
    }
}
