import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Plugins: scraper repository management + provider list
// (fork PluginsSettingsScreen parity). Overview badges, global/group
// switches, repo install/remove/refresh, per-provider enable toggles,
// settings dialogs (hasSettings rows), and trial runs with results.
Item {
    id: pluginsPage

    property string repoUrl: ""
    property string message: ""
    property bool adding: false
    property string testingId: ""
    property var testOutputs: ({})
    property string configuringId: ""
    property string configuringName: ""
    property string configuringLayout: "[]"

    Connections {
        target: plugins
        function onAddRepositoryFinished(ok, msg) {
            pluginsPage.adding = false
            if (ok) {
                pluginsPage.repoUrl = ""
                pluginsPage.message = qsTr("Installed %1").arg(msg)
            } else {
                pluginsPage.message = msg
            }
        }
        function onTestFinished(scraperId, rows, error) {
            pluginsPage.testingId = ""
            const out = { error: error, rows: rows }
            const next = Object.assign({}, pluginsPage.testOutputs)
            next[scraperId] = out
            pluginsPage.testOutputs = next
        }
        function onSettingsLayoutReady(scraperId, layout) {
            if (scraperId === pluginsPage.configuringId)
                pluginsPage.configuringLayout = layout
        }
    }

    function sortedRepos() {
        return plugins.repositories.slice().sort(function(a, b) {
            return a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1
        })
    }
    function repoName(url) {
        for (const r of plugins.repositories)
            if (r.manifestUrl === url) return r.name
        // Host fallback label (fork fallbackRepositoryLabel parity).
        const noQuery = url.split("?")[0]
        const noManifest = noQuery.endsWith("/manifest.json")
            ? noQuery.slice(0, -14) : noQuery
        const host = noManifest.split("://").length > 1
            ? noManifest.split("://")[1].split("/")[0] : ""
        if (host !== "") return host
        const tail = noManifest.split("/").pop()
        return tail !== "" ? tail : qsTr("Plugin repository")
    }
    function sortedScrapers() {
        return plugins.scrapers.slice().sort(function(a, b) {
            const ra = pluginsPage.repoName(a.repositoryUrl).toLowerCase()
            const rb = pluginsPage.repoName(b.repositoryUrl).toLowerCase()
            if (ra !== rb) return ra < rb ? -1 : 1
            return a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1
        })
    }
    function fallbackLabel(url) { return pluginsPage.repoName(url) }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Plugins")
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

            Text {
                x: Theme.spacingLg
                text: qsTr("OVERVIEW")
                color: Theme.textSecondary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Rectangle {
                width: parent.width - Theme.spacingLg * 2
                x: Theme.spacingLg
                radius: Theme.radiusMd
                color: Theme.surface
                height: overviewCol.height + 32
                Column {
                    id: overviewCol
                    x: Theme.spacingMd
                    y: 16
                    width: parent.width - 2 * Theme.spacingMd
                    spacing: 8
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("%n repos", "", plugins.repositories.length)
                              + "  ·  " +
                              qsTr("%n providers", "",
                                   plugins.scrapers.length) + "  ·  " +
                              (plugins.pluginsEnabled ? qsTr("Enabled")
                                                      : qsTr("Disabled")) +
                              "  ·  " +
                              (tmdb.hasApiKey ? qsTr("TMDB key set")
                                              : qsTr("TMDB key missing"))
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        visible: !tmdb.hasApiKey
                        text: qsTr("A TMDB API key improves provider id resolution; scrapers still run without one.")
                        color: "#e57373"
                        font.pixelSize: 13
                    }
                    Row {
                        width: parent.width
                        Switch {
                            checked: plugins.pluginsEnabled
                            onToggled: plugins.setPluginsEnabled(checked)
                        }
                        Text {
                            width: parent.width - 80
                            wrapMode: Text.Wrap
                            text: qsTr("Enable plugin providers globally")
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Use plugin providers during stream discovery.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Row {
                        width: parent.width
                        Switch {
                            checked: plugins.groupStreamsByRepository
                            onToggled: plugins.setGroupStreamsByRepository(
                                checked)
                        }
                        Text {
                            width: parent.width - 80
                            wrapMode: Text.Wrap
                            text: qsTr("Group plugin providers by repository")
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("In Streams, show one provider per repository instead of one per source.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            Text {
                x: Theme.spacingLg
                text: qsTr("ADD REPOSITORY")
                color: Theme.textSecondary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Rectangle {
                width: parent.width - Theme.spacingLg * 2
                x: Theme.spacingLg
                radius: Theme.radiusMd
                color: Theme.surface
                height: addCol.height + 32
                Column {
                    id: addCol
                    x: Theme.spacingMd
                    y: 16
                    width: parent.width - 2 * Theme.spacingMd
                    spacing: 8
                    TextField {
                        width: parent.width
                        text: pluginsPage.repoUrl
                        placeholderText: qsTr("https://…/manifest.json")
                        selectByMouse: true
                        enabled: !pluginsPage.adding
                        onTextChanged: {
                            pluginsPage.repoUrl = text
                            pluginsPage.message = ""
                        }
                    }
                    Button {
                        text: pluginsPage.adding ? qsTr("Installing…")
                                                : qsTr("Install Plugin Repository")
                        enabled: pluginsPage.repoUrl.trim() !== "" &&
                                 !pluginsPage.adding
                        onClicked: {
                            if (pluginsPage.repoUrl.trim() === "") {
                                pluginsPage.message =
                                    qsTr("Enter a plugin repository URL.")
                                return
                            }
                            pluginsPage.adding = true
                            pluginsPage.message = ""
                            plugins.addRepository(pluginsPage.repoUrl.trim())
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        visible: pluginsPage.message !== ""
                        text: pluginsPage.message
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            Text {
                x: Theme.spacingLg
                text: qsTr("INSTALLED REPOSITORIES")
                color: Theme.textSecondary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                x: Theme.spacingLg
                visible: plugins.repositories.length === 0
                text: qsTr("No plugin repositories installed yet.")
                color: Theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                x: Theme.spacingLg
                visible: plugins.repositories.length === 0
                width: parent.width - 2 * Theme.spacingLg
                wrapMode: Text.Wrap
                text: qsTr("Add a repository URL to install provider plugins for stream discovery.")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            Repeater {
                model: pluginsPage.sortedRepos()
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width - Theme.spacingLg * 2
                    x: Theme.spacingLg
                    radius: Theme.radiusMd
                    color: Theme.surface
                    height: repoCol.height + 32
                    Column {
                        id: repoCol
                        x: Theme.spacingMd
                        y: 16
                        width: parent.width - 2 * Theme.spacingMd
                        spacing: 6
                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            Text {
                                width: parent.width - 160
                                text: modelData.name
                                color: Theme.textPrimary
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Button {
                                text: qsTr("Refresh")
                                flat: true
                                enabled: !modelData.isRefreshing
                                onClicked: plugins.refreshRepository(
                                    modelData.manifestUrl)
                            }
                            Button {
                                text: qsTr("Delete")
                                flat: true
                                onClicked: plugins.removeRepository(
                                    modelData.manifestUrl)
                            }
                        }
                        Text {
                            visible: (modelData.version || "") !== ""
                            text: qsTr("Version %1").arg(modelData.version)
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                        Text {
                            width: parent.width
                            text: modelData.manifestUrl
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            text: qsTr("%n providers", "",
                                       modelData.scraperCount) +
                                  (modelData.isRefreshing
                                   ? "  ·  " + qsTr("Refreshing") : "")
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            visible: (modelData.errorMessage || "") !== ""
                            text: modelData.errorMessage
                            color: "#e57373"
                            font.pixelSize: 13
                        }
                    }
                }
            }

            Text {
                x: Theme.spacingLg
                text: qsTr("PROVIDERS")
                color: Theme.textSecondary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                x: Theme.spacingLg
                visible: plugins.scrapers.length === 0
                text: qsTr("No providers available yet.")
                color: Theme.textSecondary
                font.pixelSize: 14
            }
            Repeater {
                model: pluginsPage.sortedScrapers()
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width - Theme.spacingLg * 2
                    x: Theme.spacingLg
                    radius: Theme.radiusMd
                    color: Theme.surface
                    height: provCol.height + 32
                    Column {
                        id: provCol
                        x: Theme.spacingMd
                        y: 16
                        width: parent.width - 2 * Theme.spacingMd
                        spacing: 6
                        Row {
                            width: parent.width
                            spacing: Theme.spacingSm
                            Column {
                                width: parent.width - 200
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: pluginsPage.repoName(
                                        modelData.repositoryUrl)
                                    color: Theme.accent
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: (modelData.description || "") !== ""
                                          ? modelData.description
                                          : qsTr("No description.")
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }
                            Button {
                                text: qsTr("Settings")
                                flat: true
                                visible: modelData.hasSettings
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: {
                                    pluginsPage.configuringId = modelData.id
                                    pluginsPage.configuringName = modelData.name
                                    pluginsPage.configuringLayout = "[]"
                                    plugins.requestSettingsLayout(modelData.id)
                                }
                            }
                            Switch {
                                checked: modelData.enabled
                                enabled: modelData.manifestEnabled
                                anchors.verticalCenter: parent.verticalCenter
                                onToggled: plugins.toggleScraper(modelData.id,
                                                                 checked)
                            }
                        }
                        Text {
                            text: modelData.supportedTypes.join(" | ") +
                                  "  ·  " + qsTr("v%1").arg(modelData.version) +
                                  (modelData.manifestEnabled
                                   ? "" : "  ·  " + qsTr("Disabled by repo"))
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Button {
                            text: pluginsPage.testingId === modelData.id
                                  ? qsTr("Testing…") : qsTr("Test Provider")
                            enabled: pluginsPage.testingId === "" &&
                                     modelData.enabled
                            onClicked: {
                                pluginsPage.testingId = modelData.id
                                const next = Object.assign(
                                    {}, pluginsPage.testOutputs)
                                delete next[modelData.id]
                                pluginsPage.testOutputs = next
                                plugins.testScraper(modelData.id)
                            }
                        }
                        Column {
                            width: parent.width
                            spacing: 4
                            visible: pluginsPage.testOutputs[modelData.id] !==
                                     undefined
                            Text {
                                visible: (pluginsPage.testOutputs[
                                              modelData.id] || {}).error !==
                                         undefined &&
                                         (pluginsPage.testOutputs[
                                              modelData.id] || {}).error !== ""
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: (pluginsPage.testOutputs[
                                           modelData.id] || {}).error || ""
                                color: "#e57373"
                                font.pixelSize: 13
                            }
                            Text {
                                visible: ((pluginsPage.testOutputs[
                                               modelData.id] || {}).rows ||
                                          []).length > 0
                                text: qsTr("%n results", "",
                                           ((pluginsPage.testOutputs[
                                                  modelData.id] || {}).rows ||
                                             []).length)
                                color: Theme.textPrimary
                                font.pixelSize: 14
                            }
                            Repeater {
                                model: ((pluginsPage.testOutputs[
                                             modelData.id] || {}).rows ||
                                        []).slice(0, 8)
                                delegate: Column {
                                    required property var modelData
                                    width: parent.width
                                    Text {
                                        width: parent.width
                                        text: modelData.title
                                        color: Theme.textPrimary
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.url
                                        color: Theme.textSecondary
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Scraper settings dialog (layout schema: header/info/text/select/
    // toggle rows; values persist per scraper id, global like the fork).
    Rectangle {
        visible: pluginsPage.configuringId !== ""
        anchors.fill: parent
        color: "#80000000"
        MouseArea {
            anchors.fill: parent
            onClicked: pluginsPage.configuringId = ""
        }
        Rectangle {
            width: Math.min(parent.width - 64, 480)
            height: Math.min(parent.height - 64, dlgCol.height + 48)
            anchors.centerIn: parent
            radius: Theme.radiusMd
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            ScrollView {
                anchors.fill: parent
                anchors.margins: 16
                clip: true
                Column {
                    id: dlgCol
                    width: parent.width
                    spacing: 10
                    Text {
                        text: qsTr("%1 Settings").arg(
                            pluginsPage.configuringName)
                        color: Theme.accent
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: JSON.parse(pluginsPage.configuringLayout ||
                                          "[]")
                        delegate: Column {
                            required property var modelData
                            width: parent.width
                            spacing: 4
                            Text {
                                visible: modelData.type === "header"
                                text: modelData.label || ""
                                color: Theme.textSecondary
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                visible: modelData.type === "info"
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: modelData.label || ""
                                color: Theme.textSecondary
                                font.pixelSize: 13
                            }
                            Text {
                                visible: modelData.type === "text" ||
                                         modelData.type === "select" ||
                                         modelData.type === "toggle"
                                text: modelData.label || ""
                                color: Theme.textPrimary
                                font.pixelSize: 14
                            }
                            Text {
                                visible: (modelData.description || "") !== "" &&
                                         modelData.type !== "header" &&
                                         modelData.type !== "info"
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: modelData.description || ""
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                            TextField {
                                visible: modelData.type === "text"
                                width: parent.width
                                selectByMouse: true
                                echoMode: modelData.isPassword === true
                                          ? TextInput.Password
                                          : TextInput.Normal
                                placeholderText: modelData.placeholder || ""
                                text: {
                                    const saved = JSON.parse(
                                        plugins.loadScraperSettings(
                                            pluginsPage.configuringId))
                                    return saved[modelData.key] || ""
                                }
                                onEditingFinished: {
                                    const saved = JSON.parse(
                                        plugins.loadScraperSettings(
                                            pluginsPage.configuringId))
                                    saved[modelData.key] = text
                                    plugins.saveScraperSettings(
                                        pluginsPage.configuringId,
                                        JSON.stringify(saved))
                                }
                            }
                            ComboBox {
                                visible: modelData.type === "select"
                                width: parent.width
                                textRole: "label"
                                model: (modelData.options || []).map(
                                    function(o) {
                                        return { label: o.label, value: o.value }
                                    })
                                Component.onCompleted: {
                                    const saved = JSON.parse(
                                        plugins.loadScraperSettings(
                                            pluginsPage.configuringId))
                                    const cur = saved[modelData.key] ||
                                                modelData.defaultValue || ""
                                    for (let i = 0; i < count; ++i) {
                                        if (model[i] &&
                                            model[i].value === cur) {
                                            currentIndex = i
                                            break
                                        }
                                    }
                                }
                                onActivated: function(i) {
                                    const saved = JSON.parse(
                                        plugins.loadScraperSettings(
                                            pluginsPage.configuringId))
                                    saved[modelData.key] = model[i].value
                                    plugins.saveScraperSettings(
                                        pluginsPage.configuringId,
                                        JSON.stringify(saved))
                                }
                            }
                            Switch {
                                visible: modelData.type === "toggle"
                                checked: {
                                    const saved = JSON.parse(
                                        plugins.loadScraperSettings(
                                            pluginsPage.configuringId))
                                    if (saved[modelData.key] !== undefined)
                                        return !!saved[modelData.key]
                                    return !!modelData.defaultValue
                                }
                                onToggled: {
                                    const saved = JSON.parse(
                                        plugins.loadScraperSettings(
                                            pluginsPage.configuringId))
                                    saved[modelData.key] = checked
                                    plugins.saveScraperSettings(
                                        pluginsPage.configuringId,
                                        JSON.stringify(saved))
                                }
                            }
                        }
                    }
                    Row {
                        spacing: Theme.spacingSm
                        Button {
                            text: qsTr("Close")
                            flat: true
                            onClicked: pluginsPage.configuringId = ""
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: plugins.initialize()
}
