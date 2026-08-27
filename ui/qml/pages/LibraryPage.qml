import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: library

    // Skeleton data: real catalog federation arrives with the library
    // system; these are keyless CDN posters proving the async pipeline.
    // Live catalog: fed by CatalogService (Cinemeta). Loading + error are
    // first-class states so the grid never lies about what it has.
    property var shelf: []
    property bool loading: false

    Component.onCompleted:
        loading = true,
        catalog.fetch("movie", "top")

    Connections {
        target: catalog
        function onCatalogReady(type, catalogId, items) {
            loading = false
            shelf = items
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
            text: library.loading ? qsTr("Library — loading…") : qsTr("Library")
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

    // ---- poster rail (async provider proof) ---------------------------------
    GridView {
        anchors.fill: parent
        anchors.topMargin: header.height
        anchors.margins: Theme.spacingLg
        clip: true
        cellWidth: 160
        cellHeight: 260
        model: shelf.length > 0 ? shelf : []

        Text {
            anchors.centerIn: parent
            visible: shelf.length === 0 && !library.loading
            text: qsTr("No items (catalog empty or unreachable)")
            color: Theme.textDisabled
        }

        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            width: library.cellWidth - Theme.spacingSm
            height: library.cellHeight - Theme.spacingSm

            Rectangle {
                anchors.fill: parent
                color: "#1c1c22"
                radius: 6

                Image {
                    id: art
                    anchors.fill: parent
                    anchors.margins: 1
                    sourceSize.width: 320          // decode cap for skeleton
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    source: "image://poster/https://images.metahub.space/poster/medium/"
                            + modelData.id + "/img"
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WrapAnywhere
                    text: modelData.name
                    color: Theme.textPrimary
                    font.pixelSize: 12
                }
            }
            // Navigation-only for now: launching media lands with the
            // playback-session module (plan §8 P1). Cards stay inert.
        }
    }
}
