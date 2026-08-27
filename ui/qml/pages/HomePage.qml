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
                // Compose ContinueWatchingWideCard (>=1440dp tier):
                // 400x160 card, 100px artwork strip, 16px content padding,
                // radius 18, title 20 bold, meta 16, progress 6 + label 14.
                width: 400
                height: 160

                Rectangle {
                    anchors.fill: parent
                    radius: 18
                    color: Theme.surface
                    border.width: 1
                    border.color: "#26ffffff"   // Compose: white 15%
                }
                Image {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 100
                    visible: modelData.artwork !== ""
                    source: visible ? "image://poster/" + modelData.artwork : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.height: 320
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
                Item {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.leftMargin: 100
                    anchors.right: parent.right

                    Text {
                        id: cwTitle
                        anchors.top: parent.top
                        anchors.topMargin: 16
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        text: modelData.title
                        color: Theme.textPrimary
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Text {
                        id: cwMeta
                        anchors.top: cwTitle.bottom
                        anchors.topMargin: 6
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        text: modelData.season >= 0
                              ? qsTr("S%1 E%2").arg(modelData.season)
                                                 .arg(modelData.episode)
                              : qsTr("Movie")
                        color: Theme.textSecondary
                        font.pixelSize: 16
                        elide: Text.ElideRight
                    }
                    Text {
                        id: cwEpisodeTitle
                        anchors.top: cwMeta.bottom
                        anchors.topMargin: 6
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        visible: modelData.episodeTitle !== ""
                        text: modelData.episodeTitle
                        color: Theme.textSecondary
                        font.pixelSize: 16
                        elide: Text.ElideRight
                    }
                    Text {
                        id: cwPct
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 16
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        visible: modelData.fraction > 0
                        // Compose: roundToInt().coerceIn(1, 99) + "% watched"
                        text: qsTr("%1 watched").arg(
                                  Math.max(1, Math.min(99,
                                      Math.round(modelData.fraction * 100)))
                                  + "%")
                        color: Theme.textSecondary
                        font.pixelSize: 14
                    }
                    Rectangle {
                        anchors.bottom: cwPct.top
                        anchors.bottomMargin: 8
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        height: 6
                        radius: 3
                        visible: modelData.fraction > 0
                        color: "#1affffff"   // Compose: white 10% track
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Math.max(6, parent.width *
                                Math.min(1, Math.max(0, modelData.fraction)))
                            radius: 3
                            color: Theme.accent
                        }
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
