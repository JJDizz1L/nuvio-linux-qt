import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: home

    // ---- idle brand mark (until media launches from CLI/hooks) -------------
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingMd

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Nuvio")
            color: Theme.textPrimary
            font.pixelSize: 44
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Linux — exploration build")
            color: Theme.textSecondary
            font.pixelSize: 15
        }
    }

    // ---- navigation skeleton entry (Phase 3 demo wiring) -------------------
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 90
        spacing: Theme.spacingSm

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Browse Library")
            onClicked: navigation.pushIfDifferent("library")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Settings")
            onClicked: navigation.pushIfDifferent("settings")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Add-ons")
            onClicked: navigation.pushIfDifferent("addons")
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("usage: nuvio-linux-qt <file|url>   ·   F11 fullscreen")
            color: Theme.textDisabled
            font.pixelSize: 12
        }
    }
}
