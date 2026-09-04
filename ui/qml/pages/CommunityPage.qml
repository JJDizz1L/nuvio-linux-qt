import QtQuick
import QtQuick.Controls
import "../components"
import "../theme"

// Community page (fork SupportersContributorsSettingsScreen parity):
// membership card + Contributors/Supporters tabs with retry. Avatars
// ride plain Image elements; profile/support links open externally.
// Donation progress is absent (desktop policy disables it).
Item {
    id: communityPage

    property string selectedTab: "contributors"

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Supporters & Contributors")
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
        anchors.fill: parent
        anchors.topMargin: header.height
        clip: true

        Column {
            width: parent.width
            spacing: Theme.spacingMd

            MembershipCard {
                width: parent.width - Theme.spacingLg * 2
                x: Theme.spacingLg
                height: implicitHeight
            }

            Row {
                x: Theme.spacingLg
                spacing: Theme.spacingSm
                Button {
                    text: qsTr("Contributors")
                    flat: communityPage.selectedTab !== "contributors"
                    onClicked: {
                        communityPage.selectedTab = "contributors"
                        community.loadContributors(false)
                    }
                }
                Button {
                    text: qsTr("Supporters")
                    flat: communityPage.selectedTab !== "supporters"
                    onClicked: {
                        communityPage.selectedTab = "supporters"
                        community.loadSupporters(false)
                    }
                }
            }

            // Contributors tab.
            Column {
                width: parent.width
                spacing: 6
                visible: communityPage.selectedTab === "contributors"
                Text {
                    x: Theme.spacingLg
                    visible: community.contributorsLoading
                    text: qsTr("Loading contributors…")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
                Text {
                    x: Theme.spacingLg
                    visible: !community.contributorsLoading &&
                             community.contributorsError !== ""
                    text: community.contributorsError
                    color: "#e57373"
                    font.pixelSize: 14
                }
                Button {
                    x: Theme.spacingLg
                    visible: !community.contributorsLoading &&
                             community.contributorsError !== ""
                    text: qsTr("Retry")
                    flat: true
                    onClicked: community.loadContributors(true)
                }
                Text {
                    x: Theme.spacingLg
                    visible: !community.contributorsLoading &&
                             community.contributorsError === "" &&
                             community.contributors.length === 0
                    text: qsTr("No contributors found.")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
                Repeater {
                    model: community.contributors
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width - Theme.spacingLg * 2
                        x: Theme.spacingLg
                        height: 56
                        radius: Theme.radiusMd
                        color: Theme.surface
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            spacing: Theme.spacingSm
                            Image {
                                width: 36; height: 36
                                anchors.verticalCenter: parent.verticalCenter
                                source: modelData.avatarUrl || ""
                                fillMode: Image.PreserveAspectCrop
                                visible: (modelData.avatarUrl || "") !== ""
                            }
                            Column {
                                width: parent.width - 120
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    width: parent.width
                                    text: modelData.login
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: qsTr("%n total commits", "",
                                                modelData.totalContributions)
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                }
                            }
                            Button {
                                text: qsTr("Open")
                                flat: true
                                visible: (modelData.profileUrl || "") !== ""
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: Qt.openUrlExternally(
                                    modelData.profileUrl)
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: (modelData.profileUrl || "") !== ""
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(
                                modelData.profileUrl)
                        }
                    }
                }
            }

            // Supporters tab.
            Column {
                width: parent.width
                spacing: 6
                visible: communityPage.selectedTab === "supporters"
                Text {
                    x: Theme.spacingLg
                    visible: community.supportersLoading
                    text: qsTr("Loading supporters…")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
                Text {
                    x: Theme.spacingLg
                    visible: !community.supportersLoading &&
                             community.supportersError !== ""
                    text: community.supportersError
                    color: "#e57373"
                    font.pixelSize: 14
                }
                Button {
                    x: Theme.spacingLg
                    visible: !community.supportersLoading &&
                             community.supportersError !== ""
                    text: qsTr("Retry")
                    flat: true
                    onClicked: community.loadSupporters(true)
                }
                Text {
                    x: Theme.spacingLg
                    visible: !community.supportersLoading &&
                             community.supportersError === "" &&
                             community.supporters.length === 0
                    text: qsTr("No supporters found.")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
                Repeater {
                    model: community.supporters
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width - Theme.spacingLg * 2
                        x: Theme.spacingLg
                        height: 56
                        radius: Theme.radiusMd
                        color: Theme.surface
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            spacing: Theme.spacingSm
                            Image {
                                width: 36; height: 36
                                anchors.verticalCenter: parent.verticalCenter
                                source: modelData.avatarUrl || ""
                                fillMode: Image.PreserveAspectCrop
                                visible: (modelData.avatarUrl || "") !== ""
                            }
                            Column {
                                width: parent.width - 60
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    width: parent.width
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: [modelData.membershipLevelDisplay,
                                           modelData.supporterSinceDisplay]
                                        .filter(function(s) {
                                            return String(s).length > 0
                                        }).join(" · ")
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

    Component.onCompleted: {
        community.loadContributors(false)
        community.loadSupporters(false)
    }
}
