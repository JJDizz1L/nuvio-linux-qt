import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Account: sign-in state for the shared profile (Supabase auth
// lives in AuthService; the Welcome route handles sign-in/up forms).
Item {
    id: accountPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Account")
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

        Text {
            visible: auth.sessionActive
            text: qsTr("Signed in as %1").arg(auth.userEmail)
            color: Theme.textPrimary
            font.pixelSize: 15
            wrapMode: Text.Wrap
            width: parent.width - 2 * Theme.spacingLg
            x: Theme.spacingLg
        }
        Text {
            visible: auth.sessionActive
            text: qsTr("Library, progress, addons and settings sync while "
                       + "signed in.")
            color: Theme.textSecondary
            font.pixelSize: 13
            wrapMode: Text.Wrap
            width: parent.width - 2 * Theme.spacingLg
            x: Theme.spacingLg
        }
        Button {
            visible: auth.sessionActive
            x: Theme.spacingLg
            text: qsTr("Sign out")
            onClicked: auth.signOut()
        }

        // Profile switcher (P7): quick-switch list + manager entry.
        Text {
            visible: auth.sessionActive
            x: Theme.spacingLg
            text: qsTr("Profile")
            color: Theme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Repeater {
            model: auth.sessionActive ? profiles.profiles : []
            delegate: Row {
                required property var modelData
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: Theme.spacingSm
                Rectangle {
                    width: 28; height: 28
                    radius: 14
                    color: modelData.avatarColorHex || "#1E88E5"
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.centerIn: parent
                        text: (modelData.name || "?")[0].toUpperCase()
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                }
                Text {
                    width: parent.width - 200
                    text: (modelData.name || qsTr("Profile %1").arg(modelData.index))
                          + (modelData.pinEnabled ? "  🔒" : "")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
                Button {
                    text: modelData.index === profiles.activeProfileIndex
                          ? qsTr("Active") : qsTr("Switch")
                    flat: true
                    enabled: modelData.index !== profiles.activeProfileIndex
                             && !modelData.locked
                    onClicked: {
                        if (modelData.pinEnabled)
                            navigation.push("profiles")
                        else
                            profiles.switchToProfile(modelData.index)
                    }
                }
            }
        }
        Button {
            visible: auth.sessionActive
            x: Theme.spacingLg
            text: qsTr("Manage profiles")
            flat: true
            onClicked: navigation.push("profiles")
        }
        Text {
            visible: !auth.sessionActive
            text: qsTr("Not signed in. Sync stays off until you sign in.")
            color: Theme.textPrimary
            font.pixelSize: 15
            x: Theme.spacingLg
        }
        Button {
            visible: !auth.sessionActive
            x: Theme.spacingLg
            text: qsTr("Go to sign in")
            onClicked: navigation.push("welcome")
        }
    }
}
