// Theme — design tokens ONLY (discipline rule: QML computes nothing here).
pragma Singleton
import QtQuick

QtObject {
    // Surfaces
    readonly property color background:   "#101114"
    readonly property color surface:      "#17181c"
    readonly property color surfaceHigh:  "#1f2127"
    readonly property color chromeScrim:  "#e6121213"

    // Lines / borders
    readonly property color border:       "#2a2d34"

    // Text
    readonly property color textPrimary:  "#e8eaf0"
    readonly property color textSecondary:"#9aa1ad"
    readonly property color textDisabled: "#5c626e"

    // Accent
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
}
