import QtQuick
import QtQuick.Controls
import "../theme"

// Global search: Cinemeta "search=<q>" catalogs (movie + series fanned out
// in parallel by the catalog service), recent-search chips riding the shared
// search_history.properties store (Compose parity). Card click opens the
// detail route via the standard meta context object, carrying the section
// type so MetaPage queries the right catalog flavor.
Item {
    id: searchPage

    readonly property int debounceMs: 350

    function runQuery(q) {
        const trimmed = q.trim()
        catalog.search(trimmed)
        if (trimmed.length >= 2) searchHistory.record(trimmed)
    }

    // ---- header -------------------------------------------------------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Search")
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

    // ---- query row ------------------------------------------------------------
    Column {
        id: queryRow
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingSm

        TextField {
            id: queryField
            width: parent.width
            placeholderText:
                qsTr("Search movies and series… (min. 2 characters)")
            selectByMouse: true
            onAccepted: searchPage.runQuery(text)
            onTextChanged: debounce.restart()
        }
        Timer {
            id: debounce
            interval: searchPage.debounceMs
            onTriggered: searchPage.runQuery(queryField.text)
        }

        Label {
            visible: catalog.searchError.length > 0
            text: catalog.searchError
            color: "#ff9a9a"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            width: parent.width
        }
        Label {
            visible: catalog.searchActive
            text: qsTr("Searching…")
            color: Theme.textSecondary
            font.pixelSize: 12
        }
    }

    // ---- body ------------------------------------------------------------------
    Item {
        id: body
        anchors.top: queryRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // Recent searches (only while the query is empty/too short).
        Column {
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd
            visible: queryField.text.trim().length < 2

            Text {
                text: qsTr("Recent")
                color: Theme.textSecondary
                font.pixelSize: 13
                visible: searchHistory.recent.length > 0
            }
            Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    model: searchHistory.recent
                    delegate: Button {
                        required property var modelData
                        text: modelData
                        flat: true
                        onClicked: {
                            queryField.text = modelData
                            searchPage.runQuery(modelData)
                        }
                    }
                }
            }
            Button {
                text: qsTr("Clear history")
                visible: searchHistory.recent.length > 0
                flat: true
                onClicked: searchHistory.clear()
            }
        }

        // Search results (visible while a real query is present).
        // Two typed grids mirror the LibraryPage card idiom; the section
        // type rides each delegate so MetaPage gets movie vs series flavor.
        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: resultsCol.implicitHeight + Theme.spacingLg * 2
            clip: true
            visible: queryField.text.trim().length >= 2

            Column {
                id: resultsCol
                x: Theme.spacingLg
                y: Theme.spacingLg
                width: parent.width - Theme.spacingLg * 2
                spacing: Theme.spacingLg

                Repeater {
                    model: [
                        { title: qsTr("Movies"), type: "movie",
                          items: catalog.searchMovieResults },
                        { title: qsTr("Series"), type: "series",
                          items: catalog.searchSeriesResults }
                    ]

                    delegate: Column {
                        id: sectionCol
                        required property var modelData
                        readonly property int count:
                            modelData.items ? modelData.items.length : 0
                        width: parent.width
                        spacing: Theme.spacingSm

                        Text {
                            visible: sectionCol.count > 0
                            text: sectionCol.modelData.title + "  \u00b7  " +
                                  sectionCol.count
                            color: Theme.textPrimary
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: sectionCol.count === 0 &&
                                     !catalog.searchActive &&
                                     sectionCol.modelData.type === "movie"
                            text: qsTr("No results")
                            color: Theme.textDisabled
                            font.pixelSize: 13
                        }

                        GridView {
                            id: grid
                            width: parent.width
                            readonly property int cellsPerRow:
                                Math.max(1, Math.floor(width / 138))
                            height: sectionCol.count > 0
                                        ? Math.ceil(sectionCol.count /
                                                    cellsPerRow) * (196 + 8)
                                        : 0
                            cellWidth: 138
                            cellHeight: 196 + 8
                            interactive: false
                            model: sectionCol.modelData.items

                            delegate: Item {
                                width: grid.cellWidth - 8
                                height: grid.cellHeight - 8
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
                                        source: "image://poster/" +
                                                modelData.poster
                                    }
                                    // Watched badge (shared Compose store):
                                    // same movie-level check as the library
                                    // cards.
                                    Rectangle {
                                        visible: watching.isWatched(
                                                     sectionCol.modelData.type,
                                                     modelData.id)
                                        anchors.top: parent.top
                                        anchors.right: parent.right
                                        anchors.margins: 6
                                        width: 22; height: 22
                                        radius: 11
                                        color: Theme.chromeScrim
                                        Text {
                                            anchors.centerIn: parent
                                            text: "\u2713"
                                            color: Theme.accent
                                            font.pixelSize: 13
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            meta.load(
                                                sectionCol.modelData.type,
                                                modelData.id,
                                                modelData.name)
                                            navigation.push("meta")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
