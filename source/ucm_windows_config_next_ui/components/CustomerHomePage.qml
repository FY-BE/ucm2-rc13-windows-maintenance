import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "CustomerUnits.js" as CustomerUnits
import "EngineerConfig.js" as ProductStatus
import "AppPalette.js" as Palette

Item {
    id: root
    property var backendObject
    property real activeRodDiameterMm: 0
    readonly property var rodColors: [Palette.rod1, Palette.rod2, Palette.rod3, Palette.rod4]

    function rodValue(index) {
        if (!backendObject || !backendObject.rodForceTexts
                || backendObject.rodForceTexts.length <= index)
            return "--"
        return backendObject.rodForceTexts[index]
    }

    function rodState(index) {
        if (!backendObject || !backendObject.rodStateTexts
                || backendObject.rodStateTexts.length <= index)
            return "等待设备数据"
        return backendObject.rodStateTexts[index]
    }

    function rodStrain(index) {
        if (!backendObject || !backendObject.rodStrainTexts
                || backendObject.rodStrainTexts.length <= index)
            return "--"
        return backendObject.rodStrainTexts[index]
    }

    function totalTonneText() {
        return CustomerUnits.tonneText(backendObject
                                      ? backendObject.totalForceText : "--")
    }

    function totalKilonewtonText() {
        return CustomerUnits.kilonewtonText(backendObject
                                           ? backendObject.totalForceText : "--")
    }

    function totalMicrostrainText() {
        return CustomerUnits.microstrainText(backendObject
                                             ? backendObject.totalForceText : "--",
                                             activeRodDiameterMm)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label {
            objectName: "monitoringCadenceStatus"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: ProductStatus.monitoringStatus(Boolean(root.backendObject && root.backendObject.offlinePreview), Boolean(root.backendObject && root.backendObject.connected), Boolean(root.backendObject && root.backendObject.telemetryReady))
            color: Palette.secondary
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(500, Math.max(350, root.height * 0.48))
            Layout.maximumHeight: Math.min(500, Math.max(350, root.height * 0.48))
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1.30
                color: "#FFFFFF"
                border.color: Palette.border
                radius: 20

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        Rectangle { width: 4; height: 20; radius: 2; color: Palette.blue }
                        Text {
                            text: "四杆实时状态"
                            color: Palette.text
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    ColumnLayout {
                        Layout.preferredWidth: 170
                        Layout.minimumWidth: 150
                        Layout.fillHeight: true
                        spacing: 10
                        CustomerRodMetric {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            rodNumber: 3
                            newtonsText: root.rodValue(2)
                            microstrainValueText: root.rodStrain(2)
                            stateText: root.rodState(2)
                            rodDiameterMm: root.activeRodDiameterMm
                            accent: root.rodColors[2]
                        }
                        CustomerRodMetric {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            rodNumber: 4
                            newtonsText: root.rodValue(3)
                            microstrainValueText: root.rodStrain(3)
                            stateText: root.rodState(3)
                            rodDiameterMm: root.activeRodDiameterMm
                            accent: root.rodColors[3]
                        }
                    }

                    Image {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 230
                        source: "../assets/customer-machine-front.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        opacity: 0.76
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 170
                        Layout.minimumWidth: 150
                        Layout.fillHeight: true
                        spacing: 10
                        CustomerRodMetric {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            rodNumber: 2
                            newtonsText: root.rodValue(1)
                            microstrainValueText: root.rodStrain(1)
                            stateText: root.rodState(1)
                            rodDiameterMm: root.activeRodDiameterMm
                            accent: root.rodColors[1]
                        }
                        CustomerRodMetric {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            rodNumber: 1
                            newtonsText: root.rodValue(0)
                            microstrainValueText: root.rodStrain(0)
                            stateText: root.rodState(0)
                            rodDiameterMm: root.activeRodDiameterMm
                            accent: root.rodColors[0]
                        }
                    }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 0.70
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 1.18
                    color: "#FFFFFF"
                    border.color: Palette.border
                    radius: 20

                    Column {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 9
                        Text {
                            text: "总锁模力"
                            color: Palette.text
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                        Row {
                            spacing: 7
                            Text {
                                text: root.totalTonneText()
                                color: Palette.text
                                font.family: "Segoe UI Variable Display"
                                font.pixelSize: 40
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.baseline: parent.children[0].baseline
                                text: "T"
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 17
                            }
                        }
                        Row {
                            spacing: 24
                            Text {
                                text: root.totalKilonewtonText() + " kN"
                                color: Palette.tertiary
                                font.family: "Segoe UI Variable Display"
                                font.pixelSize: 15
                            }
                            Text {
                                text: root.totalMicrostrainText() + " με"
                                color: Palette.tertiary
                                font.family: "Segoe UI Variable Display"
                                font.pixelSize: 15
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#FFFFFF"
                    border.color: Palette.border
                    radius: 20

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        spacing: 22
                        Column {
                            spacing: 3
                            Text {
                                text: "偏载率"
                                color: Palette.text
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: root.backendObject ? root.backendObject.processStateText : "待机"
                                color: "#7F8A97"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 12
                            }
                            Text {
                                width: 250
                                text: root.backendObject
                                      ? root.backendObject.eventText : "等待设备状态"
                                color: Palette.tertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: (root.backendObject ? root.backendObject.imbalanceText : "--") + " %"
                            color: Palette.text
                            font.family: "Segoe UI Variable Display"
                            font.pixelSize: 37
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        CustomerTrendPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            trendSource: root.backendObject ? root.backendObject.trendBuffer : null
        }
    }
}
