import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Integrations: Discord presence, torrent engine cache.
Item {
    id: integrationsPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Integrations")
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
                    text: qsTr("Discord Rich Presence")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Switch {
                    checked: appsettings.discordEnabled
                    onClicked: appsettings.discordEnabled = !appsettings.discordEnabled
                }
                Text {
                    text: qsTr("Show what you're watching on your profile. "
                               + "Connects/disconnects live.")
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
                Text {
                    text: qsTr("Torrent cache size")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [
                        { label: qsTr("Off (64 MB floor)"), value: "NONE" },
                        { label: qsTr("2 GB"),              value: "GB_2" },
                        { label: qsTr("5 GB"),              value: "GB_5" },
                        { label: qsTr("10 GB"),             value: "GB_10" }
                    ]
                    currentIndex: {
                        const vals = ["NONE", "GB_2", "GB_5", "GB_10"]
                        const i = vals.indexOf(appsettings.torrentCacheSize)
                        return i >= 0 ? i : 1
                    }
                    onActivated: function(i) {
                        appsettings.torrentCacheSize = model[i].value
                    }
                }
                Text {
                    text: qsTr("RAM piece cache pushed to TorrServer before each "
                               + "torrent starts; shares the Compose-line profile key.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }
        }
    }
}
