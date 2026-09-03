import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Playback: decoder, cache, hold-to-speed, external player,
// link reuse, libass, and synced-but-inert compatibility flags.
Item {
    id: playbackPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Playback")
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

    ScrollView {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            anchors.margins: Theme.spacingLg

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("Hardware decoding")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    // Compose-parity contract exposes exactly three states
                    // (device / prefer-device share one hwdec chain).
                    model: ["auto", "software"]
                    currentIndex: ["auto", "software"]
                        .indexOf(appsettings.decoderMode)
                    onActivated: function(i) {
                        appsettings.decoderMode = model[i]
                        applier.applyAll()
                    }
                }
                Text {
                    text: qsTr("auto lets mpv pick per GPU vendor; software is the broken-GL escape hatch")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Row {
                    width: parent.width
                    Text {
                        width: parent.width * 0.7
                        text: qsTr("Streaming cache")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                    }
                    Label {
                        text: appsettings.cacheMb + " MB"
                        color: Theme.textSecondary
                    }
                }
                Slider {
                    width: parent.width * 0.8
                    from: 64; to: 2048; stepSize: 64
                    value: appsettings.cacheMb
                    onMoved: appsettings.cacheMb = value
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        id: holdSwitch
                        checked: appsettings.holdToSpeedEnabled
                        onToggled: appsettings.holdToSpeedEnabled = checked
                    }
                    Text {
                        text: qsTr("Hold to speed up")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    visible: holdSwitch.checked
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Speed")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Slider {
                        width: parent.width * 0.4
                        from: 0.5; to: 4.0; stepSize: 0.25
                        value: appsettings.holdToSpeedValue
                        onMoved: appsettings.holdToSpeedValue = value
                    }
                    Label {
                        text: appsettings.holdToSpeedValue.toFixed(2) + "×"
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Gesture handling lands with the player work "
                               + "(P3); the preference already syncs.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        id: extSwitch
                        checked: appsettings.externalPlayerEnabled
                        onToggled: appsettings.externalPlayerEnabled = checked
                    }
                    Text {
                        text: qsTr("External player")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Column {
                    visible: extSwitch.checked
                    width: parent.width
                    spacing: 4
                    TextField {
                        width: parent.width * 0.6
                        placeholderText: qsTr("Player id (empty = system)")
                        text: appsettings.externalPlayerId === "system"
                              ? "" : appsettings.externalPlayerId
                        selectByMouse: true
                        onEditingFinished:
                            appsettings.externalPlayerId = text
                    }
                    Row {
                        width: parent.width
                        spacing: Theme.spacingSm
                        Switch {
                            checked: appsettings.externalPlayerForwardSubtitles
                            onToggled: appsettings.externalPlayerForwardSubtitles = checked
                        }
                        Text {
                            text: qsTr("Forward subtitles")
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: Theme.spacingSm
                        Switch {
                            checked: appsettings.externalPlayerSendSkipSegments
                            onToggled: appsettings.externalPlayerSendSkipSegments = checked
                        }
                        Text {
                            text: qsTr("Send skip segments")
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                Text {
                    text: qsTr("Launcher lands with the player work (P3); "
                               + "preferences already persist and sync.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        id: reuseSwitch
                        checked: appsettings.streamReuseLastLinkEnabled
                        onToggled: appsettings.streamReuseLastLinkEnabled = checked
                    }
                    Text {
                        text: qsTr("Reuse last stream link")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    visible: reuseSwitch.checked
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Cache hours")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Slider {
                        width: parent.width * 0.4
                        from: 0; to: 72; stepSize: 1
                        value: appsettings.streamReuseLastLinkCacheHours
                        onMoved: appsettings.streamReuseLastLinkCacheHours = value
                    }
                    Label {
                        text: appsettings.streamReuseLastLinkCacheHours + " h"
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.useLibass
                        onToggled: appsettings.useLibass = checked
                    }
                    Text {
                        text: qsTr("libass subtitles")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Render type")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: parent.width * 0.5
                        text: appsettings.libassRenderType
                        selectByMouse: true
                        onEditingFinished:
                            appsettings.libassRenderType = text
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("Compatibility flags")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Text {
                    text: qsTr("Synced for profile parity; no local effect on "
                               + "this line yet.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.mapDv7ToHevc
                        onToggled: appsettings.mapDv7ToHevc = checked
                    }
                    Text {
                        text: qsTr("Map Dolby Vision 7 to HEVC")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.tunnelingEnabled
                        onToggled: appsettings.tunnelingEnabled = checked
                    }
                    Text {
                        text: qsTr("Tunneled playback")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.touchGesturesEnabled
                        onToggled: appsettings.touchGesturesEnabled = checked
                    }
                    Text {
                        text: qsTr("Touch gestures")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.nvidiaRtxSuperResolutionEnabled
                        onToggled: appsettings.nvidiaRtxSuperResolutionEnabled = checked
                    }
                    Text {
                        text: qsTr("NVIDIA RTX super resolution")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
