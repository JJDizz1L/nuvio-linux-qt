import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Appearance: theme, hover previews, playback chrome flags.
Item {
    id: appearancePage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Appearance")
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

            Row {
                width: parent.width - 2 * Theme.spacingLg
                x: Theme.spacingLg
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

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        id: hoverSwitch
                        checked: appsettings.hoverPreviewEnabled
                        onToggled: appsettings.hoverPreviewEnabled = checked
                    }
                    Text {
                        text: qsTr("Poster hover preview")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    visible: hoverSwitch.checked
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Hover delay")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Slider {
                        width: parent.width * 0.5
                        from: 500; to: 5000; stepSize: 250
                        value: appsettings.hoverPreviewDelayMs
                        onMoved: appsettings.hoverPreviewDelayMs = value
                    }
                    Label {
                        text: (appsettings.hoverPreviewDelayMs / 1000) + " s"
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Row {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                Switch {
                    checked: appsettings.showLoadingOverlay
                    onToggled: appsettings.showLoadingOverlay = checked
                }
                Text {
                    text: qsTr("Show loading overlay")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                Switch {
                    checked: appsettings.showParentalGuide
                    onToggled: appsettings.showParentalGuide = checked
                }
                Text {
                    text: qsTr("Show parental guide")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("Resize mode")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    model: ["Fit", "Fill", "Zoom", "Stretch"]
                    currentIndex: model.indexOf(appsettings.resizeMode)
                    onActivated: function(i) {
                        appsettings.resizeMode = model[i]
                    }
                }
                Text {
                    text: qsTr("Fit keeps aspect; Fill crops; Zoom scales up; "
                               + "Stretch distorts. Compose PlayerResizeMode "
                               + "names, shared profile key.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }
        }
    }
}
