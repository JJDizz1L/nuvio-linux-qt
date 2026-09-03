import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Streams & autoplay: source auto-play, scoped sets,
// next-episode continuation, skip-intro providers.
Item {
    id: streamsPage

    function segChecked(list, value) {
        return list.indexOf(value) >= 0
    }
    function segToggled(list, value, on) {
        let out = list.slice()
        const i = out.indexOf(value)
        if (on && i < 0) out.push(value)
        if (!on && i >= 0) out.splice(i, 1)
        return out
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
            text: qsTr("Streams & autoplay")
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
                        textRole: "label"
                        model: [
                            { label: "0 s", value: 0 },
                            { label: "3 s", value: 3 },
                            { label: "5 s", value: 5 },
                            { label: "10 s", value: 10 },
                            { label: "15 s", value: 15 },
                            { label: "30 s", value: 30 },
                            { label: qsTr("No timeout"),
                              value: 2147483647 }
                        ]
                        currentIndex: {
                            const vals = model.map(
                                function(m){ return m.value })
                            const i = vals.indexOf(
                                appsettings.streamAutoPlayTimeoutSeconds)
                            return i >= 0 ? i : 1
                        }
                        onActivated: function(i) {
                            appsettings.streamAutoPlayTimeoutSeconds =
                                model[i].value
                        }
                    }
                    Text {
                        text: qsTr("Regex")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: 220
                        text: appsettings.streamAutoPlayRegex
                        selectByMouse: true
                        onEditingFinished:
                            appsettings.streamAutoPlayRegex = text
                    }
                }
                Text {
                    text: qsTr("Addon scope: %1 · Plugin scope: %2")
                              .arg(appsettings.streamAutoPlaySelectedAddons.length)
                              .arg(appsettings.streamAutoPlaySelectedPlugins.length)
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Button {
                        text: qsTr("Clear scoped sets")
                        onClicked: {
                            appsettings.streamAutoPlaySelectedAddons = []
                            appsettings.streamAutoPlaySelectedPlugins = []
                        }
                    }
                }
                Text {
                    text: qsTr("Scoped pickers land with the streams panel "
                               + "(P3); the sets already persist and sync.")
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
                    text: qsTr("Next episode")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.streamAutoPlayNextEpisodeEnabled
                        onToggled: appsettings.streamAutoPlayNextEpisodeEnabled = checked
                    }
                    Text {
                        text: qsTr("Auto-play next episode")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.streamAutoPlayNextEpisodeFallbackEnabled
                        onToggled: appsettings.streamAutoPlayNextEpisodeFallbackEnabled = checked
                    }
                    Text {
                        text: qsTr("Fallback source when preferred fails")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.streamAutoPlayPreferBingeGroup
                        onToggled: appsettings.streamAutoPlayPreferBingeGroup = checked
                    }
                    Text {
                        text: qsTr("Prefer binge group")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.streamAutoPlayReuseBingeGroup
                        onToggled: appsettings.streamAutoPlayReuseBingeGroup = checked
                    }
                    Text {
                        text: qsTr("Reuse binge group")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Trigger point")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                ComboBox {
                    width: parent.width * 0.6
                    textRole: "label"
                    model: [
                        { label: qsTr("Percent watched"),
                          value: "PERCENTAGE" },
                        { label: qsTr("Minutes before end"),
                          value: "MINUTES_BEFORE_END" }
                    ]
                    currentIndex:
                        ["PERCENTAGE", "MINUTES_BEFORE_END"]
                            .indexOf(appsettings.nextEpisodeThresholdMode)
                    onActivated: function(i) {
                        appsettings.nextEpisodeThresholdMode = model[i].value
                    }
                }
                Row {
                    visible: appsettings.nextEpisodeThresholdMode === "PERCENTAGE"
                    width: parent.width
                    spacing: Theme.spacingSm
                    Slider {
                        width: parent.width * 0.6
                        from: 0; to: 100; stepSize: 1
                        value: appsettings.nextEpisodeThresholdPercent
                        onMoved: appsettings.nextEpisodeThresholdPercent = value
                    }
                    Label {
                        text: Math.round(
                            appsettings.nextEpisodeThresholdPercent) + " %"
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    visible: appsettings.nextEpisodeThresholdMode === "MINUTES_BEFORE_END"
                    width: parent.width
                    spacing: Theme.spacingSm
                    Slider {
                        width: parent.width * 0.6
                        from: 0; to: 30; stepSize: 0.5
                        value: appsettings.nextEpisodeThresholdMinutesBeforeEnd
                        onMoved: appsettings.nextEpisodeThresholdMinutesBeforeEnd = value
                    }
                    Label {
                        text: appsettings.nextEpisodeThresholdMinutesBeforeEnd.toFixed(1) + qsTr(" min")
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Continuation behavior lands with the player "
                               + "work (P3); thresholds already sync.")
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
                    text: qsTr("Skip intro")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.skipIntroEnabled
                        onToggled: appsettings.skipIntroEnabled = checked
                    }
                    Text {
                        text: qsTr("Skip intro segments")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingMd
                    Repeater {
                        model: ["intro", "recap", "outro"]
                        delegate: Row {
                            required property string modelData
                            spacing: Theme.spacingSm
                            Switch {
                                checked: streamsPage.segChecked(
                                    appsettings.autoSkipSegmentTypes,
                                    modelData)
                                onToggled: appsettings.autoSkipSegmentTypes =
                                    streamsPage.segToggled(
                                        appsettings.autoSkipSegmentTypes,
                                        modelData, checked)
                            }
                            Text {
                                text: modelData
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.animeSkipEnabled
                        onToggled: appsettings.animeSkipEnabled = checked
                    }
                    Text {
                        text: qsTr("AnimeSkip")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: 220
                        text: appsettings.animeSkipClientId
                        selectByMouse: true
                        placeholderText: qsTr("AnimeSkip client id")
                        onEditingFinished:
                            appsettings.animeSkipClientId = text
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        width: parent.width * 0.35
                        text: qsTr("IntroDb API key")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: parent.width * 0.6
                        text: appsettings.introDbApiKey
                        selectByMouse: true
                        echoMode: TextInput.Password
                        onEditingFinished:
                            appsettings.introDbApiKey = text
                    }
                }
                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Switch {
                        checked: appsettings.introSubmitEnabled
                        onToggled: appsettings.introSubmitEnabled = checked
                    }
                    Text {
                        text: qsTr("Submit intros (IntroDb)")
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    text: qsTr("Segment clients land with the player work "
                               + "(P3); the API key never leaves this device "
                               + "over sync.")
                    color: Theme.textDisabled
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }
        }
    }
}
