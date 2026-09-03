import QtQuick
import QtQuick.Controls
import "../theme"

// Home route (P4): hero spotlight + Continue Watching + per-addon catalog
// rails from HomeShelves (installed-addon manifests, settings-ordered).
// Poster cards mirror the library idiom (poster, watched badge, click to
// detail); playback intent stays on the detail page except the hero Play.
Item {
    id: home

    Component.onCompleted: homeshelves.refresh()

    ScrollView {
        anchors.fill: parent
        clip: true

        Column {
            width: parent.width
            spacing: Theme.spacingLg

            // ---- hero spotlight -----------------------------------------
            // First item of the hero-enabled sections (deterministic; the
            // Compose line picks seeded-random - noted divergence).
            Rectangle {
                visible: homeshelves.heroItems.length > 0
                width: parent.width - 2 * Theme.spacingLg
                x: Theme.spacingLg
                height: 220
                radius: 18
                color: Theme.surface
                border.width: 1
                border.color: "#26ffffff"

                readonly property var hero: homeshelves.heroItems.length > 0
                                            ? homeshelves.heroItems[0] : ({})

                Image {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    width: 130
                    source: parent.hero.poster
                            ? "image://poster/" + parent.hero.poster : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.height: 440
                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 158
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Text {
                        width: parent.width
                        text: parent.parent.hero.name || ""
                        color: Theme.textPrimary
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: {
                            const bits = []
                            if (parent.parent.hero.type)
                                bits.push(parent.parent.hero.type)
                            if (parent.parent.hero.year)
                                bits.push(parent.parent.hero.year)
                            return bits.join(" · ")
                        }
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: (parent.parent.hero.description || "") !== ""
                        text: parent.parent.hero.description || ""
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                    }
                    Row {
                        spacing: Theme.spacingSm
                        Button {
                            text: qsTr("Play")
                            onClicked: {
                                const h = parent.parent.parent.hero
                                playback.requestPlay(h.type || "movie",
                                                     h.id, h.name || "")
                            }
                        }
                        Button {
                            text: qsTr("Info")
                            flat: true
                            onClicked: {
                                const h = parent.parent.parent.hero
                                meta.load(h.type || "movie", h.id,
                                          h.name || "")
                                navigation.push("meta")
                            }
                        }
                    }
                }
            }

            // ---- continue-watching rail (shared Compose profile store) --
            // Rows recorded by the player (>=1 s positions, not completed);
            // clicking relaunches playback (episode ids "tt:S:E").
            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: watching.cwPrefs.visible !== false
                         && watching.continueWatching.length > 0

                Text {
                    x: Theme.spacingLg
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
                        width: 400
                        height: 160

                        Rectangle {
                            anchors.fill: parent
                            radius: 18
                            color: Theme.surface
                            border.width: 1
                            border.color: "#26ffffff"
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
                                color: "#1affffff"
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

            // ---- addon catalog rails --------------------------------------
            Repeater {
                model: homeshelves.sections
                delegate: Column {
                    required property var modelData
                    width: home.width
                    spacing: 6

                    Text {
                        x: Theme.spacingLg
                        text: modelData.title +
                              (modelData.loading ? qsTr("  …") : "")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        x: Theme.spacingLg
                        visible: modelData.items.length === 0
                                 && !modelData.loading
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
                        model: modelData.items
                        // Left inset so the first card aligns with headers.
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
                                    source: "image://poster/"
                                            + modelData.poster
                                }
                                Rectangle {
                                    visible: watching.isWatched(
                                        modelData.type || "movie",
                                        modelData.id)
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
                                                  modelData.id,
                                                  modelData.name)
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

            // Bottom breathing room inside the scroll view.
            Item { width: 1; height: Theme.spacingLg }
        }
    }
}
