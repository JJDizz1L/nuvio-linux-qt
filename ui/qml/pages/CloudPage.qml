import QtQuick
import QtQuick.Controls
import "../theme"

// Cloud library browser (D3): per-provider stored downloads with
// per-file playback resolution. Plays through the player route like
// trailers (no watch session - cloud ids are provider-scoped).
Item {
    id: cloudPage

    property string error: ""

    Connections {
        target: cloud
        function onPlaybackResolved(url, filename) {
            navigation.push("video")
            pageItem.launchMedia(url)
        }
        function onPlaybackFailed(message) {
            cloudPage.error = message
        }
    }

    Component.onCompleted: cloud.refresh()

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: cloud.loading ? qsTr("Cloud — loading…") : qsTr("Cloud")
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
        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 110
            text: qsTr("Refresh")
            flat: true
            enabled: !cloud.loading
            onClicked: {
                cloudPage.error = ""
                cloud.refresh()
            }
        }
    }

    ScrollView {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            anchors.margins: Theme.spacingLg

            Text {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                visible: cloudPage.error.length > 0
                text: cloudPage.error
                color: "#e57373"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Text {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                visible: !cloud.loading && cloud.items.length === 0
                text: qsTr("Nothing stored. Connect Torbox or Premiumize in "
                           + "Debrid settings to browse cloud downloads.")
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Repeater {
                model: cloud.items
                delegate: Rectangle {
                    required property var modelData
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    height: itemCol.height + 20
                    radius: Theme.radiusMd
                    color: Theme.surface

                    Column {
                        id: itemCol
                        property var itemRef: modelData
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        spacing: 4

                        Text {
                            width: parent.width
                            text: modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: [modelData.providerName,
                                   modelData.status || "",
                                   modelData.type || ""]
                                .filter(function(s){ return String(s).length > 0 })
                                .join(" · ")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Repeater {
                            model: modelData.files
                            delegate: Row {
                                required property var modelData
                                width: parent.width
                                spacing: Theme.spacingSm
                                Text {
                                    width: parent.width - 120
                                    text: (modelData.playable ? "" : "○ ")
                                          + modelData.name
                                    color: modelData.playable
                                           ? Theme.textPrimary
                                           : Theme.textDisabled
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Button {
                                    visible: modelData.playable
                                    text: qsTr("Play")
                                    flat: true
                                    onClicked: {
                                        cloudPage.error = ""
                                        cloud.resolvePlayback(
                                            itemCol.itemRef.providerId,
                                            itemCol.itemRef.id,
                                            itemCol.itemRef.type,
                                            modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.spacingLg }
        }
    }
}
