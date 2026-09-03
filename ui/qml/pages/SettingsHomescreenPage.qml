import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Homescreen: hero/rails visibility, unreleased filter, and
// per-shelf order/enable/hero-source/custom-title (shared Compose
// home_catalog_settings profile key).
Item {
    id: homescreenPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Homescreen")
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
                    checked: homeshelves.heroEnabled
                    onToggled: homeshelves.setHeroEnabled(checked)
                }
                Text {
                    text: qsTr("Hero spotlight")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                Switch {
                    checked: homeshelves.showCatalogType
                    onToggled: homeshelves.setShowCatalogType(checked)
                }
                Text {
                    text: qsTr("Show media type in rail titles")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                Switch {
                    checked: homeshelves.hideUnreleasedContent
                    onToggled: homeshelves.setHideUnreleasedContent(checked)
                }
                Text {
                    text: qsTr("Hide unreleased titles")
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Text {
                x: Theme.spacingLg
                text: qsTr("Rails (in order)")
                color: Theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            Repeater {
                model: homeshelves.shelfPrefs
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    height: shelfCol.height + 16
                    radius: Theme.radiusMd
                    color: Theme.surface

                    Column {
                        id: shelfCol
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        spacing: 4

                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            Switch {
                                checked: modelData.enabled
                                onToggled: homeshelves.setShelfEnabled(
                                    modelData.key, checked)
                            }
                            Text {
                                width: parent.width - 120
                                text: modelData.title
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Button {
                                text: qsTr("↑")
                                flat: true
                                enabled: index > 0
                                onClicked: homeshelves.moveShelf(
                                    modelData.key, -1)
                            }
                            Button {
                                text: qsTr("↓")
                                flat: true
                                enabled: index < homeshelves.shelfPrefs.length - 1
                                onClicked: homeshelves.moveShelf(
                                    modelData.key, 1)
                            }
                        }
                        Text {
                            text: modelData.addonName
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            Switch {
                                checked: modelData.heroSourceEnabled
                                onToggled: homeshelves.setShelfHeroSource(
                                    modelData.key, checked)
                            }
                            Text {
                                text: qsTr("Hero source")
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        TextField {
                            width: parent.width
                            text: modelData.customTitle
                            selectByMouse: true
                            placeholderText: qsTr("Custom title (empty = default)")
                            onEditingFinished: homeshelves.setShelfCustomTitle(
                                modelData.key, text)
                        }
                    }
                }
            }

            Text {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                text: qsTr("Rails come from installed addons; required-search "
                           + "catalogs are skipped. Shares the Compose-line "
                           + "home_catalog_settings profile key.")
                color: Theme.textDisabled
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
    }
}
