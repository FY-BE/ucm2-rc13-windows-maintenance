import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

RowLayout {
    id: root
    property string label: ""
    property string symbol: ""
    property string unit: ""
    property string placeholder: ""
    property alias text: editor.text
    property bool numeric: true
    signal edited(string value)

    spacing: 12

    Text {
        Layout.preferredWidth: 128
        text: root.label
        color: Palette.text
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 20
        horizontalAlignment: Text.AlignRight
    }

    AppTextField {
        id: editor
        Layout.fillWidth: true
        Layout.preferredHeight: 52
        placeholderText: root.placeholder
        selectByMouse: true
        inputMethodHints: root.numeric ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
        validator: root.numeric ? decimalValidator : null
        color: "#263240"
        placeholderTextColor: "#A7AAB0"
        font.family: root.numeric ? "Microsoft YaHei UI" : "Microsoft YaHei UI"
        font.pixelSize: 18
        leftPadding: 18
        rightPadding: 58
        background: Rectangle {
            radius: 10
            color: "#FFFFFF"
            border.width: editor.activeFocus ? 1.5 : 1
            border.color: editor.activeFocus ? Palette.blue : Palette.borderStrong
        }
        onTextEdited: root.edited(text)

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.unit
            color: "#989DA5"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 15
        }
    }

    RegularExpressionValidator {
        id: decimalValidator
        regularExpression: /[0-9]{0,6}([.][0-9]{0,3})?/
    }
}
