import QtQuick
import QtQuick.Controls
import "../theme"

// Collection folders (P5): folder cards of the open collection; a card
// loads its items and opens the folder route. Edit via Collections.
Item {
    id: collectionDetail

    property var detail: collections.openCollection

    // Re-pull the shared selection whenever anything changes (edits do
    // not emit opened, only changed).
    function refresh() { detail = collections.openCollection }
    Connections {
        target: collections
        function onOpened() { collectionDetail.refresh() }
        function onChanged() { collectionDetail.refresh() }
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            width: parent.width - 320
            text: detail.title || qsTr("Collection")
            color: Theme.textPrimary
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
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
            text: qsTr("Edit")
            flat: true
            onClicked: navigation.push("collections")
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

            Repeater {
                model: detail.folders || []
                delegate: Rectangle {
                    required property var modelData
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    height: 64
                    radius: Theme.radiusMd
                    color: Theme.surface

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            collections.loadFolder(detail.id, modelData.id)
                            navigation.push("collectionfolder")
                        }
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: -10
                        text: modelData.title
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 12
                        text: qsTr("%n source(s)", "",
                                   modelData.sources.length)
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: "›"
                        color: Theme.textSecondary
                        font.pixelSize: 20
                    }
                }
            }

            Text {
                visible: (detail.folders || []).length === 0
                x: Theme.spacingLg
                text: qsTr("No folders yet — add some in Collections.")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }
    }
}
