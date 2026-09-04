import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Debrid (D3): provider accounts (API key + device flows),
// resolver preferences, stream templates.
Item {
    id: debridPage

    property var providerMeta: [
        { id: "torbox", label: qsTr("Torbox") },
        { id: "premiumize", label: qsTr("Premiumize") },
        { id: "realdebrid", label: qsTr("Real-Debrid") }
    ]
    function isAuthed(pid) {
        if (pid === "torbox") return debridauth.torboxAuthorized
        if (pid === "premiumize") return debridauth.premiumizeAuthorized
        return debridauth.realDebridAuthorized
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
            text: qsTr("Debrid")
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
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                Switch {
                    checked: debrid.enabled
                    onToggled: debrid.enabled = checked
                }
                Text {
                    text: qsTr("Debrid services")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                Switch {
                    checked: debrid.cloudLibraryEnabled
                    onToggled: debrid.cloudLibraryEnabled = checked
                }
                Text {
                    text: qsTr("Cloud library")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Repeater {
                model: debridPage.providerMeta
                delegate: Rectangle {
                    required property var modelData
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    height: provCol.height + 20
                    radius: Theme.radiusMd
                    color: Theme.surface

                    Column {
                        id: provCol
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        spacing: 6

                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            Text {
                                width: parent.width - 220
                                text: modelData.label
                                color: Theme.textPrimary
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: debridPage.isAuthed(modelData.id)
                                      ? qsTr("Connected") : qsTr("Not connected")
                                color: debridPage.isAuthed(modelData.id)
                                       ? Theme.accent : Theme.textSecondary
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            TextField {
                                id: keyField
                                width: 240
                                echoMode: TextInput.Password
                                placeholderText: qsTr("API key")
                                selectByMouse: true
                            }
                            Button {
                                text: qsTr("Save key")
                                flat: true
                                onClicked: {
                                    debridauth.authorizeWithApiKey(
                                        modelData.id, keyField.text)
                                    keyField.text = ""
                                }
                            }
                            Button {
                                visible: modelData.id !== "realdebrid"
                                text: qsTr("Device code")
                                flat: true
                                onClicked: debridauth.startDeviceFlow(
                                    modelData.id)
                            }
                            Button {
                                visible: debridPage.isAuthed(modelData.id)
                                text: qsTr("Sign out")
                                flat: true
                                onClicked: debridauth.signOut(modelData.id)
                            }
                        }
                        Column {
                            visible: debridauth.deviceFlowActive
                                     && debridauth.flowProviderId === modelData.id
                            width: parent.width
                            spacing: 4
                            Text {
                                text: qsTr("Enter this code at %1").arg(
                                    debridauth.deviceVerificationUrl)
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                width: parent.width
                            }
                            Text {
                                text: debridauth.deviceUserCode
                                color: Theme.accent
                                font.pixelSize: 26
                                font.weight: Font.Bold
                                font.letterSpacing: 3
                            }
                            Row {
                                spacing: Theme.spacingSm
                                Button {
                                    visible: debridauth.deviceVerificationUrl.length > 0
                                    text: qsTr("Open page")
                                    flat: true
                                    onClicked: Qt.openUrlExternally(
                                        (debridauth.deviceVerificationUrl.startsWith("http")
                                         ? "" : "https://")
                                        + debridauth.deviceVerificationUrl)
                                }
                                Button {
                                    text: qsTr("Cancel")
                                    flat: true
                                    onClicked: debridauth.cancelDeviceFlow()
                                }
                            }
                            Text {
                                visible: debridauth.busy
                                text: qsTr("Waiting for approval…")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                        Text {
                            visible: debridauth.errorMessage.length > 0
                                     && debridauth.deviceFlowActive
                                     && debridauth.flowProviderId === modelData.id
                            width: parent.width
                            text: debridauth.errorMessage
                            color: "#e57373"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("Preferred resolver")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [{ label: qsTr("Automatic"), value: "" }].concat(
                        debrid.configuredProviderIds().map(function(id) {
                            return { label: id, value: id }
                        }))
                    currentIndex: {
                        const vals = model.map(function(m){ return m.value })
                        const i = vals.indexOf(
                            debrid.preferredResolverProviderId)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        debrid.preferredResolverProviderId = model[i].value
                    }
                }
                Text {
                    text: qsTr("Automatic picks the first configured Torbox "
                               + "or Premiumize key. Real-Debrid never "
                               + "auto-resolves.")
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
                    text: qsTr("Stream presentation")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: 160
                        text: qsTr("Sort")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 220
                        model: ["DEFAULT", "QUALITY_DESC", "SIZE_DESC",
                                "SIZE_ASC"]
                        currentIndex: model.indexOf(debrid.streamSortMode)
                        onActivated: function(i) {
                            debrid.streamSortMode = model[i]
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: 160
                        text: qsTr("Minimum quality")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 220
                        model: ["ANY", "P720", "P1080", "P2160"]
                        currentIndex: model.indexOf(
                            debrid.streamMinimumQuality)
                        onActivated: function(i) {
                            debrid.streamMinimumQuality = model[i]
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: 160
                        text: qsTr("Dolby Vision")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 220
                        model: ["ANY", "EXCLUDE", "ONLY"]
                        currentIndex: model.indexOf(
                            debrid.streamDolbyVisionFilter)
                        onActivated: function(i) {
                            debrid.streamDolbyVisionFilter = model[i]
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: 160
                        text: qsTr("HDR")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 220
                        model: ["ANY", "EXCLUDE", "ONLY"]
                        currentIndex: model.indexOf(debrid.streamHdrFilter)
                        onActivated: function(i) {
                            debrid.streamHdrFilter = model[i]
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: 160
                        text: qsTr("Codec")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 220
                        model: ["ANY", "H264", "HEVC", "AV1"]
                        currentIndex: model.indexOf(debrid.streamCodecFilter)
                        onActivated: function(i) {
                            debrid.streamCodecFilter = model[i]
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: 160
                        text: qsTr("Max results (0 = all)")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Slider {
                        width: 220
                        from: 0; to: 50; stepSize: 1
                        value: debrid.streamMaxResults
                        onMoved: debrid.streamMaxResults = Math.round(value)
                    }
                    Label {
                        text: debrid.streamMaxResults
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Name template")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                }
                TextField {
                    width: parent.width
                    text: debrid.streamNameTemplate
                    selectByMouse: true
                    onEditingFinished: debrid.streamNameTemplate = text
                }
                Text {
                    text: qsTr("Description template")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                }
                TextField {
                    width: parent.width
                    text: debrid.streamDescriptionTemplate
                    selectByMouse: true
                    placeholderText: qsTr("(empty)")
                    onEditingFinished:
                        debrid.streamDescriptionTemplate = text
                }
                Text {
                    width: parent.width
                    text: qsTr("Templates render stream metadata; unknown "
                               + "fields stay empty. Sort/filter shaping of "
                               + "the source list arrives with the metadata "
                               + "backfill.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
