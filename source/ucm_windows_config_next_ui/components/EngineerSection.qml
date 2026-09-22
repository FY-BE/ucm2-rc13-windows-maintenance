import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Rectangle {
    id: root
    property string title: ""
    property string description: ""
    default property alias content: body.data
    color: "#FFFFFF"
    border.color: Palette.border
    radius: 10
    implicitHeight: layout.implicitHeight + 36
    data: ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        Label { text: root.title; color: Palette.text; font.family: "Microsoft YaHei UI"; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { visible: root.description.length>0; text: root.description; color: Palette.secondary; font.family: "Microsoft YaHei UI"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
        ColumnLayout { id: body; Layout.fillWidth: true; spacing: 10 }
    }
}
