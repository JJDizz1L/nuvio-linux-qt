import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: settingsPage

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
            text: qsTr("Settings")
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

    Column {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // ---- appearance ------------------------------------------------------
        Row {
            width: parent.width
            Text {
                width: parent.width * 0.6
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Dark theme")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            Switch {
                checked: appsettings.darkTheme
                onClicked: appsettings.darkTheme = !appsettings.darkTheme
            }
        }

        // ---- playback: decoder -------------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Hardware decoding")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            ComboBox {
                id: decoderBox
                width: parent.width * 0.6
                model: ["auto", "vaapi", "nvdec", "software"]
                currentIndex: ["auto","vaapi","nvdec","software"]
                    .indexOf(appsettings.decoderMode)
                onActivated: function(i) {
                    appsettings.decoderMode = model[i]
                    applier.applyAll()      // live push into the running core
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

        // ---- playback: cache ----------------------------------------------------
        Column {
            width: parent.width
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
                onMoved: appsettings.cacheMb = value   // persists while dragging
            }
        }

        Text {
            text: qsTr("Values persist to the shared Nuvio profile "
                       + "(~/.config/nuvio-linux).")
            color: Theme.textDisabled
            font.pixelSize: 11
        }
    }
}
