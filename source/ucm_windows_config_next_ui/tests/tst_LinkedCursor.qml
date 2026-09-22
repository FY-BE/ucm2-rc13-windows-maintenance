import QtQuick
import QtTest
import "../components"

TestCase {
    id: testCase
    name: "NextUiLinkedFourRodCursor"
    width: 900
    height: 700
    visible: true
    when: windowShown

    WaveformCard {
        id: waveform
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 680
        windowStart: 4096
        sampleRateHz: 100000000
        series: [
            [0, 100, 200, 100, 0, -100, -200, -100],
            [20, 120, 220, 120, 20, -80, -180, -80],
            [40, 140, 240, 140, 40, -60, -160, -60],
            [60, 160, 260, 160, 60, -40, -140, -40]
        ]
    }

    function test_waveform_shared_sample_cursor() {
        waitForRendering(waveform)
        mouseMove(waveform, 300, 180)
        verify(waveform.cursorSample >= 0)
        mouseClick(waveform, 300, 180, Qt.LeftButton)
        verify(waveform.cursorLocked)
        const lockedSample = waveform.cursorSample
        mouseMove(waveform, 700, 180)
        compare(waveform.cursorSample, lockedSample)
        verify(waveform.adcText(0).indexOf("ADC") > 0)
        mouseClick(waveform, 700, 180, Qt.LeftButton)
        verify(!waveform.cursorLocked)
    }
}
