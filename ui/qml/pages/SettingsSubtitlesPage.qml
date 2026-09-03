import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Subtitles & tracks: languages, style, forced/SDH.
// Subtitle colors apply live into mpv via PreferencesApplier.
Item {
    id: subtitlesPage

    property var audioLangs: [
        { label: qsTr("Follow device"), value: "device" },
        { label: qsTr("Original"),      value: "original" },
        { label: "English",   value: "en" },
        { label: "日本語",     value: "ja" },
        { label: "한국어",     value: "ko" },
        { label: "中文",       value: "zh" },
        { label: "Español",          value: "es" },
        { label: "Español (LatAm)",  value: "es-419" },
        { label: "Português (BR)",   value: "pt-BR" },
        { label: "Français",  value: "fr" },
        { label: "Deutsch",   value: "de" },
        { label: "Italiano",  value: "it" },
        { label: "Русский",   value: "ru" },
        { label: "हिन्दी",      value: "hi" }
    ]
    property var subLangs: [
        { label: qsTr("Off"),                 value: "none" },
        { label: qsTr("Forced only"),         value: "forced" },
        { label: "English",   value: "en" },
        { label: "日本語",     value: "ja" },
        { label: "한국어",     value: "ko" },
        { label: "中文",       value: "zh" },
        { label: "Español",          value: "es" },
        { label: "Español (LatAm)",  value: "es-419" },
        { label: "Português (BR)",   value: "pt-BR" },
        { label: "Français",  value: "fr" },
        { label: "Deutsch",   value: "de" },
        { label: "Italiano",  value: "it" },
        { label: "Русский",   value: "ru" },
        { label: "हिन्दी",      value: "hi" }
    ]

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Subtitles & tracks")
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
                    text: qsTr("Preferred audio language")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: subtitlesPage.audioLangs
                    currentIndex: {
                        const vals = model.map(function(m){ return m.value })
                        const i = vals.indexOf(appsettings.preferredAudioLanguage)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        appsettings.preferredAudioLanguage = model[i].value
                    }
                }
                Text {
                    text: qsTr("Secondary audio (fallback)")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [{ label: qsTr("Unset"), value: "" }]
                               .concat(subtitlesPage.audioLangs)
                    currentIndex: {
                        const vals = model.map(function(m){ return m.value })
                        const i = vals.indexOf(
                            appsettings.secondaryPreferredAudioLanguage)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        appsettings.secondaryPreferredAudioLanguage =
                            model[i].value
                    }
                }
                Text {
                    text: qsTr("Preferred subtitles")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: subtitlesPage.subLangs
                    currentIndex: {
                        const vals = model.map(function(m){ return m.value })
                        const i = vals.indexOf(
                            appsettings.preferredSubtitleLanguage)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        appsettings.preferredSubtitleLanguage = model[i].value
                    }
                }
                Text {
                    text: qsTr("Secondary subtitles (fallback)")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [{ label: qsTr("Unset"), value: "" }]
                               .concat(subtitlesPage.subLangs)
                    currentIndex: {
                        const vals = model.map(function(m){ return m.value })
                        const i = vals.indexOf(
                            appsettings.secondaryPreferredSubtitleLanguage)
                        return i >= 0 ? i : 0
                    }
                    onActivated: function(i) {
                        appsettings.secondaryPreferredSubtitleLanguage =
                            model[i].value
                    }
                }
                Text {
                    text: qsTr("Applies at each file load. Match rules mirror "
                               + "the Compose line (region codes fall back to "
                               + "their primary language).")
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
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.useForcedSubtitles
                        onToggled: appsettings.useForcedSubtitles = checked
                    }
                    Text {
                        text: qsTr("Prefer forced subtitles")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.subtitleStripSdh
                        onToggled: appsettings.subtitleStripSdh = checked
                    }
                    Text {
                        text: qsTr("Strip SDH (hearing-impaired) cues")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.subtitleShowOnlyPreferredLanguages
                        onToggled: appsettings.subtitleShowOnlyPreferredLanguages = checked
                    }
                    Text {
                        text: qsTr("Show only preferred languages")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("SDH filtering and language gating land with "
                               + "the subtitle engine (P3); preferences "
                               + "already persist and sync.")
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
                    text: qsTr("Subtitle size")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Slider {
                        id: subSize
                        width: parent.width - sizeLabel.width - parent.spacing
                        from: 6; to: 40; stepSize: 1
                        value: appsettings.subtitleFontSize
                        onMoved: appsettings.subtitleFontSize = value
                    }
                    Label {
                        id: sizeLabel
                        text: appsettings.subtitleFontSize + " sp"
                        color: Theme.textSecondary
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Text color")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: parent.width * 0.6 - parent.spacing
                        textRole: "label"
                        model: [
                            { label: qsTr("White"),  value: "#FFFFFFFF" },
                            { label: qsTr("Yellow"), value: "#FFFFFF00" },
                            { label: qsTr("Cyan"),   value: "#FF00FFFF" },
                            { label: qsTr("Green"),  value: "#FF00FF00" }
                        ]
                        currentIndex: {
                            const vals = ["#FFFFFFFF", "#FFFFFF00",
                                          "#FF00FFFF", "#FF00FF00"]
                            return vals.indexOf(appsettings.subtitleTextColor)
                        }
                        onActivated: function(i) {
                            appsettings.subtitleTextColor = model[i].value
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Background")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: parent.width * 0.6 - parent.spacing
                        text: appsettings.subtitleBackgroundColor
                        selectByMouse: true
                        placeholderText: qsTr("#AARRGGBB")
                        onEditingFinished:
                            appsettings.subtitleBackgroundColor = text
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Outline color")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: parent.width * 0.6 - parent.spacing
                        text: appsettings.subtitleOutlineColor
                        selectByMouse: true
                        placeholderText: qsTr("#AARRGGBB")
                        onEditingFinished:
                            appsettings.subtitleOutlineColor = text
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        id: outlineSwitch
                        checked: appsettings.subtitleOutlineEnabled
                        onToggled: appsettings.subtitleOutlineEnabled = checked
                    }
                    Text {
                        text: qsTr("Outline")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Slider {
                        from: 0; to: 6; stepSize: 1
                        value: appsettings.subtitleOutlineWidth
                        enabled: outlineSwitch.checked
                        width: 160
                        anchors.verticalCenter: parent.verticalCenter
                        onMoved: appsettings.subtitleOutlineWidth = value
                    }
                    Switch {
                        checked: appsettings.subtitleBold
                        onToggled: appsettings.subtitleBold = checked
                        text: qsTr("Bold")
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.4
                        text: qsTr("Bottom offset")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Slider {
                        width: parent.width * 0.6
                        from: 0; to: 200; stepSize: 5
                        value: appsettings.subtitleBottomOffset
                        onMoved: appsettings.subtitleBottomOffset = value
                    }
                }
            }

            Column {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                spacing: 4
                Text {
                    text: qsTr("Addon subtitle startup mode")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                TextField {
                    width: parent.width * 0.6
                    text: appsettings.addonSubtitleStartupMode
                    selectByMouse: true
                    onEditingFinished:
                        appsettings.addonSubtitleStartupMode = text
                }
            }
        }
    }
}
