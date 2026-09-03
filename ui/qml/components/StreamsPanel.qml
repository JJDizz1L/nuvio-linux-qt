import QtQuick
import QtQuick.Controls
import "../theme"

// Streams scope panel (P3d): addon/plugin source list for the current
// key with autoplay-scope checkboxes. Compose scoping semantics verbatim:
// an empty selected-set means "all in scope"; matching is by display name;
// scope edits apply to FUTURE autoplay resolutions (the live session is
// untouched - the session owns playback).
Rectangle {
    id: panel

    required property string mediaType
    required property string mediaId

    signal closed()

    color: Theme.chromeScrim
    radius: Theme.radiusMd
    border.color: Theme.border
    border.width: 1

    readonly property var rows: streams.allStreams(mediaType, mediaId)

    function distinctNames(installed) {
        const seen = {}
        const out = []
        for (const r of panel.rows) {
            const isAddon = streams.addonIds().indexOf(r.source) >= 0
            if (isAddon !== installed) continue
            const nm = r.sourceName || r.source
            if (!seen[nm]) { seen[nm] = true; out.push(nm) }
        }
        return out.sort()
    }
    function scopeSet(installed) {
        return installed ? appsettings.streamAutoPlaySelectedAddons
                         : appsettings.streamAutoPlaySelectedPlugins
    }
    function inScope(name, installed) {
        const set = panel.scopeSet(installed)
        return set.length === 0 || set.indexOf(name) >= 0
    }
    function toggleScope(name, installed) {
        const all = panel.distinctNames(installed)
        let set = panel.scopeSet(installed).slice()
        if (set.length === 0) set = all.slice()
        const i = set.indexOf(name)
        if (i >= 0) set.splice(i, 1)
        else set.push(name)
        // Full explicit coverage normalizes back to empty (Compose treats
        // empty as all, which additionally covers later-installed addons).
        if (set.length === all.length) set = []
        if (installed) appsettings.streamAutoPlaySelectedAddons = set
        else appsettings.streamAutoPlaySelectedPlugins = set
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Row {
            width: parent.width
            Text {
                width: parent.width - closeBtn.width
                text: qsTr("Sources (%1)").arg(panel.rows.length)
                color: Theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                id: closeBtn
                text: qsTr("Close")
                flat: true
                onClicked: panel.closed()
            }
        }

        ScrollView {
            width: parent.width
            height: Math.min(320, parent.height - 90)
            clip: true
            Column {
                width: parent.width
                spacing: 2

                Text {
                    visible: panel.distinctNames(true).length > 0
                    text: qsTr("Addons")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: panel.distinctNames(true)
                    delegate: Row {
                        required property string modelData
                        width: parent.width
                        spacing: Theme.spacingSm
                        CheckBox {
                            checked: panel.inScope(modelData, true)
                            onToggled: panel.toggleScope(modelData, true)
                        }
                        Text {
                            width: parent.width - 60
                            text: modelData
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                Text {
                    visible: panel.distinctNames(false).length > 0
                    text: qsTr("Plugins")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: panel.distinctNames(false)
                    delegate: Row {
                        required property string modelData
                        width: parent.width
                        spacing: Theme.spacingSm
                        CheckBox {
                            checked: panel.inScope(modelData, false)
                            onToggled: panel.toggleScope(modelData, false)
                        }
                        Text {
                            width: parent.width - 60
                            text: modelData
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                Text {
                    visible: panel.rows.length === 0
                    text: qsTr("No sources resolved for this title yet.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }
        }

        Text {
            width: parent.width
            text: qsTr("Scope applies to future autoplay picks. Torrent rows play through the built-in engine.")
            color: Theme.textDisabled
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
    }
}
