import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: settingsPage

    // ---- header ------------------------------------------------------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Settings")
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

        // ---- appearance ------------------------------------------------------
        Row {
            width: parent.width
            Text {
                width: parent.width * 0.6
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Dark theme")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            Switch {
                checked: appsettings.darkTheme
                onClicked: appsettings.darkTheme = !appsettings.darkTheme
            }
        }

        // ---- appearance: poster hover preview -----------------------------------
        Column {
            width: parent.width
            spacing: 4
            Row {
                width: parent.width
                spacing: Theme.spacingSm
                Switch {
                    id: hoverSwitch
                    checked: appsettings.hoverPreviewEnabled
                    onToggled: appsettings.hoverPreviewEnabled = checked
                }
                Text {
                    text: qsTr("Poster hover preview")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                visible: hoverSwitch.checked
                width: parent.width
                spacing: Theme.spacingSm
                Text {
                    width: parent.width * 0.4
                    text: qsTr("Hover delay")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
                Slider {
                    width: parent.width * 0.5
                    from: 500; to: 5000; stepSize: 250
                    value: appsettings.hoverPreviewDelayMs
                    onMoved: appsettings.hoverPreviewDelayMs = value
                }
                Label {
                    text: (appsettings.hoverPreviewDelayMs / 1000) + " s"
                    color: Theme.textSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // ---- playback: decoder -------------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Hardware decoding")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            ComboBox {
                id: decoderBox
                width: parent.width * 0.6
                // Compose-parity contract exposes exactly three states
                // (device / prefer-device share one hwdec chain); explicit
                // vendor pins (vaapi/nvdec) were dropped in the P4 migration.
                model: ["auto", "software"]
                currentIndex: ["auto", "software"]
                    .indexOf(appsettings.decoderMode)
                onActivated: function(i) {
                    appsettings.decoderMode = model[i]
                    applier.applyAll()      // live push into the running core
                }
            }
            Text {
                text: qsTr("auto lets mpv pick per GPU vendor; software is the broken-GL escape hatch")
                color: Theme.textDisabled
                font.pixelSize: 11
                wrapMode: Text.Wrap
                width: parent.width
            }
        }

        // ---- playback: cache ----------------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Row {
                width: parent.width
                Text {
                    width: parent.width * 0.7
                    text: qsTr("Streaming cache")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Label {
                    text: appsettings.cacheMb + " MB"
                    color: Theme.textSecondary
                }
            }
            Slider {
                width: parent.width * 0.8
                from: 64; to: 2048; stepSize: 64
                value: appsettings.cacheMb
                onMoved: appsettings.cacheMb = value   // persists while dragging
            }
        }

        // ---- playback: subtitles ------------------------------------------------
        Column {
            width: parent.width
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
                    text: qsTr("Subtitle color")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: subColorBox
                    width: parent.width * 0.6 - parent.spacing
                    textRole: "label"
                    model: [
                        { label: qsTr("White"),  value: "#FFFFFFFF" },
                        { label: qsTr("Yellow"), value: "#FFFFFF00" },
                        { label: qsTr("Cyan"),   value: "#FF00FFFF" },
                        { label: qsTr("Green"),  value: "#FF00FF00" }
                    ]
                    currentIndex: {
                        const vals = ["#FFFFFFFF", "#FFFFFF00", "#FF00FFFF", "#FF00FF00"]
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
                    id: outlineWidth
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

        // ---- playback: stream autoplay ------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Stream autoplay")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            Row {
                width: parent.width
                spacing: Theme.spacingSm
                ComboBox {
                    width: parent.width * 0.55
                    textRole: "label"
                    model: [
                        { label: qsTr("Manual"), value: "MANUAL" },
                        { label: qsTr("First stream"), value: "FIRST_STREAM" },
                        { label: qsTr("Regex match"), value: "REGEX_MATCH" }
                    ]
                    currentIndex:
                        ["MANUAL", "FIRST_STREAM", "REGEX_MATCH"]
                            .indexOf(appsettings.streamAutoPlayMode)
                    onActivated: function(i) {
                        appsettings.streamAutoPlayMode = model[i].value
                    }
                }
                ComboBox {
                    width: parent.width * 0.45 - parent.spacing
                    textRole: "label"
                    model: [
                        { label: qsTr("All sources"), value: "ALL_SOURCES" },
                        { label: qsTr("Addons only"),
                          value: "INSTALLED_ADDONS_ONLY" },
                        { label: qsTr("Plugins only"),
                          value: "ENABLED_PLUGINS_ONLY" }
                    ]
                    currentIndex:
                        ["ALL_SOURCES", "INSTALLED_ADDONS_ONLY",
                         "ENABLED_PLUGINS_ONLY"]
                            .indexOf(appsettings.streamAutoPlaySource)
                    onActivated: function(i) {
                        appsettings.streamAutoPlaySource = model[i].value
                    }
                }
            }
            Row {
                width: parent.width
                spacing: Theme.spacingSm
                Text {
                    text: qsTr("Timeout")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: timeoutBox
                    model: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30]
                    currentIndex:
                        model.indexOf(appsettings.streamAutoPlayTimeoutSeconds)
                    onActivated: function(i) {
                        appsettings.streamAutoPlayTimeoutSeconds = model[i]
                    }
                }
                Text {
                    text: qsTr("Regex")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: regexField
                    width: 220
                    text: appsettings.streamAutoPlayRegex
                    selectByMouse: true
                    onEditingFinished:
                        appsettings.streamAutoPlayRegex = text
                }
            }
        }

        // ---- playback: track languages ------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Preferred audio language")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            ComboBox {
                width: parent.width * 0.6
                textRole: "label"
                model: [
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
                currentIndex: {
                    const vals = model.map(function(m){ return m.value })
                    const i = vals.indexOf(appsettings.preferredAudioLanguage)
                    return i >= 0 ? i : 0               // device default
                }
                onActivated: function(i) {
                    appsettings.preferredAudioLanguage = model[i].value
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
                model: [
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
                currentIndex: {
                    const vals = model.map(function(m){ return m.value })
                    const i = vals.indexOf(appsettings.preferredSubtitleLanguage)
                    return i >= 0 ? i : 0               // none default
                }
                onActivated: function(i) {
                    appsettings.preferredSubtitleLanguage = model[i].value
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

        // ---- integrations: discord ---------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Discord Rich Presence")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            Switch {
                checked: appsettings.discordEnabled
                onClicked: appsettings.discordEnabled = !appsettings.discordEnabled
            }
            Text {
                text: qsTr("Show what you're watching on your profile. "
                           + "Connects/disconnects live.")
                color: Theme.textDisabled
                font.pixelSize: 11
                wrapMode: Text.Wrap
                width: parent.width
            }
        }

        // ---- torrent: cache size ------------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Torrent cache size")
                color: Theme.textPrimary
                font.pixelSize: 15
            }
            ComboBox {
                width: parent.width * 0.6
                textRole: "label"
                model: [
                    { label: qsTr("Off (64 MB floor)"), value: "NONE" },
                    { label: qsTr("2 GB"),              value: "GB_2" },
                    { label: qsTr("5 GB"),              value: "GB_5" },
                    { label: qsTr("10 GB"),             value: "GB_10" }
                ]
                currentIndex: {
                    const vals = ["NONE", "GB_2", "GB_5", "GB_10"]
                    const i = vals.indexOf(appsettings.torrentCacheSize)
                    return i >= 0 ? i : 1            // GB_2 default
                }
                onActivated: function(i) {
                    appsettings.torrentCacheSize = model[i].value
                }
            }
            Text {
                text: qsTr("RAM piece cache pushed to TorrServer before each "
                           + "torrent starts; shares the Compose-line profile key.")
                color: Theme.textDisabled
                font.pixelSize: 11
                wrapMode: Text.Wrap
                width: parent.width
            }
        }

        // ---- home: continue watching -------------------------------------------
        Column {
            width: parent.width
            spacing: 4
            Row {
                width: parent.width
                Switch {
                    id: cwVisibleSwitch
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
                onActivated: function(i) { watching.setCwStyle(model[i].value) }
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
                    const vals = ["DEFAULT", "STREAMING_STYLE", "SPLIT_UPCOMING"]
                    const i = vals.indexOf(watching.cwPrefs.sortMode)
                    return i >= 0 ? i : 0
                }
                onActivated: function(i) { watching.setCwSortMode(model[i].value) }
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

        Text {
            text: qsTr("Values persist to the shared Nuvio profile "
                       + "(~/.config/nuvio-linux).")
            color: Theme.textDisabled
            font.pixelSize: 11
        }
    }
}
