import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: library

    // Skeleton data: real catalog federation arrives with the library
    // system; these are keyless CDN posters proving the async pipeline.
    readonly property var shelf: [
        { t: "The Shawshank Redemption", id: "tt0111161" },
        { t: "Pulp Fiction",             id: "tt0110912" },
        { t: "The Matrix",               id: "tt0133093" },
        { t: "Spirited Away",            id: "tt0245429" },
        { t: "Parasite",                 id: "tt6751668" },
        { t: "Mad Max: Fury Road",       id: "tt1392214" },
        { t: "Blade Runner 2049",        id: "tt1856101" },
        { t: "Dune",                     id: "tt1160419" },
        { t: "Everything Everywhere",    id: "tt6710474" },
        { t: "Oppenheimer",              id: "tt15398776" },
        { t: "Interstellar",             id: "tt0816692" },
        { t: "Whiplash",                 id: "tt2582802" }
    ]

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
            text: qsTr("Library")
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
        model: library.shelf

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
                    text: modelData.t
                    color: Theme.textPrimary
                    font.pixelSize: 12
                }
            }
            // Navigation-only for now: launching media lands with the
            // playback-session module (plan §8 P1). Cards stay inert.
        }
    }
}
