import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: welcome

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingMd
        width: Math.min(420, parent.width - 48)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Nuvio")
            color: Theme.textPrimary
            font.pixelSize: 44
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Sign in to sync your library and progress")
            color: Theme.textSecondary
            font.pixelSize: 14
        }

        TextField {
            id: emailField
            width: parent.width
            placeholderText: qsTr("Email")
            echoMode: TextInput.Normal
            selectByMouse: true
        }
        TextField {
            id: passField
            width: parent.width
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            selectByMouse: true
            onAccepted: signInBtn.clicked()
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingSm

            Button {
                id: signInBtn
                text: qsTr("Sign In")
                onClicked: auth.signIn(emailField.text, passField.text)
            }
            Button {
                text: qsTr("Create Account")
                onClicked: auth.signUp(emailField.text, passField.text)
            }
        }

        Label {
            id: statusLabel
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: failed ? Theme.textSecondary : Theme.textSecondary
            property bool failed: false

            Connections {
                target: auth
                function onAuthResult(ok, error) {
                    statusLabel.failed = !ok
                    if (!ok) {
                        statusLabel.text = error
                    } else if (error.length > 0) {
                        statusLabel.text = error   // e.g. confirm-your-mail
                    } else {
                        statusLabel.text = qsTr("Signed in")
                    }
                }
                function onStateChanged() {
                    // success lands the user back home automatically
                    if (auth.sessionActive &&
                            navigation.currentRoute === "welcome")
                        navigation.replaceTop("home")
                }
            }
        }
    }
}
