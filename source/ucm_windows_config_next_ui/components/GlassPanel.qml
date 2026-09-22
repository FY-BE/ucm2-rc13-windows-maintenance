import QtQuick
import QtQuick.Effects
import "AppPalette.js" as Palette

Rectangle {
    id: root
    property color glassColor: Palette.surface
    property color strokeColor: Palette.border
    property real cornerRadius: 20
    property bool elevated: true
    // Frequently changing cards avoid an offscreen layer/effect rebuild on
    // every value update. Static surfaces keep the richer soft shadow.
    property bool performanceSensitive: false

    color: glassColor
    radius: cornerRadius
    border.color: strokeColor
    border.width: 1

    layer.enabled: elevated && !performanceSensitive
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#151C1C1E"
        shadowBlur: 0.46
        shadowVerticalOffset: 3
        shadowHorizontalOffset: 0
    }
}
