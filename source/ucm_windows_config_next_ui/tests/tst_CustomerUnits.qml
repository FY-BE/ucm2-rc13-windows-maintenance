import QtQuick
import QtTest
import "../components/CustomerUnits.js" as CustomerUnits

TestCase {
    name: "CustomerUnits"

    function test_forceUnitsShareOneNewtonValue() {
        compare(CustomerUnits.tonneText("9806.65"), "1.0")
        compare(CustomerUnits.kilonewtonText("9806.65"), "9.8")
    }

    function test_formalMicrostrainRequiresArmValue() {
        compare(CustomerUnits.microstrainText("9806.65", 10), "--")
        compare(CustomerUnits.microstrainText("9806.65", 0), "--")
    }

    function test_invalidForceIsUnavailable() {
        compare(CustomerUnits.tonneText("--"), "--")
        compare(CustomerUnits.kilonewtonText("not-a-number"), "--")
        compare(CustomerUnits.microstrainText("--", 80), "--")
    }
}
