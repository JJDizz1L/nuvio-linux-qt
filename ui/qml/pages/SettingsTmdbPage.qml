import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > TMDB: enrichment master switch, personal v3 api key,
// metadata language, and the 12 module gates (fork TmdbSettingsPage
// parity). The key is a credential: stored locally, stripped from the
// sync blob, traveling only through the provider-credentials family.
// The module switches persist + sync; they gate the metadata
// enrichment engine, which stays deferred per P6 (honest inert flags
// until it lands - same class as the P2 synced-but-inert set).
Item {
    id: tmdbPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("TMDB")
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
                    text: qsTr("TMDB ENRICHMENT")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: tmdb.enabled
                        enabled: tmdb.hasApiKey
                        onToggled: tmdb.setEnabled(checked)
                    }
                    Text {
                        width: parent.width - 80
                        wrapMode: Text.Wrap
                        text: qsTr("Enable TMDB Enrichment")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: !tmdb.hasApiKey
                    text: qsTr("Add your own TMDB API key below before turning enrichment on.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Use TMDB as a metadata source to enhance addon data")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 8
                Text {
                    text: qsTr("CREDENTIALS")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Personal API key")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Enter your TMDB v3 API key.")
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
                        Component.onCompleted: text = tmdb.apiKey
                    }
                    Button {
                        text: qsTr("Save")
                        onClicked: {
                            tmdb.setApiKey(keyField.text)
                            keyField.text = tmdb.apiKey
                        }
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 8
                Text {
                    text: qsTr("LOCALIZATION")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Language")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("TMDB metadata language for title, logo, and enabled fields")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    TextField {
                        id: langField
                        width: 160
                        placeholderText: qsTr("Language code")
                        selectByMouse: true
                        enabled: tmdb.hasApiKey
                        Component.onCompleted: text = tmdb.language
                    }
                    Button {
                        text: qsTr("Save")
                        enabled: tmdb.hasApiKey
                        onClicked: {
                            tmdb.setLanguage(langField.text)
                            langField.text = tmdb.language
                        }
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("MODULES")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: [
                        { title: qsTr("Trailers"),
                          desc: qsTr("Trailer candidates from TMDB videos for the detail trailer section"),
                          get: () => tmdb.useTrailers,
                          set: v => tmdb.setUseTrailers(v) },
                        { title: qsTr("Artwork"),
                          desc: qsTr("Logo and backdrop images from TMDB"),
                          get: () => tmdb.useArtwork,
                          set: v => tmdb.setUseArtwork(v) },
                        { title: qsTr("Basic Info"),
                          desc: qsTr("Description, genres, and rating from TMDB"),
                          get: () => tmdb.useBasicInfo,
                          set: v => tmdb.setUseBasicInfo(v) },
                        { title: qsTr("Details"),
                          desc: qsTr("Runtime, status, country, and language from TMDB"),
                          get: () => tmdb.useDetails,
                          set: v => tmdb.setUseDetails(v) },
                        { title: qsTr("Release dates"),
                          desc: qsTr("Use TMDB broadcaster air dates instead of precise add-on release times"),
                          get: () => tmdb.useReleaseDates,
                          set: v => tmdb.setUseReleaseDates(v) },
                        { title: qsTr("Credits"),
                          desc: qsTr("Cast with photos, director, and writer from TMDB"),
                          get: () => tmdb.useCredits,
                          set: v => tmdb.setUseCredits(v) },
                        { title: qsTr("Productions"),
                          desc: qsTr("Production companies from TMDB"),
                          get: () => tmdb.useProductions,
                          set: v => tmdb.setUseProductions(v) },
                        { title: qsTr("Networks"),
                          desc: qsTr("Networks with logos from TMDB"),
                          get: () => tmdb.useNetworks,
                          set: v => tmdb.setUseNetworks(v) },
                        { title: qsTr("Episodes"),
                          desc: qsTr("Episode titles, overviews, thumbnails, and runtime from TMDB"),
                          get: () => tmdb.useEpisodes,
                          set: v => tmdb.setUseEpisodes(v) },
                        { title: qsTr("Season posters"),
                          desc: qsTr("Use TMDB season posters in the metadata screen season selector for series."),
                          get: () => tmdb.useSeasonPosters,
                          set: v => tmdb.setUseSeasonPosters(v) },
                        { title: qsTr("More Like This"),
                          desc: qsTr("TMDB recommendation backdrops on detail page"),
                          get: () => tmdb.useMoreLikeThis,
                          set: v => tmdb.setUseMoreLikeThis(v) },
                        { title: qsTr("Collections"),
                          desc: qsTr("TMDB movie collections in release order"),
                          get: () => tmdb.useCollections,
                          set: v => tmdb.setUseCollections(v) }
                    ]
                    delegate: Column {
                        required property var modelData
                        width: parent.width
                        spacing: 2
                        Row {
                            width: parent.width
                            Switch {
                                checked: modelData.get()
                                enabled: tmdb.enabled && tmdb.hasApiKey
                                onToggled: modelData.set(checked)
                            }
                            Text {
                                text: modelData.title
                                color: Theme.textPrimary
                                font.pixelSize: 15
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: modelData.desc
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }
}
