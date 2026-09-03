import QtQuick
import QtQuick.Controls
import "../theme"

// Settings > Tracking (T4): Trakt + SIMKL device/PIN auth cards and
// scrobbling status. Client ids ride NUVIO_TRAKT_CLIENT_ID /
// NUVIO_SIMKL_CLIENT_ID (empty = inert, Compose parity). Heavier sync
// (libraries, progress projections, sync engine) rides the engine
// backlog; scrobbling works as soon as a provider connects.
Item {
    id: trackingPage

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingLg
            text: qsTr("Tracking")
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
                width: parent.width - 2 * Theme.spacingLg
                visible: tracking.connectedProviders.length === 0
                text: qsTr("Nothing connected. Scrobbling starts automatically "
                           + "once a provider below signs in.")
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Text {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                visible: tracking.connectedProviders.length > 0
                text: qsTr("Connected: %1. Plays, pauses and completions "
                           + "scrobble automatically.")
                          .arg(tracking.connectedProviders.join(", "))
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            // ---- Trakt ------------------------------------------------
            Rectangle {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                height: traktCol.height + 20
                radius: Theme.radiusMd
                color: Theme.surface

                Column {
                    id: traktCol
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    spacing: 6

                    Text {
                        text: qsTr("Trakt")
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: trakt.authenticated
                              ? qsTr("Signed in — scrobbling plays, pauses "
                                     + "and completions.")
                              : qsTr("Sign in with a device code to scrobble "
                                     + "to Trakt.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    Button {
                        visible: !trakt.authenticated && !trakt.flowActive
                        text: qsTr("Connect with device code")
                        onClicked: trakt.startDeviceFlow()
                    }
                    Column {
                        visible: trakt.flowActive
                        width: parent.width
                        spacing: 4
                        Text {
                            width: parent.width
                            text: qsTr("Enter this code at %1").arg(
                                trakt.verificationUrl.length > 0
                                ? trakt.verificationUrl : "trakt.tv/activate")
                            color: Theme.textPrimary
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                        Text {
                            text: trakt.userCode
                            color: Theme.accent
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            font.letterSpacing: 4
                        }
                        Row {
                            spacing: Theme.spacingSm
                            Button {
                                visible: trakt.verificationUrl.length > 0
                                text: qsTr("Open page")
                                flat: true
                                onClicked: Qt.openUrlExternally(
                                    (trakt.verificationUrl.startsWith("http")
                                     ? "" : "https://")
                                    + trakt.verificationUrl)
                            }
                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                onClicked: trakt.cancelDeviceFlow()
                            }
                        }
                        Text {
                            visible: trakt.busy
                            text: qsTr("Waiting for approval…")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        visible: trakt.errorMessage.length > 0
                        width: parent.width
                        text: trakt.errorMessage
                        color: "#e57373"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Button {
                        visible: trakt.authenticated
                        text: qsTr("Sign out")
                        flat: true
                        onClicked: trakt.signOut()
                    }
                }
            }

            // ---- SIMKL -------------------------------------------------
            Rectangle {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                height: simklCol.height + 20
                radius: Theme.radiusMd
                color: Theme.surface

                Column {
                    id: simklCol
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    spacing: 6

                    Text {
                        text: qsTr("SIMKL")
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: simkl.authenticated
                              ? qsTr("Signed in — scrobbling plays, pauses "
                                     + "and completions.")
                              : qsTr("Sign in with a PIN to scrobble to SIMKL.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    Button {
                        visible: !simkl.authenticated && !simkl.flowActive
                        text: qsTr("Connect with PIN")
                        onClicked: simkl.startPinFlow()
                    }
                    Column {
                        visible: simkl.flowActive
                        width: parent.width
                        spacing: 4
                        Text {
                            width: parent.width
                            text: qsTr("Enter this PIN at %1").arg(
                                simkl.verificationUrl.length > 0
                                ? simkl.verificationUrl : "simkl.com/pin")
                            color: Theme.textPrimary
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                        Text {
                            text: simkl.userCode
                            color: Theme.accent
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            font.letterSpacing: 4
                        }
                        Row {
                            spacing: Theme.spacingSm
                            Button {
                                visible: simkl.verificationUrl.length > 0
                                text: qsTr("Open page")
                                flat: true
                                onClicked: Qt.openUrlExternally(
                                    (simkl.verificationUrl.startsWith("http")
                                     ? "" : "https://")
                                    + simkl.verificationUrl)
                            }
                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                onClicked: simkl.cancelPinFlow()
                            }
                        }
                        Text {
                            visible: simkl.busy
                            text: qsTr("Waiting for approval…")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        visible: simkl.errorMessage.length > 0
                        width: parent.width
                        text: simkl.errorMessage
                        color: "#e57373"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Button {
                        visible: simkl.authenticated
                        text: qsTr("Sign out")
                        flat: true
                        onClicked: simkl.signOut()
                    }
                }
            }

            Text {
                x: Theme.spacingLg
                width: parent.width - 2 * Theme.spacingLg
                text: qsTr("API keys for skip providers sync through your "
                           + "Nuvio account when signed in. Deeper provider "
                           + "sync (libraries, progress) arrives with the "
                           + "sync engine.")
                color: Theme.textDisabled
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
    }
}
