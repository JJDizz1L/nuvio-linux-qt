import QtQuick
import QtQuick.Controls
import "../theme"

// Downloads browser (A3): offline files from the shared profile store.
// Root shows Active / Movies / Shows sections (fork parity); a show row
// drills into per-season episode lists. Completed rows play through the
// normal session (prefer-local tier serves the file with resume + watch
// state); active rows pause/resume/retry. Deletes are immediate
// (CollectionsPage precedent - no confirm modal in this line).
Item {
    id: downloadsPage

    // Show drill-down state ("", "" = root sections).
    property string showId: ""
    property string showTitle: ""

    function isEpisode(r) {
        return r.seasonNumber >= 0 && r.episodeNumber >= 0
    }
    function displayTitle(r) {
        if (downloadsPage.isEpisode(r))
            return (r.episodeTitle || "").trim() || r.title
        return r.title
    }
    function episodeCode(r) {
        const s = r.seasonNumber, e = r.episodeNumber
        return "S" + (s < 10 ? "0" + s : s) + " E"
             + (e < 10 ? "0" + e : e)
    }
    function displaySubtitle(r) {
        if (!downloadsPage.isEpisode(r)) return ""
        const dt = downloadsPage.displayTitle(r)
        const bits = [downloadsPage.episodeCode(r)]
        const et = (r.episodeTitle || "").trim()
        if (et !== "" && et !== dt) bits.push(et)
        if (r.title !== "" && r.title !== dt) bits.push(r.title)
        return bits.join(" • ")
    }
    function formatBytes(bytes) {
        if (!(bytes > 0)) return qsTr("0 B")
        const kib = 1024.0, mib = kib * 1024.0, gib = mib * 1024.0
        const v = bytes * 1.0
        const trunc1 = x => Math.floor(x * 10) / 10
        if (v >= gib) return trunc1(v / gib) + qsTr(" GB")
        if (v >= mib) return trunc1(v / mib) + qsTr(" MB")
        if (v >= kib) return trunc1(v / kib) + qsTr(" KB")
        return bytes + qsTr(" B")
    }
    function statusText(r) {
        if (r.status === "Downloading" || r.status === "Paused") {
            const size = (r.totalBytes > 0)
                ? downloadsPage.formatBytes(r.downloadedBytes) + " / "
                  + downloadsPage.formatBytes(r.totalBytes)
                : downloadsPage.formatBytes(r.downloadedBytes)
            return (r.status === "Downloading" ? qsTr("Downloading — %1")
                                               : qsTr("Paused — %1")).arg(size)
        }
        if (r.status === "Completed")
            return qsTr("Completed — %1").arg(
                downloadsPage.formatBytes(
                    r.totalBytes > 0 ? r.totalBytes : r.downloadedBytes))
        return r.errorMessage || qsTr("Download failed")
    }
    function activeRows() {
        return downloads.items.filter(r => r.status !== "Completed")
    }
    function completedMovies() {
        return downloads.items.filter(
            r => r.status === "Completed" && !downloadsPage.isEpisode(r))
    }
    function completedShows() {
        const byShow = {}
        for (const r of downloads.items) {
            if (r.status !== "Completed" || !downloadsPage.isEpisode(r))
                continue
            if (!byShow[r.parentMetaId]) byShow[r.parentMetaId] = []
            byShow[r.parentMetaId].push(r)
        }
        return Object.keys(byShow).map(k => ({
            showId: k, title: byShow[k][0].title, count: byShow[k].length
        })).sort((a, b) => a.title.toLowerCase() < b.title.toLowerCase()
                                 ? -1 : 1)
    }
    function sortedEpisodes(rows) {
        return rows.slice().sort((a, b) => {
            const sa = a.seasonNumber, sb = b.seasonNumber
            if (sa !== sb) return sa - sb
            if (a.episodeNumber !== b.episodeNumber)
                return a.episodeNumber - b.episodeNumber
            const ta = (a.episodeTitle || "").trim().toLowerCase()
            const tb = (b.episodeTitle || "").trim().toLowerCase()
            if (ta !== tb) return ta < tb ? -1 : 1
            if (a.title !== b.title)
                return a.title.toLowerCase() < b.title.toLowerCase() ? -1 : 1
            return a.id < b.id ? -1 : (a.id > b.id ? 1 : 0)
        })
    }
    function showEpisodeRows() {
        return downloadsPage.sortedEpisodes(downloads.items.filter(
            r => r.status === "Completed" && downloadsPage.isEpisode(r)
                 && r.parentMetaId === downloadsPage.showId))
    }
    function seasonGroups(rows) {
        const bySeason = {}
        for (const r of rows) {
            const k = r.seasonNumber
            if (!bySeason[k]) bySeason[k] = []
            bySeason[k].push(r)
        }
        // Specials (season 0) first, then ascending (fork parity).
        return Object.keys(bySeason).map(k => ({
            season: parseInt(k, 10), rows: bySeason[k]
        })).sort((a, b) => (a.season === 0 ? -1 : a.season)
                            - (b.season === 0 ? -1 : b.season))
    }
    function playRow(r) {
        if (downloadsPage.isEpisode(r))
            playback.requestPlay(r.contentType, r.videoId,
                                 downloadsPage.displayTitle(r))
        else
            playback.requestPlay(r.contentType, r.parentMetaId, r.title)
    }
    function showToast(text) {
        dlPageToastText.text = text
        dlPageToast.opacity = 1
        dlPageToastTimer.restart()
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
            text: downloadsPage.showId === "" ? qsTr("Downloads")
                                              : downloadsPage.showTitle
            color: Theme.textPrimary
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            text: qsTr("Back")
            onClicked: {
                if (downloadsPage.showId !== "") {
                    downloadsPage.showId = ""
                    downloadsPage.showTitle = ""
                } else {
                    navigation.pop()
                }
            }
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 110
            text: qsTr("Open folder")
            flat: true
            visible: downloadsPage.showId === ""
            onClicked: {
                if (!downloads.openDownloadsDirectory())
                    downloadsPage.showToast(
                        qsTr("Couldn't open the downloads folder"))
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.topMargin: header.height
        clip: true

        Column {
            width: parent.width
            spacing: 6

            // ---- root sections -------------------------------------------
            Column {
                width: parent.width
                spacing: 6
                visible: downloadsPage.showId === ""

                Text {
                    x: Theme.spacingLg
                    visible: downloadsPage.activeRows().length > 0
                    text: qsTr("Active")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: downloadsPage.activeRows()
                    delegate: downloadRow
                }
                Text {
                    x: Theme.spacingLg
                    visible: downloadsPage.completedMovies().length > 0
                    text: qsTr("Movies")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: downloadsPage.completedMovies()
                    delegate: downloadRow
                }
                Text {
                    x: Theme.spacingLg
                    visible: downloadsPage.completedShows().length > 0
                    text: qsTr("Shows")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: downloadsPage.completedShows()
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width - Theme.spacingLg * 2
                        x: Theme.spacingLg
                        height: 64
                        radius: Theme.radiusMd
                        color: Theme.surface
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            spacing: Theme.spacingSm
                            Column {
                                width: parent.width - 80
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: modelData.title
                                    color: Theme.textPrimary
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: qsTr("%n episode(s)", "",
                                                modelData.count)
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                }
                            }
                            Button {
                                text: qsTr("Open")
                                flat: true
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: {
                                    downloadsPage.showId = modelData.showId
                                    downloadsPage.showTitle = modelData.title
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                downloadsPage.showId = modelData.showId
                                downloadsPage.showTitle = modelData.title
                            }
                        }
                    }
                }
                Text {
                    x: Theme.spacingLg
                    visible: downloads.items.length === 0
                    text: qsTr("No downloads yet — use Download in the player.")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
            }

            // ---- show drill-down ------------------------------------------
            Column {
                width: parent.width
                spacing: 6
                visible: downloadsPage.showId !== ""
                Repeater {
                    model: downloadsPage.seasonGroups(
                        downloadsPage.showEpisodeRows())
                    delegate: Column {
                        required property var modelData
                        width: parent.width
                        spacing: 6
                        Text {
                            x: Theme.spacingLg
                            text: modelData.season === 0
                                  ? qsTr("Specials")
                                  : qsTr("Season %1").arg(modelData.season)
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: modelData.rows
                            delegate: downloadRow
                        }
                    }
                }
                Text {
                    x: Theme.spacingLg
                    visible: downloadsPage.showEpisodeRows().length === 0
                    text: qsTr("No episodes downloaded.")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
            }
        }
    }

    // ---- shared download row ----------------------------------------------
    Component {
        id: downloadRow
        Rectangle {
            required property var modelData
            width: parent.width - Theme.spacingLg * 2
            x: Theme.spacingLg
            height: modelData.status === "Downloading" ? 108 : 84
            radius: Theme.radiusMd
            color: Theme.surface
            Column {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Column {
                        width: parent.width - actionsRow.width - 8
                        spacing: 2
                        Text {
                            width: parent.width
                            text: downloadsPage.displayTitle(modelData)
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: downloadsPage.displaySubtitle(
                                modelData) !== ""
                            text: downloadsPage.displaySubtitle(modelData)
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: downloadsPage.statusText(modelData)
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: actionsRow
                        spacing: 0
                        Button {
                            text: qsTr("Pause")
                            flat: true
                            visible: modelData.status === "Downloading"
                            onClicked: downloads.pauseDownload(modelData.id)
                        }
                        Button {
                            text: qsTr("Resume")
                            flat: true
                            visible: modelData.status === "Paused"
                            onClicked: downloads.resumeDownload(modelData.id)
                        }
                        Button {
                            text: qsTr("Retry")
                            flat: true
                            visible: modelData.status === "Failed"
                            onClicked: downloads.resumeDownload(modelData.id)
                        }
                        Button {
                            text: qsTr("Play")
                            flat: true
                            visible: modelData.status === "Completed"
                            onClicked: downloadsPage.playRow(modelData)
                        }
                        Button {
                            text: qsTr("Delete")
                            flat: true
                            onClicked: downloads.cancelDownload(modelData.id)
                        }
                    }
                }
                ProgressBar {
                    width: parent.width
                    visible: modelData.status === "Downloading"
                    from: 0
                    to: modelData.totalBytes > 0 ? modelData.totalBytes : 1
                    value: modelData.totalBytes > 0
                           ? Math.min(modelData.downloadedBytes,
                                      modelData.totalBytes)
                           : 0
                    indeterminate: !(modelData.totalBytes > 0)
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: modelData.status === "Completed"
                cursorShape: Qt.PointingHandCursor
                onClicked: downloadsPage.playRow(modelData)
            }
        }
    }

    Rectangle {
        id: dlPageToast
        opacity: 0
        z: 40
        radius: Theme.radiusMd
        color: Theme.chromeScrim
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 42
        width: dlPageToastText.width + 32
        height: dlPageToastText.height + 18
        Text {
            id: dlPageToastText
            anchors.centerIn: parent
            color: Theme.textPrimary
            font.pixelSize: 13
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.fadeMs }
        }
        Timer {
            id: dlPageToastTimer
            interval: 2600
            onTriggered: dlPageToast.opacity = 0
        }
    }
}
