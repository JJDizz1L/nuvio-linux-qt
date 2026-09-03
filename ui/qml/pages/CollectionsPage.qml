import QtQuick
import QtQuick.Controls
import "../theme"

// Collections manager (P5): create/rename/pin/reorder/delete collections,
// folders, and addon-catalog sources. TMDB/Trakt sources ride along
// untouched (see CollectionStore verbatim-preservation).
Item {
    id: collectionsPage

    // Add-source picker state (per folder row, expanded inline).
    property string pickingFor: ""   // "collectionId/folderId" or ""

    function addonCatalogs() {
        // Installed, enabled, manifest-backed addons and their catalogs.
        const out = []
        for (const a of addons.addons) {
            if (a.enabled === false || !a.id) continue
            for (const c of (a.catalogs || [])) {
                if (c.hasRequiredExtra) continue
                out.push({ addonId: a.id, addonName: a.name,
                           type: c.type, catalogId: c.id,
                           name: c.name,
                           label: a.name + " · " + c.name
                                  + " (" + c.type + ")" })
            }
        }
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
            text: qsTr("Collections")
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
                spacing: Theme.spacingSm
                TextField {
                    id: newTitle
                    width: parent.width - createBtn.width - parent.spacing
                    placeholderText: qsTr("New collection title")
                    selectByMouse: true
                    onAccepted: createBtn.clicked()
                }
                Button {
                    id: createBtn
                    text: qsTr("Create")
                    onClicked: {
                        if (newTitle.text.trim().length === 0) return
                        collections.createCollection(newTitle.text)
                        newTitle.text = ""
                    }
                }
            }

            Repeater {
                model: collections.collections
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    height: colInner.height + 20
                    radius: Theme.radiusMd
                    color: Theme.surface

                    Column {
                        id: colInner
                        property string collectionId: modelData.id
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
                                width: parent.width - 260
                                text: (modelData.pinned ? "📌 " : "")
                                      + modelData.title
                                color: Theme.textPrimary
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Button {
                                text: qsTr("↑")
                                flat: true
                                enabled: index > 0
                                onClicked: collections.moveCollection(
                                    modelData.id, -1)
                            }
                            Button {
                                text: qsTr("↓")
                                flat: true
                                enabled: index < collections.collections.length - 1
                                onClicked: collections.moveCollection(
                                    modelData.id, 1)
                            }
                            Button {
                                text: modelData.pinned ? qsTr("Unpin")
                                                       : qsTr("Pin")
                                flat: true
                                onClicked: collections.setCollectionPinned(
                                    modelData.id, !modelData.pinned)
                            }
                            Button {
                                text: qsTr("Open")
                                onClicked: {
                                    collections.openCollection(modelData.id)
                                    navigation.push("collectiondetail")
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            TextField {
                                id: renameField
                                width: 200
                                text: modelData.title
                                selectByMouse: true
                            }
                            Button {
                                text: qsTr("Rename")
                                flat: true
                                onClicked: collections.renameCollection(
                                    modelData.id, renameField.text)
                            }
                            Button {
                                text: qsTr("Delete")
                                flat: true
                                onClicked: collections.removeCollection(
                                    modelData.id)
                            }
                        }

                        Repeater {
                            model: modelData.folders
                            delegate: Column {
                                id: folderCol
                                required property var modelData
                                width: parent.width
                                spacing: 4

                                property string folderId: modelData.id
                                property string folderKey:
                                    colInner.collectionId + "/" + modelData.id

                                Row {
                                    width: parent.width
                                    spacing: Theme.spacingSm
                                    Text {
                                        width: parent.width - 220
                                        text: "▸ " + modelData.title
                                              + qsTr(" (%n source(s))", "",
                                                     modelData.sources.length
                                                     + (modelData.sourceCount
                                                        - modelData.sources.length))
                                        color: Theme.textPrimary
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Button {
                                        text: qsTr("Sources")
                                        flat: true
                                        onClicked: collectionsPage.pickingFor =
                                            collectionsPage.pickingFor === folderKey
                                            ? "" : folderKey
                                    }
                                    Button {
                                        text: qsTr("Delete")
                                        flat: true
                                        onClicked: collections.removeFolder(
                                            colInner.collectionId, modelData.id)
                                    }
                                }
                                TextField {
                                    width: parent.width
                                    visible: collectionsPage.pickingFor === folderKey
                                    text: modelData.title
                                    selectByMouse: true
                                    placeholderText: qsTr("Folder title (Enter to rename)")
                                    onAccepted: collections.renameFolder(
                                        colInner.collectionId, modelData.id, text)
                                }
                                Column {
                                    visible: collectionsPage.pickingFor === folderKey
                                    width: parent.width
                                    spacing: 4

                                    Repeater {
                                        model: modelData.sources
                                        delegate: Row {
                                            required property var modelData
                                            required property int index
                                            width: parent.width
                                            spacing: Theme.spacingSm
                                            Text {
                                                width: parent.width - 80
                                                text: modelData.addonId + " · "
                                                      + modelData.type + "/"
                                                      + modelData.catalogId
                                                      + (modelData.genre
                                                         ? " · " + modelData.genre
                                                         : "")
                                                color: Theme.textSecondary
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Button {
                                                text: qsTr("Remove")
                                                flat: true
                                                onClicked: collections.removeAddonSource(
                                                    colInner.collectionId,
                                                    folderCol.folderId,
                                                    index)
                                            }
                                        }
                                    }

                                    Row {
                                        width: parent.width
                                        spacing: Theme.spacingSm
                                        ComboBox {
                                            id: catalogPicker
                                            width: parent.width - 150
                                            textRole: "label"
                                            model: collectionsPage.addonCatalogs()
                                            currentIndex: 0
                                        }
                                        TextField {
                                            id: genreField
                                            width: 70
                                            placeholderText: qsTr("Genre")
                                            selectByMouse: true
                                        }
                                        Button {
                                            text: qsTr("Add")
                                            onClicked: {
                                                const pick = catalogPicker.model[
                                                    catalogPicker.currentIndex]
                                                if (!pick) return
                                                collections.addAddonSource(
                                                    colInner.collectionId,
                                                    folderCol.folderId,
                                                    pick.addonId, pick.type,
                                                    pick.catalogId,
                                                    genreField.text)
                                                genreField.text = ""
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            TextField {
                                id: folderField
                                width: 200
                                placeholderText: qsTr("New folder title")
                                selectByMouse: true
                                onAccepted: addFolderBtn.clicked()
                            }
                            Button {
                                id: addFolderBtn
                                text: qsTr("Add folder")
                                flat: true
                                onClicked: {
                                    if (folderField.text.trim().length === 0)
                                        return
                                    collections.createFolder(
                                        modelData.id, folderField.text)
                                    folderField.text = ""
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: collections.collections.length === 0
                x: Theme.spacingLg
                text: qsTr("No collections yet. Create one above, add folders, "
                           + "then attach addon catalogs as sources.")
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
                width: parent.width - 2 * Theme.spacingLg
            }
        }
    }
}
