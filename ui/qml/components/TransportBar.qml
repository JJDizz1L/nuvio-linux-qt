import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: bar

    required property var mpv

    // Track lists derived from the mpv track-list (kind-filtered).
    readonly property var audioTracks: {
        const out = []
        for (const t of (mpv ? mpv.tracks : []))
            if (t.kind === "audio") out.push(t)
        return out
    }
    readonly property var subTracks: {
        const out = []
        for (const t of (mpv ? mpv.tracks : []))
            if (t.kind === "sub") out.push(t)
        return out
    }
    function trackLabel(t) {
        // "Title (lang)" | "Title" | "lang" | "Track N"
        if (t.title && t.lang) return t.title + " (" + t.lang + ")"
        if (t.title) return t.title
        if (t.lang) return t.lang
        return qsTr("Track %1").arg(t.id)
    }

    color: Theme.chromeScrim
    radius: Theme.radiusMd
    border.color: Theme.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        Button {
            flat: true
            enabled: bar.mpv.hasMedia
            contentItem: Text {
                text: bar.mpv.paused ? qsTr("Play") : qsTr("Pause")   // ▶ ❚❚
                color: enabled ? Theme.textPrimary : Theme.textDisabled
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: bar.mpv.togglePlayPause()
        }

        Label {
            text: bar.mpv.formatTime(bar.mpv.positionMs / 1000.0)
            color: Theme.textSecondary
            font.pixelSize: 12
        }

        Slider {
            id: seekSlider
            Layout.fillWidth: true
            from: 0
            to: Math.max(bar.mpv.durationMs, 1)
            value: bar.mpv.positionMs < 0 ? 0 : bar.mpv.positionMs
            enabled: bar.mpv.hasMedia && bar.mpv.durationMs > 0

            // don't fight live updates while the user drags
            property bool dragging: false
            onPressedChanged: {
                dragging = pressed
                if (!pressed) bar.mpv.seekToSeconds(value / 1000.0)
            }
            onMoved: if (dragging) value = value     // keeps visual position

            // custom track: buffered range behind the played fill
            background: Rectangle {
                x: seekSlider.leftPadding
                y: seekSlider.topPadding
                   + seekSlider.availableHeight / 2 - height / 2
                width: seekSlider.availableWidth
                height: 4
                radius: 2
                color: Theme.surfaceHigh

                Rectangle {   // buffered (demuxer cache ahead of playhead)
                    visible: bar.mpv.hasMedia && bar.mpv.cacheSeconds > 0
                    width: parent.width * Math.min(
                               1,
                               bar.mpv.cacheSeconds
                               / Math.max(bar.mpv.durationMs / 1000.0, 1))
                    height: parent.height
                    radius: 2
                    x: seekSlider.visualPosition * parent.width
                    color: Theme.textDisabled
                    opacity: 0.45
                }
                Rectangle {   // played range
                    width: seekSlider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
        }

        Label {
            text: bar.mpv.formatTime(bar.mpv.durationMs / 1000.0)
            color: Theme.textSecondary
            font.pixelSize: 12
        }

        // ---- track pickers (audio / subtitle) over the mpv track list ------
        Button {
            flat: true
            enabled: bar.mpv.hasMedia && bar.audioTracks.length > 0
            contentItem: Text {
                text: qsTr("Audio")
                color: enabled ? Theme.textPrimary : Theme.textDisabled
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: audioMenu.popup()

            Menu {
                id: audioMenu
                Repeater {
                    model: {
                        // "Off" + audio tracks (mpv resolves id -> aid).
                        const list = [{ label: qsTr("Off"), id: 0,
                                        checked: false }]
                        for (const t of bar.audioTracks)
                            list.push({ label: bar.trackLabel(t),
                                        id: t.id, checked: t.selected })
                        return list
                    }
                    delegate: MenuItem {
                        required property var modelData
                        text: (modelData.checked ? "\u2713 " : "   ")
                              + modelData.label
                        onTriggered: bar.mpv.setTrack("audio",
                                                      modelData.id)
                    }
                }
            }
        }

        Button {
            flat: true
            enabled: bar.mpv.hasMedia
            contentItem: Text {
                text: qsTr("Subs")
                color: enabled ? Theme.textPrimary : Theme.textDisabled
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: subMenu.popup()

            Menu {
                id: subMenu
                Repeater {
                    model: {
                        const list = [{ label: qsTr("Off"), id: 0,
                                        checked: false }]
                        for (const t of bar.subTracks)
                            list.push({ label: bar.trackLabel(t),
                                        id: t.id, checked: t.selected })
                        return list
                    }
                    delegate: MenuItem {
                        required property var modelData
                        text: (modelData.checked ? "\u2713 " : "   ")
                              + modelData.label
                        onTriggered: bar.mpv.setTrack("sub", modelData.id)
                    }
                }
            }
        }

        Slider {
            id: volumeSlider
            Layout.preferredWidth: 96
            from: 0
            to: 130
            value: bar.mpv.volumePercent
            onMoved: bar.mpv.volumePercent = Math.round(value)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Volume %1%").arg(Math.round(value))
        }

        Button {
            flat: true
            contentItem: Text {
                text: qsTr("Full")                               // ⛶ fullscreen
                color: Theme.textPrimary
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: applicationWindow.toggleFullscreen()
        }
    }

    // thin buffering stripe across the top while demuxer stalls
    Rectangle {
        visible: bar.mpv.buffering
        anchors.top: parent.top
        width: parent.width * 0.4
        height: 2
        x: ((bar.width - width) / 2)
        color: Theme.accent
        SequentialAnimation on x {
            running: bar.mpv.buffering
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: bar.width * 0.6; duration: 900;
                              easing.type: Easing.InOutQuad }
            NumberAnimation { from: bar.width * 0.6; to: 0; duration: 900;
                              easing.type: Easing.InOutQuad }
        }
    }
}
