import QtQuick
import "AppPalette.js" as Palette

GlassPanel {
    id: root
    performanceSensitive: true
    property string title: "四杆力趋势"
    property var source: null
    property var samples: []
    property real cursorRatio: 0.5
    property bool cursorLocked: false
    readonly property var selectedSample: {
        const revision = plot.revision
        return plot.sampleAtRatio(cursorRatio)
    }
    readonly property bool hasData: {
        const revision = plot.revision
        return Object.keys(plot.sampleAtRatio(0.5)).length > 0
    }
    readonly property var colors: [Palette.rod1, Palette.rod2, Palette.rod3, Palette.rod4]

    function valueText(rod) {
        if (!selectedSample || Object.keys(selectedSample).length === 0
                || !(selectedSample.validMask & (1 << rod)))
            return "--"
        return Number(selectedSample.values[rod]).toFixed(2) + " N"
    }

    function relativeTimeText() {
        if (!selectedSample || Object.keys(selectedSample).length === 0)
            return ""
        const newest = plot.sampleAtRatio(1.0)
        if (!newest || Object.keys(newest).length === 0)
            return ""
        return ((Number(selectedSample.timestampMs)
                 - Number(newest.timestampMs)) / 1000.0).toFixed(2) + " s"
    }

    Text {
        id: titleLabel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 22
        text: root.title
        color: Palette.text
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 17
        font.weight: Font.DemiBold
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 22
        spacing: 14
        Repeater {
            model: root.colors
            delegate: Row {
                spacing: 5
                Rectangle {
                    width: 16
                    height: 3
                    radius: 2
                    color: modelData
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "杆" + (index + 1)
                    color: "#718095"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 11
                }
            }
        }
    }

    Item {
        anchors.fill: plot
        visible: !root.hasData
        Repeater {
            model: 7
            delegate: Rectangle {
                x: index * (parent.width - 1) / 6
                width: 1
                height: parent.height
                color: "#E2E8F0"
            }
        }
        Repeater {
            model: 5
            delegate: Rectangle {
                y: index * (parent.height - 1) / 4
                width: parent.width
                height: 1
                color: "#E2E8F0"
            }
        }
    }

    TrendPlotItem {
        id: plot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleLabel.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 22
        anchors.topMargin: 18
        source: root.source
        samples: root.samples
        cursorRatio: root.cursorRatio
        cursorVisible: cursorArea.containsMouse || root.cursorLocked

        MouseArea {
            id: cursorArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            onPositionChanged: function(mouse) {
                if (!root.cursorLocked)
                    root.cursorRatio = Math.max(0, Math.min(1, mouse.x / width))
            }
            onClicked: function(mouse) {
                if (!root.hasData)
                    return
                root.cursorRatio = Math.max(0, Math.min(1, mouse.x / width))
                root.cursorLocked = !root.cursorLocked
            }
        }
    }

    Rectangle {
        visible: root.hasData && (cursorArea.containsMouse || root.cursorLocked)
        x: plot.x + root.cursorRatio * plot.width
        y: plot.y
        width: 1
        height: plot.height
        color: "#8A97A8"
        z: 1
    }

    Column {
        visible: !root.hasData
        anchors.centerIn: plot
        spacing: 5
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "等待 ARM 实时数据"
            color: Palette.secondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "无数据时不绘制模拟曲线"
            color: "#8B97A7"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 12
        }
    }

    GlassPanel {
        visible: root.selectedSample
                 && Object.keys(root.selectedSample).length > 0
                 && (cursorArea.containsMouse || root.cursorLocked)
        width: 188
        height: 126
        cornerRadius: 16
        glassColor: Palette.surface
        x: Math.min(Math.max(plot.x + root.cursorRatio * plot.width + 12,
                             plot.x), plot.x + plot.width - width)
        y: plot.y + 10
        z: 2

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 5
            Text {
                text: (root.cursorLocked ? "已锁定" : "跟随")
                      + "  t " + root.relativeTimeText()
                color: Palette.text
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Repeater {
                model: 4
                delegate: Text {
                    text: "杆" + (index + 1) + "   " + root.valueText(index)
                    color: root.colors[index]
                    font.family: "Cascadia Mono"
                    font.pixelSize: 11
                }
            }
        }
    }
}
