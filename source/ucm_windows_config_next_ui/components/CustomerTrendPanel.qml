import QtQuick

Item {
    id: root
    property var trendSource

    TrendCard {
        anchors.fill: parent
        title: "四杆实时趋势 · 最近 1 分钟"
        source: root.trendSource
    }
}
