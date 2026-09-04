import QtQuick
import QtQuick.Controls
import Nuvio.Mpv 1.0
import "../theme"

Item {
    id: library

    // ---- live catalog: rails filled by CatalogService.loadShelves() --------
    // Three standard rails (movies / series / anime). A rail keeps its
    // header + position while loading or empty so layout never jumps.
    property bool anyLoading: false

    Component.onCompleted: catalog.loadShelves()

    // ---- poster hover trailer preview (Compose parity) ----------------------
    // Hover a rail poster for ~2s -> fetch its meta, resolve the first
    // YouTube trailer in PREVIEW mode, and play it muted in a popup over the
    // card (single shared hero mpv instance; routes are exclusive so the
    // instance is idle while browsing the library).
    property string previewKey: ""        // "type:id" of the hovered card
    property string previewTitle: ""
    property string previewType: ""
    property point previewPos: Qt.point(0, 0)
    readonly property bool previewVisible: previewKey !== ""

    function requestPreview(type, id, name, pos) {
        if (typeof heroController === "undefined") return
        if (!appsettings.hoverPreviewEnabled) return
        const k = type + ":" + id
        if (library.previewKey === k) return
        library.stopPreview()
        library.previewKey = k
        library.previewType = type
        library.previewTitle = name
        library.previewPos = pos
        meta.load(type, id, name)         // async; currentChanged continues
    }
    function stopPreview() {
        if (library.previewKey === "") return
        library.previewKey = ""
        if (typeof heroController !== "undefined")
            heroController.enqueueCommand(["stop"])
    }
    function extractPreviewKey() {
        const list = meta.current.trailers || []
        for (let i = 0; i < list.length; ++i)
            if (list[i].provider === "youtube") return list[i].key
        return ""
    }

    Connections {
        target: meta
        function onCurrentChanged() {
            if (library.previewKey === "") return
            const c = meta.current
            if ((c.id || "") + "" !==
                library.previewKey.substring(library.previewType.length + 1))
                return
            const key = library.extractPreviewKey()
            if (key !== "") trailer.resolveForKeyPreview(key)
            else library.stopPreview()
        }
    }
    Connections {
        target: trailer
        function onPreviewResolved(url, audioUrl) {
            if (library.previewKey === "") return
            heroController.play(url, audioUrl || "", 0)
        }
        function onPreviewFailed(reason) { library.stopPreview() }
    }
    // Popup surface (positioned over the hovered card).
    Rectangle {
        id: previewPopup
        visible: library.previewKey !== ""
        x: library.previewPos.x
        y: Math.max(8, library.previewPos.y - 8)
        width: 264
        height: 200
        radius: 8
        color: "#101014"
        border.color: Theme.accent
        border.width: 1
        clip: true
        z: 50
        MpvItem {
            id: previewMpv
            anchors.fill: parent
            controller: (typeof heroController !== "undefined")
                            ? heroController : null
        }
        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 8
            text: library.previewTitle
            color: "#ffffff"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            styleColor: "#000000"
            style: Text.Outline
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                const parts = library.previewKey.split(":")
                meta.load(parts[0],
                          library.previewKey.substring(parts[0].length + 1),
                          library.previewTitle)
                library.stopPreview()
                navigation.push("meta")
            }
        }
    }

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
        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 110
            text: qsTr("Cloud")
            flat: true
            onClicked: navigation.push("cloud")
        }
    }

    // ---- my library + collections + catalog rails ------------------------
    // My Library and Collections ride the shared Compose profile stores;
    // catalog rails below are the live Cinemeta federation as before.
    ScrollView {
        anchors.fill: parent
        anchors.topMargin: header.height
        clip: true

        Column {
            width: parent.width
            spacing: Theme.spacingLg

            Column {
                width: parent.width
                spacing: 6
                visible: mylibrary.count > 0

                Text {
                    x: Theme.spacingLg
                    text: qsTr("My Library (%1)").arg(mylibrary.count)
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                ListView {
                    width: parent.width
                    height: 200
                    clip: true
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingSm
                    model: mylibrary.items
                    header: Item { width: Theme.spacingLg; height: 1 }

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
                                hoverEnabled: true
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

            Column {
                width: parent.width
                spacing: 6

                Row {
                    x: Theme.spacingLg
                    spacing: Theme.spacingSm
                    Text {
                        text: qsTr("Collections (%1)")
                              .arg(collections.collections.length)
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: qsTr("Manage")
                        flat: true
                        onClicked: navigation.push("collections")
                    }
                }
                ListView {
                    visible: collections.collections.length > 0
                    width: parent.width
                    height: 110
                    clip: true
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingSm
                    model: collections.collections
                    header: Item { width: Theme.spacingLg; height: 1 }

                    delegate: Item {
                        width: 220
                        height: 100

                        required property var modelData

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.surface
                            radius: 12
                            border.width: 1
                            border.color: "#26ffffff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                collections.openCollection(modelData.id)
                                navigation.push("collectiondetail")
                            }
                        }
                        Column {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4
                            Text {
                                width: parent.width
                                text: (modelData.pinned ? "📌 " : "")
                                      + modelData.title
                                color: Theme.textPrimary
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                text: qsTr("%n folder(s)", "",
                                          modelData.folders.length)
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            Repeater {
                model: catalog.shelves
                delegate: Item {
                    width: library.width
                    height: 246

                    required property var modelData
                    readonly property var shelfInfo: modelData

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
                            // Watched badge (shared Compose store): a small
                            // check overlay when this movie is fully watched.
                            Rectangle {
                                visible: watching.isWatched(shelfInfo.type,
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
                            // Card click opens the DETAIL route; playback
                            // intent moves to the meta page's Play/episode
                            // actions (which funnel through the same
                            // playback session).
                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    hoverTimer.stop()
                                    meta.load(shelfInfo.type,
                                              modelData.id,
                                              modelData.name)
                                    navigation.push("meta")
                                }
                                Timer {
                                    id: hoverTimer
                                    interval: appsettings.hoverPreviewDelayMs
                                    onTriggered: {
                                        if (!cardArea.containsMouse) return
                                        if (!appsettings.hoverPreviewEnabled)
                                            return
                                        library.requestPreview(
                                            shelfInfo.type,
                                            modelData.id,
                                            modelData.name,
                                            cardArea.mapToItem(library, -65, 0))
                                    }
                                }
                                onContainsMouseChanged: {
                                    if (containsMouse) hoverTimer.restart()
                                    else {
                                        hoverTimer.stop()
                                        library.stopPreview()
                                    }
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
    }
}
}
}
