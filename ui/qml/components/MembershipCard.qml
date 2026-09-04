import QtQuick
import QtQuick.Controls
import "../theme"

// Supporter membership card (fork SupporterMembershipCard parity,
// compacted): loading / load-error / active-subscription /
// patreon-connected / grant-or-active / non-member states with
// refresh + manage actions. Used by the Account section and the
// Community page alike. Action behavior mirrors the fork: the primary
// button opens the Patreon memberships page for active subscribers,
// else the (usually unconfigured) donate url, and stays disabled
// without either.
Rectangle {
    id: card

    // Donate url override for tests/previews (blank by default, fork
    // CommunityConfig parity - DONATIONS_DONATE_URL ships blank).
    property string donateUrl: ""
    readonly property string manageUrl:
        "https://www.patreon.com/settings/memberships"
    readonly property var ov: membership.overview || {}
    readonly property bool subActive: ov.subscriptionActive === true
    readonly property bool donateConfigured: card.donateUrl !== ""
    readonly property bool actionEnabled: card.subActive
                                           || card.donateConfigured

    radius: Theme.radiusMd
    color: "#07080B"
    border.color: Theme.border
    border.width: 1
    implicitHeight: cardCol.implicitHeight + 40

    Column {
        id: cardCol
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 12

        Text {
            visible: membership.loading
            text: qsTr("Loading membership…")
            color: "#F6F7F9"
            font.pixelSize: 15
        }
        Text {
            visible: !membership.loading && !ov.status
            text: qsTr("Nuvio Supporter Membership")
            color: "#F6F7F9"
            font.pixelSize: 19
            font.weight: Font.DemiBold
        }
        Text {
            visible: !membership.loading && !ov.status
            text: qsTr("Unable to load membership status. Please try again.")
            color: "#F6F7F9"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            width: parent.width
        }

        // Active subscription / grant / connected / non-member bodies.
        Text {
            visible: !membership.loading && ov.status &&
                     (card.subActive || ov.hasActiveGrant === true ||
                      ov.active === true)
            text: qsTr("You’re a %1. Thank you.").arg(
                ov.membershipLevelDisplay || ov.tierDisplay ||
                qsTr("Supporter"))
            color: "#F6F7F9"
            font.pixelSize: 19
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            width: parent.width
        }
        Text {
            visible: !membership.loading && ov.status &&
                     ov.providerConnected === true &&
                     ov.hasActiveGrant !== true && !card.subActive
            text: qsTr("Patreon is connected")
            color: "#F6F7F9"
            font.pixelSize: 19
            font.weight: Font.DemiBold
        }
        Text {
            visible: !membership.loading && ov.status &&
                     ov.providerConnected === true &&
                     ov.hasActiveGrant !== true && !card.subActive
            text: qsTr("No active Nuvio tier was found on this Patreon account. View the available options or refresh after changing your Patreon membership.")
            color: "#FFFFFF"
            opacity: 0.65
            font.pixelSize: 14
            wrapMode: Text.Wrap
            width: parent.width
        }
        Text {
            visible: !membership.loading && ov.status &&
                     ov.providerConnected !== true && !card.subActive &&
                     ov.hasActiveGrant !== true && ov.active !== true
            text: qsTr("Nuvio Supporter Membership")
            color: "#F6F7F9"
            font.pixelSize: 19
            font.weight: Font.DemiBold
        }
        Text {
            visible: !membership.loading && ov.status &&
                     ov.providerConnected !== true && !card.subActive &&
                     ov.hasActiveGrant !== true && ov.active !== true
            text: qsTr("Supporting Nuvio helps cover infrastructure and ongoing development while keeping the core experience free for everyone.")
            color: "#FFFFFF"
            opacity: 0.65
            font.pixelSize: 14
            wrapMode: Text.Wrap
            width: parent.width
        }
        Text {
            visible: !membership.loading &&
                     (ov.supporterSince || "") !== "" &&
                     (card.subActive || ov.hasActiveGrant === true ||
                      ov.active === true)
            text: qsTr("Supporter since %1.").arg(ov.supporterSince)
            color: "#FFFFFF"
            opacity: 0.52
            font.pixelSize: 14
        }
        Text {
            visible: membership.errorMessage !== ""
            text: qsTr("Unable to load membership status. Please try again.")
            color: "#FF9E9E"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            width: parent.width
        }

        Row {
            spacing: Theme.spacingSm
            Button {
                text: membership.refreshing ? qsTr("Refreshing")
                                            : qsTr("Refresh")
                flat: true
                enabled: !membership.refreshing
                onClicked: membership.refresh()
            }
            Button {
                visible: !membership.loading && ov.status !== undefined
                text: card.subActive ? qsTr("Manage membership")
                                     : qsTr("View Supporter Membership")
                enabled: card.actionEnabled
                onClicked: {
                    const url = card.subActive ? card.manageUrl
                                               : card.donateUrl
                    if (url !== "") Qt.openUrlExternally(url)
                }
            }
        }
    }
}
