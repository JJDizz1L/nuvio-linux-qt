import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: addonsPage

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
            text: qsTr("Add-ons")
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

    // ---- install row ---------------------------------------------------------
    Column {
        id: installRow
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingSm

        Row {
            width: parent.width
            spacing: Theme.spacingSm

            TextField {
                id: urlField
                width: parent.width - installBtn.width - parent.spacing
                placeholderText:
                    qsTr("Stremio addon manifest URL or domain")
                selectByMouse: true
                onAccepted: installBtn.clicked()
            }
            Button {
                id: installBtn
                text: qsTr("Install")
                onClicked: addons.add(urlField.text)
            }
        }
        Label {
            id: statusLabel
            width: parent.width
            wrapMode: Text.Wrap
            color: failed ? "#ff9a9a" : Theme.textSecondary
            property bool failed: false

            Connections {
                target: addons
                function onAddResult(ok, message) {
                    statusLabel.failed = !ok
                    statusLabel.text = message.length > 0 ? message
                                                          : (ok ? "Installed"
                                                                : "Failed")
                    if (ok && errorText.visible === false)
                        urlField.text = ""
                }
            }
        }
    }

    // ---- installed list ------------------------------------------------------
    ListView {
        anchors.top: installRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        anchors.topMargin: Theme.spacingMd
        clip: true
        spacing: Theme.spacingSm
        model: addons.addons

        Text {
            anchors.centerIn: parent
            visible: addons.addons.length === 0
            text: qsTr("No add-ons installed yet.\n"
                       + "Paste a manifest URL above, e.g.\n"
                       + "v3-cinemeta.strem.io")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textDisabled
        }

        delegate: Rectangle {
            required property var modelData
            required property int index
            width: ListView.view.width
            height: 64
            color: Theme.surface
            radius: 6

            Column {
                anchors.verticalCenter: parent.verticalCenter
                x: 12
                spacing: 2
                opacity: modelData.enabled === false ? 0.45 : 1
                Text {
                    text: modelData.name
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    text: modelData.url
                    elide: Text.ElideMiddle
                    width: 560
                    color: Theme.textDisabled
                    font.pixelSize: 11
                }
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 12
                spacing: 8
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Switch {
                        anchors.horizontalCenter: parent.horizontalCenter
                        checked: modelData.enabled !== false
                        onToggled: addons.setEnabled(index, checked)
                    }
                }
                Button {
                    anchors.verticalCenter: parent.verticalCenter
                    flat: true
                    text: qsTr("Remove")
                    onClicked: addons.remove(modelData.id)
                }
            }
        }
    }
}
