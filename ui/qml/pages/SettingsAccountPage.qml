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
                       + "signed in. Profile switching lands with P7.")
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
