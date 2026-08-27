// Theme — design tokens ONLY (discipline rule: QML computes nothing here).
// The single branch the singleton owns is dark/light switching, driven by
// the persisted AppSettings property; spacing/motion are mode-invariant.
pragma Singleton
import QtQuick

QtObject {
    // Dark when unset (Compose out-of-box feel). Reads the engine context
    // object defensively so any consumer that has no `appsettings` context
    // (tests, other engines) still gets a valid, dark-by-default token set.
    readonly property bool dark:
        (typeof appsettings !== "undefined" &&
         appsettings !== null && appsettings.darkTheme !== undefined)
            ? appsettings.darkTheme : true

    readonly property color cBg:     dark ? "#101114" : "#f5f5f7"
    readonly property color cSurf:   dark ? "#17181c" : "#ffffff"
    readonly property color cSurfHi: dark ? "#1f2127" : "#eceef2"
    readonly property color cScrim:  dark ? "#e6121213" : "#e6ffffff"
    readonly property color cBorder: dark ? "#2a2d34" : "#d8dbe2"
    readonly property color cTextP:  dark ? "#e8eaf0" : "#16181d"
    readonly property color cTextS:  dark ? "#9aa1ad" : "#5b6270"
    readonly property color cTextD:  dark ? "#5c626e" : "#a4abb8"

    // Surfaces
    readonly property color background:   cBg
    readonly property color surface:      cSurf
    readonly property color surfaceHigh:  cSurfHi
    readonly property color chromeScrim:  cScrim

    // Lines / borders
    readonly property color border:       cBorder

    // Text
    readonly property color textPrimary:  cTextP
    readonly property color textSecondary:cTextS
    readonly property color textDisabled: cTextD

    // Accent (brand constant across modes)
    readonly property color accent:         "#82aaff"
    readonly property color accentPressed:  "#6b93dd"

    // Motion
    readonly property int fadeMs:   180
    readonly property int slideMs:  220

    // Geometry
    readonly property int radiusMd: 12
    readonly property int barHeight: 52
    readonly property int spacingSm: 8
    readonly property int spacingMd: 14
    readonly property int spacingLg: 22
}
