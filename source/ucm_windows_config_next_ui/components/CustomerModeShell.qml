import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

Rectangle {
    id: root
    property var backendObject
    property int currentPage: 0
    property int initialPage: 0
    property real activeRodDiameterMm: 0
    signal engineerRequested()

    color: Palette.canvas

    function selectPage(page) {
        currentPage = page
        if (!backendObject)
            return
        if (page === 2) {
            backendObject.setActivePage(5)
            backendObject.refreshTimeline()
        } else {
            backendObject.setActivePage(0)
        }
    }

    Component.onCompleted: selectPage(Math.max(0, Math.min(2, initialPage)))

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CustomerTopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            backendObject: root.backendObject
            currentPage: root.currentPage
            onPageRequested: function(page) { root.selectPage(page) }
            onEngineerRequested: root.engineerRequested()
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            currentIndex: root.currentPage

            CustomerHomePage {
                backendObject: root.backendObject
                activeRodDiameterMm: root.activeRodDiameterMm
            }
            CustomerSettingsPage {
                backendObject: root.backendObject
                onActiveDiameterConfirmed: function(diameterMm) {
                    root.activeRodDiameterMm = diameterMm
                }
            }
            CustomerInfoPage {
                backendObject: root.backendObject
            }
        }
    }
}
