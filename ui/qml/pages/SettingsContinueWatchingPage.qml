import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Continue Watching: rail visibility and style (Compose
// continue_watching_preferences parity).
Item {
    id: continueWatchingPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Continue Watching")
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
                Row {
                    width: parent.width
                    Switch {
                        checked: watching.cwPrefs.visible !== false
                        onToggled: watching.setCwVisible(checked)
                    }
                    Text {
                        text: qsTr("Continue Watching rail")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Card style")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [
                        { label: qsTr("Card"),   value: "Card" },
                        { label: qsTr("Wide"),   value: "Wide" },
                        { label: qsTr("Poster"), value: "Poster" }
                    ]
                    currentIndex: {
                        const vals = ["Card", "Wide", "Poster"]
                        const i = vals.indexOf(watching.cwPrefs.style)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        watching.setCwStyle(model[i].value)
                    }
                }
                Text {
                    text: qsTr("Sort mode")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [
                        { label: qsTr("Default (recency)"),    value: "DEFAULT" },
                        { label: qsTr("Streaming style"),      value: "STREAMING_STYLE" },
                        { label: qsTr("Split upcoming"),       value: "SPLIT_UPCOMING" }
                    ]
                    currentIndex: {
                        const vals = ["DEFAULT", "STREAMING_STYLE",
                                      "SPLIT_UPCOMING"]
                        const i = vals.indexOf(watching.cwPrefs.sortMode)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        watching.setCwSortMode(model[i].value)
                    }
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: watching.cwPrefs.episodeThumbnails !== false
                        onToggled: watching.setCwEpisodeThumbnails(checked)
                    }
                    Text {
                        text: qsTr("Use episode thumbnails")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: watching.cwPrefs.upNextFurthest !== false
                        onToggled: watching.setCwUpNextFurthest(checked)
                    }
                    Text {
                        text: qsTr("Next up from furthest episode")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: watching.cwPrefs.unairedNextUp !== false
                        onToggled: watching.setCwUnairedNextUp(checked)
                    }
                    Text {
                        text: qsTr("Show unaired next-up")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: watching.cwPrefs.blurNextUp === true
                        onToggled: watching.setCwBlurNextUp(checked)
                    }
                    Text {
                        text: qsTr("Blur next-up art")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    Switch {
                        checked: watching.cwPrefs.resumePrompt !== false
                        onToggled: watching.setCwResumePrompt(checked)
                    }
                    Text {
                        text: qsTr("Resume prompt on launch")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Shares the Compose-line continue_watching_preferences "
                               + "profile key. Next-up knobs persist for parity; the "
                               + "Qt rail has no next-up candidates yet.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }
        }
    }
}
