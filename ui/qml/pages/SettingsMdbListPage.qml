import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > MDBList: ratings master switch, personal api key, and the
// 8 provider gates (fork MdbListSettingsPage parity). The key is a
// credential: stored locally, stripped from the sync blob, traveling
// only through the provider-credentials family.
Item {
    id: mdbPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("MDBList")
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
                    text: qsTr("MDBLIST RATINGS")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: mdblist.enabled
                        enabled: mdblist.hasApiKey
                        onToggled: mdblist.setEnabled(checked)
                    }
                    Text {
                        width: parent.width - 80
                        wrapMode: Text.Wrap
                        text: qsTr("Enable MDBList Ratings")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: !mdblist.hasApiKey
                    text: qsTr("Add your MDBList API key below before turning ratings on.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Fetch ratings from external providers in metadata detail screen")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 8
                Text {
                    text: qsTr("API KEY")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("API Key")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Required to fetch ratings from MDBList")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    TextField {
                        id: keyField
                        width: 280
                        echoMode: TextInput.Password
                        placeholderText: qsTr("API Key")
                        selectByMouse: true
                        Component.onCompleted: text = mdblist.apiKey
                    }
                    Button {
                        text: qsTr("Save")
                        onClicked: {
                            mdblist.setApiKey(keyField.text)
                            keyField.text = mdblist.apiKey
                        }
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("EXTERNAL RATINGS PROVIDERS")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: [
                        { id: "imdb",       title: qsTr("IMDb") },
                        { id: "tmdb",       title: qsTr("TMDB") },
                        { id: "tomatoes",   title: qsTr("Rotten Tomatoes") },
                        { id: "metacritic", title: qsTr("Metacritic") },
                        { id: "trakt",      title: qsTr("Trakt") },
                        { id: "letterboxd", title: qsTr("Letterboxd") },
                        { id: "audience",   title: qsTr("Audience Score") },
                        { id: "mal",        title: qsTr("MyAnimeList") }
                    ]
                    delegate: Row {
                        required property var modelData
                        width: parent.width
                        Switch {
                            checked: mdblist.isProviderEnabled(modelData.id)
                            enabled: mdblist.enabled && mdblist.hasApiKey
                            onToggled: mdblist.setProviderEnabled(
                                           modelData.id, checked)
                        }
                        Text {
                            text: modelData.title
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
    }
}
