import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "EngineerConfig.js" as Config
import "AppPalette.js" as Palette

ColumnLayout {
    id: root
    property var descriptor: ({})
    property var value
    property bool editable: false
    signal edited(string key, string value)
    spacing: 5
    RowLayout {
        Layout.fillWidth: true
        Label { text: root.descriptor.label || ""; color: Palette.secondary; font.pixelSize: 13; Layout.fillWidth: true }
        Label { text: root.descriptor.unit || ""; color: Palette.tertiary; font.pixelSize: 11 }
    }
    AppTextField {
        visible: root.descriptor.kind!=="enum"
        Layout.fillWidth: true
        Layout.preferredHeight: 38
        enabled: root.editable
        text: root.value===undefined ? "" : Config.display(root.value)
        placeholderText: "载入当前文档后编辑"
        selectByMouse: true
        color: "#243B57"
        onEditingFinished: if(root.editable) root.edited(root.descriptor.key,text)
        background: Rectangle { color: parent.enabled ? "#F7FAFE" : "#F2F5F8"; radius: 6; border.color: parent.activeFocus ? "#387AC3" : Palette.border }
    }
    AppComboBox {
        visible: root.descriptor.kind==="enum"
        Layout.fillWidth: true
        Layout.preferredHeight: 38
        enabled: root.editable
        model: root.descriptor.options || []
        textRole: "label"
        currentIndex: { const items=root.descriptor.options || []; for(let i=0;i<items.length;i++) if(items[i].value===root.value) return i; return -1 }
        displayText: currentIndex<0 ? "载入当前文档后选择" : currentText
        onActivated: function(index) { root.edited(root.descriptor.key,model[index].value) }
    }
    Label { text: root.descriptor.hint || ""; visible: text.length>0; font.pixelSize: 10; color: Palette.tertiary; Layout.fillWidth: true; wrapMode: Text.Wrap }
}
