import QtQuick
import QtQuick.Controls
import "../theme"

// Profile selection + management (P7). PIN-gated profiles open an inline
// PIN sheet; verifyPin unlocks for the session, then switching succeeds.
// Create/rename/color/PIN-set/delete ride the manager (server when signed
// in, local otherwise). Signed-out users never land here (route guard in
// main.cpp); the page still degrades gracefully when auth drops.
Item {
    id: profilesPage

    property int pinFor: -1
    property string pinError: ""
    property int editing: -1
    property string opError: ""

    function displayName(p) {
        return (p.name || "").length > 0 ? p.name
               : qsTr("Profile %1").arg(p.index)
    }
    function initialOf(p) {
        const n = profilesPage.displayName(p)
        return n.length > 0 ? n[0].toUpperCase() : "?"
    }

    Connections {
        target: profiles
        function onPinResult(unlocked, message) {
            if (unlocked) {
                profiles.switchToProfile(profilesPage.pinFor)
                profilesPage.pinFor = -1
                profilesPage.pinError = ""
            } else {
                profilesPage.pinError = message
            }
        }
        function onOperationResult(ok, message) {
            profilesPage.opError = ok ? "" : message
        }
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Who's watching?")
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

            Text {
                x: Theme.spacingLg
                visible: !auth.sessionActive
                width: parent.width - 2 * Theme.spacingLg
                text: qsTr("Sign in to use named profiles; until then "
                           + "everything lives on the default profile.")
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Repeater {
                model: profiles.profiles
                delegate: Rectangle {
                    required property var modelData
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    height: editCol.height + 20
                    radius: Theme.radiusMd
                    color: modelData.index === profiles.activeProfileIndex
                           ? Theme.surfaceHigh : Theme.surface
                    border.width: modelData.index === profiles.activeProfileIndex ? 2 : 0
                    border.color: Theme.accent

                    Column {
                        id: editCol
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        spacing: 6

                        Row {
                            width: parent.width
                            spacing: Theme.spacingMd
                            Rectangle {
                                width: 44; height: 44
                                radius: 22
                                color: modelData.avatarColorHex || "#1E88E5"
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    anchors.centerIn: parent
                                    text: profilesPage.initialOf(modelData)
                                    color: "white"
                                    font.pixelSize: 20
                                    font.weight: Font.Bold
                                }
                            }
                            Text {
                                width: parent.width - 220
                                text: profilesPage.displayName(modelData)
                                      + (modelData.pinEnabled ? "  🔒" : "")
                                      + (modelData.locked ? qsTr(" (locked)") : "")
                                color: Theme.textPrimary
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Button {
                                text: modelData.index === profiles.activeProfileIndex
                                      ? qsTr("Active") : qsTr("Switch")
                                enabled: modelData.index !== profiles.activeProfileIndex
                                         && !modelData.locked
                                onClicked: {
                                    if (modelData.pinEnabled) {
                                        profilesPage.pinFor = modelData.index
                                        profilesPage.pinError = ""
                                        pinField.text = ""
                                        pinField.forceActiveFocus()
                                    } else {
                                        profiles.switchToProfile(modelData.index)
                                    }
                                }
                            }
                        }

                        // Inline PIN sheet for gated profiles.
                        Column {
                            visible: profilesPage.pinFor === modelData.index
                            width: parent.width
                            spacing: 4
                            Row {
                                width: parent.width
                                spacing: Theme.spacingSm
                                TextField {
                                    id: pinField
                                    width: 160
                                    echoMode: TextInput.Password
                                    placeholderText: qsTr("PIN")
                                    selectByMouse: true
                                    maximumLength: 32
                                    onAccepted: profiles.verifyPin(
                                        modelData.index, text)
                                }
                                Button {
                                    text: qsTr("Unlock")
                                    onClicked: profiles.verifyPin(
                                        modelData.index, pinField.text)
                                }
                                Button {
                                    text: qsTr("Cancel")
                                    flat: true
                                    onClicked: profilesPage.pinFor = -1
                                }
                            }
                            Text {
                                visible: profilesPage.pinError.length > 0
                                text: profilesPage.pinError
                                color: "#e57373"
                                font.pixelSize: 12
                            }
                        }

                        // Inline editor (rename / color / PIN / delete).
                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            Button {
                                text: profilesPage.editing === modelData.index
                                      ? qsTr("Done") : qsTr("Edit")
                                flat: true
                                onClicked: profilesPage.editing =
                                    profilesPage.editing === modelData.index
                                    ? -1 : modelData.index
                            }
                            Button {
                                text: qsTr("Delete")
                                flat: true
                                visible: profilesPage.editing === modelData.index
                                         && profiles.profiles.length > 1
                                onClicked: profiles.deleteProfile(modelData.index)
                            }
                        }
                        Column {
                            id: editorCol
                            property int profileIndex: modelData.index
                            visible: profilesPage.editing === modelData.index
                            width: parent.width
                            spacing: 4
                            Row {
                                width: parent.width
                                spacing: Theme.spacingSm
                                TextField {
                                    id: renameField
                                    width: 200
                                    text: modelData.name
                                    selectByMouse: true
                                    placeholderText: qsTr("Display name")
                                }
                                Button {
                                    text: qsTr("Rename")
                                    flat: true
                                    onClicked: profiles.renameProfile(
                                        modelData.index, renameField.text)
                                }
                            }
                            Row {
                                width: parent.width
                                spacing: Theme.spacingSm
                                Text {
                                    text: qsTr("Color")
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Repeater {
                                    model: ["#1E88E5", "#8E24AA", "#00897B",
                                            "#F4511E", "#6D4C41", "#3949AB",
                                            "#039BE5", "#7CB342"]
                                    delegate: Rectangle {
                                        required property string modelData
                                        width: 28; height: 28
                                        radius: 14
                                        color: modelData
                                        border.width: 2
                                        border.color: "white"
                                        opacity: 0.9
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: profiles.setProfileColor(
                                                editorCol.profileIndex,
                                                modelData)
                                        }
                                    }
                                }
                            }
                            Row {
                                width: parent.width
                                spacing: Theme.spacingSm
                                TextField {
                                    id: newPinField
                                    width: 140
                                    echoMode: TextInput.Password
                                    placeholderText: modelData.pinEnabled
                                        ? qsTr("New PIN (sets)")
                                        : qsTr("Set PIN")
                                    selectByMouse: true
                                    maximumLength: 32
                                }
                                TextField {
                                    id: curPinField
                                    width: 140
                                    echoMode: TextInput.Password
                                    placeholderText: qsTr("Current PIN")
                                    selectByMouse: true
                                    maximumLength: 32
                                }
                                Button {
                                    text: qsTr("Save PIN")
                                    flat: true
                                    onClicked: profiles.setPin(
                                        modelData.index, newPinField.text,
                                        curPinField.text)
                                }
                                Button {
                                    visible: modelData.pinEnabled
                                    text: qsTr("Clear PIN")
                                    flat: true
                                    onClicked: profiles.clearPin(
                                        modelData.index, curPinField.text)
                                }
                            }
                        }
                    }
                }
            }

            Row {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: Theme.spacingSm
                TextField {
                    id: createField
                    width: 220
                    placeholderText: qsTr("New profile name")
                    selectByMouse: true
                    onAccepted: createBtn.clicked()
                }
                Button {
                    id: createBtn
                    text: qsTr("Create")
                    enabled: profiles.profiles.length < 6
                    onClicked: {
                        if (createField.text.trim().length === 0) return
                        profiles.createProfile(createField.text, "")
                        createField.text = ""
                    }
                }
            }
            Text {
                x: Theme.spacingLg
                visible: profilesPage.opError.length > 0
                text: profilesPage.opError
                color: "#e57373"
                font.pixelSize: 12
            }
            Text {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                text: qsTr("Up to 6 profiles share this device; watch state, "
                           + "library, addons and settings follow the active "
                           + "one. PINs verify online when signed in, else "
                           + "against this device's cache.")
                color: Theme.textDisabled
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
    }
}
