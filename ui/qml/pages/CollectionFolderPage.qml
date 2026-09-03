import QtQuick
import QtQuick.Controls
import "../theme"

// Folder items (P5): merged grid across the folder's addon-catalog
// sources with a source picker. Live-bound to the shared selection.
Item {
    id: folderPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            width: parent.width - 200
            text: collections.folderTitle.length > 0
                   ? collections.folderTitle : qsTr("Folder")
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
    }

    Column {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingSm

        ComboBox {
            visible: collections.folderSources.length > 1
            width: 320
            textRole: "label"
            model: [{ label: qsTr("All sources"), value: -1 }].concat(
                collections.folderSources.map(function(s, i) {
                    return { label: s.addonId + " · " + s.type + "/"
                                    + s.catalogId, value: i }
                }))
            // Index tracks the shared filter (-1 All first, then sources).
            currentIndex: collections.folderSourceIndex < 0
                          ? 0 : collections.folderSourceIndex + 1
            onActivated: function(i) {
                collections.folderSourceIndex = model[i].value
            }
        }

        Text {
            visible: collections.folderItems.length === 0
            text: qsTr("No items resolved yet.")
            color: Theme.textDisabled
            font.pixelSize: 13
        }

        GridView {
            width: parent.width
            height: parent.height - y
            clip: true
            cellWidth: 142
            cellHeight: 208
            model: collections.folderItems

            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                width: 130
                height: 196

                required property var modelData

                Rectangle {
                    anchors.fill: parent
                    color: "#1c1c22"
                    radius: 6

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        sourceSize.width: 260
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        source: "image://poster/" + modelData.poster
                    }
                    Rectangle {
                        visible: watching.isWatched(
                            modelData.type || "movie", modelData.id)
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 6
                        width: 22; height: 22
                        radius: 11
                        color: Theme.chromeScrim
                        Text {
                            anchors.centerIn: parent
                            text: "✓"
                            color: Theme.accent
                            font.pixelSize: 13
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            meta.load(modelData.type || "movie",
                                      modelData.id, modelData.name)
                            navigation.push("meta")
                        }
                    }
                    Text {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 8
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: modelData.name
                        color: Theme.textPrimary
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
