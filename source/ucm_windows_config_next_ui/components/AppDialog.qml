import QtQuick
import QtQuick.Controls.Basic
import "AppPalette.js" as Palette

Dialog {
    id: root
    padding: 20
    background: Rectangle {
        radius: 20
        color: Palette.surface
        border.color: Palette.border
        border.width: 1
    }
}
