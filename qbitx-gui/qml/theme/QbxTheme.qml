pragma Singleton
import QtQuick

QtObject {
    readonly property color bgApp: "#0b0f14"
    readonly property color bgSidebar: "#070a0f"
    readonly property color bgContent: "#0d1219"
    readonly property color bgCard: "#141c28"
    readonly property color bgInput: "#1a2433"
    readonly property color bgHover: "#1e2a3d"
    readonly property color bgActive: "#243247"

    readonly property color border: "#2a3a52"
    readonly property color borderLight: "#3d5270"

    readonly property color accent: "#00bcd4"
    readonly property color accentDim: "#0097a7"
    readonly property color accentGlow: "#4dd0e1"

    readonly property color textPrimary: "#e8eef5"
    readonly property color textSecondary: "#9aa8bc"
    readonly property color textMuted: "#6b7a90"

    readonly property color success: "#3ddc84"
    readonly property color error: "#ff6b6b"
    readonly property color warning: "#ffc857"
    readonly property color info: "#64b5f6"

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14

    readonly property int pagePadding: 32
    readonly property int pageMargin: pagePadding
    readonly property int contentMaxWidth: 860
    readonly property int sidebarWidth: 280
    readonly property int controlSpacing: 12
    readonly property int actionButtonHeight: 42
    readonly property int actionButtonMinWidth: 108
    readonly property int cardMinHeight: 220
    readonly property int dashboardCardHeight: 248
    readonly property int labelColumnWidth: 110
    readonly property int formRowHeight: 38
}
