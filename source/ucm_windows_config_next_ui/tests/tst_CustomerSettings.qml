import QtQuick
import QtTest
import "../components/CustomerSettings.js" as CustomerSettings

TestCase {
    name: "CustomerSettings"

    function test_customerFieldMapping() {
        const value = CustomerSettings.customerCandidate(
                    "UCM-1200", "2500", "1800", "120", "140", "90")
        compare(value.model_name, "UCM-1200")
        compare(value.l_total_mm, 2500)
        compare(value.l_b_mm, 1800)
        compare(value.l_d_mm, 120)
        compare(value.l_e_mm, 140)
        verify(value.rod_diameter_mm === undefined)
    }

    function test_differenceListsRespectCustomerScopeAndDirection() {
        const current = {model_name: "DE168", l_total_mm: 2500, l_b_mm: 90, l_d_mm: 90, l_e_mm: 40, rod_diameter_mm: [90,90,90,90], phi_b: 0.8}
        const basic = CustomerSettings.basicDocument(current)
        const draft = CustomerSettings.customerCandidate("DE168", 2500, 91, 90, 40, 90)
        const changes = CustomerSettings.fieldDifferences(basic, draft, CustomerSettings.basicKeys)
        compare(changes.length, 1)
        compare(changes[0].field, "l_b_mm")
        compare(changes[0].before, "90")
        compare(changes[0].after, "91")
        const allChanges = CustomerSettings.fieldDifferences({phi_b: 0.8, extra: 1}, {phi_b: 0.46})
        compare(allChanges.length, 2)
        compare(allChanges[0].field, "extra")
        compare(allChanges[0].after, "--（缺失）")
    }

    function test_geometryValidation() {
        const valid = CustomerSettings.customerCandidate(
                    "UCM-1200", 2500, 1800, 120, 140, 90)
        compare(CustomerSettings.validationError(valid), "")
        const invalid = CustomerSettings.customerCandidate(
                    "UCM-1200", 2000, 1800, 120, 140, 90)
        verify(CustomerSettings.validationError(invalid).length > 0)
    }
}
