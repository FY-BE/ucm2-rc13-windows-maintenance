import QtQuick
import QtTest
import "../components"

TestCase {
    name: "FitLivePlot"
    when: windowShown
    width: 1200
    height: 650

    Component {
        id: plotComponent
        FitLivePlot { width: 1050; height: 500; report: ({}) }
    }
    function sample(stage, time, force) {
        const g = 10
        const kmat = 1e-11
        return {stage:stage, camera_utc_ms:time, arm_utc_ms:time + 30,
                force_kN:[force,force + 1,force + 2,force + 3],
                ncc_delta_ns:[0,1,2,3].map(function(_b, rod) {
                    return 2 * kmat * g * 1e12 * (force + rod)
                }), g:g, accepted:true, rejection_reason:"", pair_gap_ms:30}
    }
    function test_independentCouplingIsDeductedOnce() {
        const plot = createTemporaryObject(plotComponent, this)
        plot.report = {status:"candidate", kmat_unified:1e-11, coupling_bias_ns:[10,20,30,40]}
        for (let rod=0; rod<4; ++rod) {
            compare(plot.modeledForceAt(2000 + 10*(rod+1), 10, rod), 10)
            compare(plot.modeledForceAt(10*(rod+1), 10, rod), 0)
        }
    }
    function test_detTOnXAxisAndForceOnYAxis() {
        const plot = createTemporaryObject(plotComponent, this)
        verify(plot !== null)
        const report = {preview:true, status:"candidate", fit_constraint:"shared_kmat_shared_b",
            kmat_unified:1e-11, b_shared_ns:0, coupling_bias_ns:[0,0,0,0], accepted_stages:2,
            display_fits_by_rod:[0,1,2,3].map(function(rod) {
                return {rod:rod,status:"diagnostic",kmat:1e-11,b_ns:rod * 10,dispatch:false}
            }),
            stages:[{start_utc_ms:1000,end_utc_ms:4000},
                    {start_utc_ms:5000,end_utc_ms:8000}],
            representatives:[{stage:1,rod:0,dt_ns:2000,raw_dt_ns:2000,force_kN:10,g:10}],
            plot_samples:[sample(1,2500,10), sample(1,1500,12),
                          sample(2,5500,20), sample(2,6500,22)]}
        plot.report = report
        compare(plot.modeledForce(report.plot_samples[0], 0), 10)
        compare(plot.modeledForce(report.plot_samples[2], 3), 23)
        report.force_correction = {model:"MONOTONE_PWL_ZERO_V1",knot_count:3,
            input_force_n:[0,10000,20000],output_force_n:[0,11000,25000]}
        plot.report = Object.assign({}, report)
        compare(plot.correctedForceKn(0), 0)
        compare(plot.correctedForceKn(10), 11)
        compare(plot.correctedForceKn(15), 18)
        delete report.force_correction
        plot.report = Object.assign({}, report)
        compare(plot.diagnosticForceAt(report.plot_samples[0].ncc_delta_ns[1], 10, 1), 10.95)
        tryVerify(function() { return plot.hitPoints.length === 17 })
        const cameraPoints = plot.hitPoints.filter(function(point) {
            return point.kind === "sample" && point.rod === 0
        })
        verify(cameraPoints[0].delay < cameraPoints[1].delay)
        verify(report.plot_samples[0].camera_utc_ms > report.plot_samples[1].camera_utc_ms)
        verify(cameraPoints[0].x < cameraPoints[1].x)
        verify(cameraPoints[0].y > cameraPoints[1].y)
        report.plot_samples[0].accepted = false
        report.plot_samples[0].rejection_reason = "outlier"
        plot.report = Object.assign({}, report)
        tryVerify(function() {
            return plot.hitPoints.some(function(point) {
                return point.kind === "sample" && point.rod === 0
                    && !point.accepted && point.rejectionReason === "outlier"
            })
        })
        report.status = "insufficient_data"
        plot.report = Object.assign({}, report)
        tryVerify(function() { return plot.hitPoints.length === 17 })
    }
}
