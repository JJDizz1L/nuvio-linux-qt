import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Notifications: episode-release alerts for saved shows
// (fork NotificationsSettingsPage parity). Unlike fork-desktop - where
// the page hides behind AppFeaturePolicy and the backend is a stub -
// this line ships a notify-send backend, so the leaf is visible:
// enabling probes for notify-send, refresh fires due releases once,
// future releases count as scheduled.
Item {
    id: notificationsPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Notifications")
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
                    text: qsTr("ALERTS")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: notifications.enabled
                        enabled: !notifications.loading
                        onToggled: notifications.setEnabled(checked)
                    }
                    Text {
                        width: parent.width - 80
                        wrapMode: Text.Wrap
                        text: qsTr("Episode release alerts")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Schedule local notifications when a new episode for a saved show becomes available.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 8
                Text {
                    text: qsTr("TEST")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: notifications.testTargetTitle !== ""
                          ? qsTr("Send a local test notification for %1.")
                              .arg(notifications.testTargetTitle)
                          : qsTr("Save a show to your library first to test notifications.")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                }
                Text {
                    width: parent.width
                    text: notifications.enabled
                          ? qsTr("%1 release alerts are currently scheduled on this device.")
                              .arg(notifications.scheduledCount)
                          : qsTr("Notifications are currently disabled in Nuvio.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Button {
                    width: parent.width
                    text: notifications.loading
                          ? qsTr("Sending Test Notification…")
                          : qsTr("Send Test Notification")
                    enabled: !notifications.loading
                             && notifications.testTargetTitle !== ""
                    onClicked: notifications.sendTestNotification()
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: notifications.statusMessage !== ""
                    text: notifications.statusMessage
                    color: Theme.accent
                    font.pixelSize: 13
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: notifications.errorMessage !== ""
                    text: notifications.errorMessage
                    color: "#e57373"
                    font.pixelSize: 13
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: !notifications.permissionGranted
                    text: qsTr("System notifications are disabled for Nuvio. Enable them to receive alerts and test notifications.")
                    color: "#e57373"
                    font.pixelSize: 13
                }
            }
        }
    }
}
