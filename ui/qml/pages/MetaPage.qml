import QtQuick
import QtQuick.Controls
import Nuvio.Mpv 1.0
import "../theme"

// Detail/meta route: poster + backdrop, info block, Play (movies), and a
// season/episode picker for series/anime. Playback intent funnels through
// the playback session exactly like card clicks do — composite series ids
// ("tt123:S:E") ride the resolver unchanged.
Item {
    id: detail

    // Convenience accessors over meta.current
    readonly property var cur: (typeof meta !== "undefined" && meta !== null)
                               ? meta.current : ({})
    readonly property string type: cur.type || "movie"
    readonly property string imdbId: cur.id || ""
    readonly property bool isSeries: type === "series" || type === "anime"
    readonly property var videos: cur.videos || []

    // Distinct season list from the normalized episode set.
    readonly property var seasons: {
        const seen = {}
        const out = []
        for (let i = 0; i < videos.length; ++i) {
            const s = videos[i].season
            if (!seen[s]) { seen[s] = true; out.push(s) }
        }
        return out.sort(function(a,b){return a-b})
    }
    property int seasonIndex: 0
    readonly property int activeSeason:
        seasons.length > 0 ? seasons[Math.min(seasonIndex, seasons.length-1)]
                           : 0

    function episodeModel(season) {
        const out = []
        for (let i = 0; i < videos.length; ++i)
            if (videos[i].season === season) out.push(videos[i])
        return out
    }

    function playMovie() {
        playback.requestPlay(type, imdbId, cur.name || "")
    }
    function playEpisode(ep) {
        playback.requestPlay(type,
                             imdbId + ":" + ep.season + ":" + ep.episode,
                             ep.name || "")
    }

    // First YouTube trailer of the detail payload (Cinemeta trailers shape:
    // [{source:"youtube", key:"..."}]), empty when none.
    readonly property var youtubeTrailer: {
        const list = cur.trailers || []
        for (let i = 0; i < list.length; ++i)
            if (list[i].provider === "youtube") return list[i]
        return null
    }
    function playTrailer() {
        if (youtubeTrailer) trailer.resolveForKey(youtubeTrailer.key)
    }

    // ---- hero ambient trailer (Compose detail-hero parity) -------------------
    // Autoplays the first YouTube trailer MUTED as a full-bleed backdrop
    // while the detail page is visible; stops on route leave and on meta
    // switch. Kill switch: NUVIO_NO_HERO=1 removes heroController entirely.
    readonly property bool heroEnabled:
        (typeof heroAmbientEnabled !== "undefined") && heroAmbientEnabled
    property string heroKey: ""
    readonly property bool heroActive:
        heroEnabled && heroKey.length > 0 && !smokeActive && visible

    function maybeStartHero() {
        if (!heroEnabled || smokeActive || !visible) return
        const t = youtubeTrailer
        if (!t || t.key === heroKey) return
        heroKey = t.key
        trailer.resolveForKeyAmbient(t.key)
    }
    function stopHero() {
        if (heroKey.length === 0) return
        heroKey = ""
        if (typeof heroController !== "undefined")
            heroController.enqueueCommand(["stop"])
    }
    onVisibleChanged: visible ? maybeStartHero() : stopHero()
    onCurChanged: maybeStartHero()
    Component.onCompleted: maybeStartHero()
    Connections {
        target: trailer
        function onAmbientResolved(url, audioUrl) {
            if (!detail.visible || detail.heroKey.length === 0) return
            heroItem.play(url, audioUrl || "", 0)
        }
        function onAmbientFailed(reason) { detail.heroKey = "" }
    }

    // ---- hero video layer (FIRST child => renders behind all content) -------
    MpvItem {
        id: heroItem
        anchors.fill: parent
        visible: detail.heroActive
        controller: (typeof heroController !== "undefined")
                        ? heroController : null
    }
    // Readability scrim over the ambient video.
    Rectangle {
        anchors.fill: parent
        visible: detail.heroActive
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#59000000" }
            GradientStop { position: 1.0; color: "#E6000000" }
        }
    }

    // ---- backdrop ------------------------------------------------------------
    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        opacity: 0.16
        asynchronous: true
        sourceSize.width: 1280
        visible: (cur.background || "").length > 0 && !detail.heroActive
        source: (visible && (cur.background || "").length > 0)
                    ? "image://poster/" + cur.background : ""
    }

    // ---- header row ----------------------------------------------------------
    Item {
        id: head
        height: 56
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            text: qsTr("Back")
            onClicked: navigation.pop()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 130
            width: parent.width - 260
            text: (cur.name || "") + (meta.loading ? "  …" : "")
            color: Theme.textSecondary
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    // ---- body ----------------------------------------------------------------
    Row {
        id: bodyRow
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        Image {
            id: posterImage
            width: 210
            height: 315
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.width: 420
            source: (cur.poster || "").length > 0
                    ? "image://poster/" + cur.poster : ""
        }

        Column {
            width: parent.width - posterImage.width - Theme.spacingLg * 2
            spacing: Theme.spacingSm

            Text {
                text: cur.name || ""
                color: Theme.textPrimary
                font.pixelSize: 26
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                width: parent.width
            }
            Text {
                text: [cur.releaseInfo || "", cur.runtime || "",
                       cur.imdbRating ? "★ " + cur.imdbRating : ""]
                    .filter(function(s){ return String(s).length > 0 })
                    .join("   ·   ")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            Text {
                visible: (cur.genres || []).length > 0
                text: (cur.genres || []).join(" · ")
                color: Theme.accent
                font.pixelSize: 12
                wrapMode: Text.Wrap
                width: parent.width
            }
            Text {
                text: cur.description || ""
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.Wrap
                width: parent.width
                maximumLineCount: 6
                elide: Text.ElideRight
            }

            Button {
                visible: !isSeries && imdbId.length > 0
                text: watching.isWatched(type, imdbId)
                      ? qsTr("\u2713  Watched \u2014 tap to undo")
                      : qsTr("\u25B6  Play")
                onClicked: {
                    if (watching.isWatched(type, imdbId))
                        watching.unmarkWatched(type, imdbId, -1, -1)
                    else
                        detail.playMovie()
                }
            }
            Button {
                visible: !isSeries && imdbId.length > 0
                         && !watching.isWatched(type, imdbId)
                flat: true
                text: qsTr("\u2713  Mark watched")
                onClicked: watching.markWatched(type, imdbId, -1, -1,
                                                Date.now())
            }

            Button {
                visible: youtubeTrailer !== null
                text: trailer.resolving ? qsTr("Resolving…") : qsTr("▶  Trailer")
                enabled: !trailer.resolving
                onClicked: detail.playTrailer()
            }

            ComboBox {
                visible: isSeries && seasons.length > 1
                width: 220
                model: seasons.map(function(s){
                    return qsTr("Season %1").arg(s)
                })
                currentIndex: detail.seasons.length > 0
                    ? Math.min(detail.seasonIndex,
                               detail.seasons.length - 1) : 0
                onActivated: function(i) { detail.seasonIndex = i }
            }

            ListView {
                visible: isSeries && videos.length > 0
                width: parent.width
                height: isSeries ? Math.min(contentHeight, 360) : 0
                clip: true
                spacing: 4
                model: detail.episodeModel(detail.activeSeason)

                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    required property var modelData
                    width: ListView.view.width
                    height: 66

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusMd * 0.5
                        color: epArea.containsMouse
                               ? Theme.surfaceHigh : Theme.surface
                    }
                    MouseArea {
                        id: epArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: detail.playEpisode(modelData)
                    }
                    Image {
                        id: epThumb
                        x: Theme.spacingMd
                        anchors.verticalCenter: parent.verticalCenter
                        width: 104
                        height: 58
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        clip: true
                        source: modelData.thumb || ""
                        visible: source.length > 0
                    }
                    Column {
                        x: (epThumb.visible
                                ? epThumb.x + epThumb.width
                                : Theme.spacingMd) + Theme.spacingMd
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - 56
                        spacing: 3
                        Text {
                            width: parent.width
                            text: "E" + modelData.episode + "   "
                                  + (modelData.name || "")
                            color: Theme.textPrimary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.description || ""
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            visible: text.length > 0
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingMd
                        anchors.verticalCenter: parent.verticalCenter
                        text: watching.isWatched(detail.type, detail.imdbId,
                                                 modelData.season,
                                                 modelData.episode)
                              ? "\u2713" : "\u25B6"
                        color: Theme.accent
                        font.pixelSize: 14
                    }
                }
            }

            Text {
                visible: meta.lastError.length > 0
                text: meta.lastError
                color: "#e57373"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                width: parent.width
            }
        }
    }
}
