import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: library

    // ---- live catalog: rails filled by CatalogService.loadShelves() --------
    // Three standard rails (movies / series / anime). A rail keeps its
    // header + position while loading or empty so layout never jumps.
    property bool anyLoading: false

    Component.onCompleted: catalog.loadShelves()

    Connections {
        target: catalog
        function onShelvesChanged() {
            var any = false
            for (let i = 0; i < catalog.shelves.length; ++i)
                if (catalog.shelves[i].loading) { any = true; break }
            library.anyLoading = any
        }
    }

    // ---- header ------------------------------------------------------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: library.anyLoading ? qsTr("Library — loading…")
                                     : qsTr("Library")
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

    // ---- rails -------------------------------------------------------------
    ListView {
        id: railList
        anchors.fill: parent
        anchors.topMargin: header.height
        clip: true
        model: catalog.shelves.length
        orientation: ListView.Vertical
        spacing: Theme.spacingLg

        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            width: railList.width
            height: 246

            required property int index
            readonly property var shelfInfo: catalog.shelves[index]

            Column {
                anchors.fill: parent
                spacing: 6

                Text {
                    x: Theme.spacingLg
                    text: shelfInfo.title +
                          (shelfInfo.loading ? qsTr("  …") : "")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    x: Theme.spacingLg
                    visible: shelfInfo.items.length === 0 &&
                             !shelfInfo.loading
                    text: qsTr("empty or unreachable")
                    color: Theme.textDisabled
                    font.pixelSize: 12
                }
                ListView {
                    width: parent.width
                    height: 200
                    clip: true
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingSm
                    model: shelfInfo.items

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
                            // Card click exercises the live resolution
                            // pipeline: addon endpoints -> policy -> log.
                            // Actual launch into the player lands with the
                            // playback-session module (plan §8 P1).
                            MouseArea {
                                anchors.fill: parent
                                onClicked:
                                    streams.resolve("movie", modelData.id)
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
    }
}
