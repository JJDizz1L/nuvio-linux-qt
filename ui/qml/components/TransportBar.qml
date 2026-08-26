import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: bar

    required property var mpv

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
                text: bar.mpv.paused ? "\u23F5" : "\u23F8"   // ▶ ❚❚
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
        }

        Label {
            text: bar.mpv.formatTime(bar.mpv.durationMs / 1000.0)
            color: Theme.textSecondary
            font.pixelSize: 12
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
                text: "\u26F6"                               // ⛶ fullscreen
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
