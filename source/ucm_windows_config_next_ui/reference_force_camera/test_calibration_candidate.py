from __future__ import annotations

import hashlib
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from calibration_candidate import fit_session, geometry, _pwl_value
from calibration_candidate import main as fit_main
from reference_force_bridge import _check_image_size
from reference_force_bridge import VENDOR_ROOT
from reference_force_bridge import emit_json
from reference_force_bridge import main as bridge_main
from reference_force_bridge import recognize_image


MODEL = {
    "schema_version": 3, "geometry_model": "BODY_REFERENCE_V1",
    "device_model_id": 1, "model_name": "A001", "coupling_bias_ns": [0.0] * 4,
    "mold_reference_mm": 100, "body_reference_mm": 100,
    "l_total_mm": 1000, "l_b_mm": 100, "l_d_mm": 100,
    "l_e_mm": 100, "abeq_mm2": 100, "ac_mm2": 100,
    "adeq_mm2": 100, "phi_b": 1, "phi_d": 1,
}
GW_MODEL = {
    "schema_version": 3, "geometry_model": "GW_DRAWING_FE_ENGINEERING_V1",
    "device_model_id": 37, "model_name": "GW1850R", "pack_id": "GW1850R-R2S",
    "coupling_bias_ns": [0.0] * 4, "mold_reference_mm": 662,
    "fixed_mold_thickness_mm": 662, "body_reference_mm": 0,
    "l_total_mm": 5230, "l_b_mm": 0, "l_d_mm": 0, "l_e_mm": 0,
    "abeq_mm2": 53816.963, "ac_mm2": 61575.216, "adeq_mm2": 53816.963,
    "phi_b": 0, "phi_d": 0, "rod_diameter_mm": [280.0] * 4,
    "thread_root_diameter_mm": 250,
}
IDENTITY = {"sha256": "known-model"}
K = 1e-11
B = [10.0] * 4


def make_session(folder: Path, *, stages=3, drift=False, invalid_mask=False,
                 clock_jump=False, same_load=False, b_by_rod=None,
                 samples_per_stage=30, sample_step_ms=100, pair_offset_ms=30,
                 quadratic_delay=0.0, model=None, mold_mm=100.0) -> None:
    model = MODEL if model is None else model
    (folder / "camera").mkdir()
    (folder / "context.json").write_text(json.dumps({
        "active_model": model, "active_model_identity": IDENTITY,
        "roi_sha256": hashlib.sha256(b"rois").hexdigest()}), encoding="utf-8")
    (folder / "confirmed-rois.json").write_bytes(b"rois")
    (folder / "session-state.json").write_text(json.dumps({"status": "complete"}), encoding="utf-8")
    base = 1_700_000_000_000
    camera, arm, stage_rows = [], [], []
    g = geometry(model, mold_mm)
    for stage in range(stages):
        start = base + stage * 5000
        stage_rows.append({"start_utc_ms": start, "end_utc_ms": start + 4000})
        for index in range(samples_per_stage):
            utc = start + 550 + index * sample_step_ms
            force = [float(10 + (0 if same_load else stage * 10) + rod)
                     for rod in range(4)]
            if drift:
                force = [value + index * 0.1 for value in force]
            delays = [K * 2 * g * 1e9 *
                      (value + quadratic_delay * value * value) * 1000 +
                      (b_by_rod or B)[rod]
                      for rod, value in enumerate(force)]
            image = folder / "camera" / f"{stage}-{index}.png"
            image.write_bytes(f"image-{stage}-{index}".encode())
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            camera.append({"sample_id": f"{stage}-{index}", "timestamp_ms": utc,
                           "monotonic_ns": utc * 1_000_000,
                           "status": "ok", "confidence": 0.99, "force_kN": force,
                           "image": str(image.relative_to(folder)).replace("\\", "/"),
                           "image_sha256": digest})
            arm.append({"observed_utc_ms": utc + pair_offset_ms + (500 if clock_jump and stage == 1 and index == 2 else 0),
                        "observed_monotonic_ns": (utc + pair_offset_ms) * 1_000_000,
                        "generation": "1", "published_monotonic_ns": str(utc),
                        "session_id": "1", "sequence": str(stage * samples_per_stage + index),
                        "frame_counter": str(stage * samples_per_stage + index),
                        "capture_request_id": "1", "measurement_mask": 0 if invalid_mask else 15,
                        "ncc_delta_ns": delays, "active_model_identity": IDENTITY,
                        "effective_thickness_um": int(round(mold_mm * 1000.0))})
    (folder / "stages.json").write_text(json.dumps({"stages": stage_rows}), encoding="utf-8")
    for name, rows in (("camera.jsonl", camera), ("arm.jsonl", arm)):
        (folder / name).write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")


class CandidateTests(unittest.TestCase):
    def test_coupling_is_preserved_and_deducted_once_before_fit_and_pwl(self):
        coupling = [5.0, -2.0, 8.0, 1.0]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, stages=6, quadratic_delay=0.002)
            context_path = path / "context.json"
            context = json.loads(context_path.read_text(encoding="utf-8"))
            context["active_model"]["coupling_bias_ns"] = coupling
            context_path.write_text(json.dumps(context), encoding="utf-8")
            self.change_rows(path, "arm.jsonl", lambda row: row.update(
                ncc_delta_ns=[value + coupling[rod] for rod, value in enumerate(row["ncc_delta_ns"])]))
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            self.assertEqual(result["coupling_bias_ns"], coupling)
            self.assertNotIn("b_total_ns", result)
            self.assertNotIn("b_total_ns", result["standard_uncertainty"])
            curve = result["force_correction"]
            errors_once, errors_twice = [], []
            for row in result["representatives"]:
                divisor = 2.0 * result["kmat_unified"] * row["g"] * 1e9
                self.assertAlmostEqual(row["raw_dt_ns"] - row["coupling_bias_ns"], row["dt_ns"])
                once = row["dt_ns"] / divisor
                twice = (row["dt_ns"] - row["coupling_bias_ns"]) / divisor
                target = row["force_kN"] * 1000.0
                errors_once.append((_pwl_value(curve["input_force_n"], curve["output_force_n"], once) - target) ** 2)
                errors_twice.append((_pwl_value(curve["input_force_n"], curve["output_force_n"], twice) - target) ** 2)
            self.assertLess(sum(errors_once), sum(errors_twice))

    def test_arm391_fixed_geometry_ignores_plc_mold_change(self):
        model = dict(MODEL, abeq_mm2=6361.725123519331,
                     ac_mm2=6361.725123519331, adeq_mm2=6361.725123519331,
                     l_b_mm=90, l_d_mm=90, body_reference_mm=500, phi_b=0.46, phi_d=0.46)
        expected = 1000.0 * (90 * 0.46 + 500 + 90 * 0.46) / 6361.725123519331
        self.assertAlmostEqual(geometry(model, 100), expected)
        self.assertEqual(geometry(model, 100), geometry(model, 800))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path)
            self.change_rows(path, "arm.jsonl", lambda row: row.update(effective_thickness_um=120000)
                             if int(row["sequence"]) >= 30 else None)
            self.assertEqual(fit_session(path)["status"], "candidate")

    def test_gw1850r_drawing_fe_geometry_and_fit(self):
        expected = 1000.0 * (1320.0 / 61575.216 +
                             (662.0 + 490.0 - 0.65 * 288.0) / 53816.963)
        self.assertAlmostEqual(geometry(GW_MODEL, 662.0), expected, places=12)
        self.assertAlmostEqual(geometry(GW_MODEL, 663.0) - geometry(GW_MODEL, 662.0),
                               1000.0 / 53816.963, places=12)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, stages=4, model=GW_MODEL, mold_mm=662.0)
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            self.assertEqual(result["geometry_credit"]["geometry_model"],
                             "GW_DRAWING_FE_ENGINEERING_V1")
            self.assertAlmostEqual(result["kmat_unified"] / K, 1.0, places=7)

    def test_old_offsets_and_incomplete_gw_context_do_not_form_dispatch_candidate(self):
        cases = ({"b_total_ns": [10.0] * 4},
                 {"relative_delay_intercept_ns": 10.0},
                 {"coupling_bias_ns": [0, 0, 0]},
                 {"device_model_id": 37, "model_name": "GW1850R",
                  "geometry_model": "GW_DRAWING_FE_ENGINEERING_V1"},
                 {"device_model_id": 168, "model_name": "A001"})
        for extra in cases:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                make_session(path)
                context_path = path / "context.json"
                context = json.loads(context_path.read_text(encoding="utf-8"))
                context["active_model"].update(extra)
                context_path.write_text(json.dumps(context), encoding="utf-8")
                result = fit_session(path)
                self.assertEqual(result["status"], "insufficient_data")
                self.assertTrue(result["reason"])

    def test_preview_reports_total_and_other_without_changing_rod_gate(self):
        sys.path.insert(0, str(VENDOR_ROOT))
        from windows.force_input_camera import image_io

        with tempfile.TemporaryDirectory() as directory:
            preview = Path(directory) / "preview.bmp"
            preview.write_bytes(b"recorded-image")
            output = io.StringIO()
            readouts = [
                ({"status": "ok", "values": {name: str(index)
                    for index, name in enumerate(("S1", "S2", "S3", "S4"), 1)}}, 0.95),
                ({"status": "need_check", "values": {"Total": "8?003"}}, 0.24),
                ({"status": "ok", "values": {"Other": "091"}}, 0.93),
            ]
            args = SimpleNamespace(recognize_image=str(preview), rois=None, config=None)
            with patch("reference_force_bridge.verify_frozen_vendor"), \
                 patch.object(image_io, "imread_unicode", return_value=SimpleNamespace(shape=(1024, 1280, 3))), \
                 patch("reference_force_bridge._load_recognizer", return_value=({}, {})) as load, \
                 patch("reference_force_bridge._check_image_size"), \
                 patch("reference_force_bridge._recognize", side_effect=readouts), \
                 redirect_stdout(output):
                self.assertEqual(recognize_image(args), 0)
            payload = json.loads(output.getvalue())
            self.assertEqual(payload["status"], "ok")
            self.assertEqual(payload["confidence"], 0.95)
            self.assertEqual(payload["values"]["Total"], "8?003")
            self.assertEqual(payload["values"]["Other"], "091")
            self.assertEqual(payload["auxiliary"]["Total"], {"status": "need_check", "confidence": 0.24})
            self.assertEqual(payload["auxiliary"]["Other"], {"status": "ok", "confidence": 0.93})
            self.assertEqual([call.args[2] for call in load.call_args_list[1:]], ["Total", "Other"])

    def test_bridge_json_is_utf8_safe_under_windows_codepage(self):
        buffer = io.BytesIO()
        writer = io.TextIOWrapper(buffer, encoding="gbk")
        payload = {"image": "C:\\测试目录\\预览.bmp", "message": "相机已连接"}
        emit_json(payload, stream=writer)
        wire = buffer.getvalue()
        self.assertTrue(wire.isascii())
        self.assertEqual(json.loads(wire.decode("utf-8")), payload)

    @staticmethod
    def change_rows(path: Path, name: str, change) -> None:
        file = path / name
        rows = [json.loads(line) for line in file.read_text(encoding="utf-8").splitlines()]
        for row in rows:
            change(row)
        file.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")

    def test_known_coefficients_and_images(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path)
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            self.assertAlmostEqual(result["kmat_unified"] / K, 1.0, places=7)
            self.assertAlmostEqual(result["b_shared_ns"], B[0], places=4)
            self.assertEqual(result["fit_constraint"], "shared_kmat_shared_b")
            self.assertEqual(result["schema"], "ucm-windows-calibration-candidate/v3")
            self.assertNotIn("b_total_ns", result)
            self.assertEqual(result["coupling_bias_ns"], MODEL["coupling_bias_ns"])
            self.assertEqual(result["force_correction"]["model"], "MONOTONE_PWL_ZERO_V1")
            self.assertGreaterEqual(result["force_correction"]["knot_count"], 3)
            self.assertEqual(len(result["display_fits_by_rod"]), 4)
            self.assertTrue(all(row["dispatch"] is False for row in result["display_fits_by_rod"]))
            self.assertEqual(len(result["used_images"]), 90)
            self.assertEqual(result["independent_validation"]["tested_values"], 12)
            self.assertEqual(len(result["representatives"]), 12)
            self.assertEqual(len(result["plot_samples"]), 90)
            self.assertTrue(all(point["accepted"] for point in result["plot_samples"]))
            self.assertEqual(result["input_counts"], {"camera_frames": 90,
                                                        "arm_frames": 90, "marked_stages": 3})
            for point in result["plot_samples"]:
                for rod in range(4):
                    force = ((point["ncc_delta_ns"][rod] - result["b_shared_ns"])
                             / (2 * result["kmat_unified"] * point["g"] * 1e12))
                    self.assertAlmostEqual(force, point["force_kN"][rod], places=6)

    def test_nonlinear_response_generates_monotone_zero_anchored_correction(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, stages=6, quadratic_delay=0.002)
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            correction = result["force_correction"]
            self.assertEqual(correction["model"], "MONOTONE_PWL_ZERO_V1")
            self.assertLessEqual(correction["knot_count"], 8)
            self.assertEqual(correction["input_force_n"][0], 0.0)
            self.assertEqual(correction["output_force_n"][0], 0.0)
            self.assertTrue(all(b > a for a, b in zip(
                correction["input_force_n"], correction["input_force_n"][1:])))
            self.assertTrue(all(b > a for a, b in zip(
                correction["output_force_n"], correction["output_force_n"][1:])))
            self.assertLess(correction["representative_corrected_rmse_n"],
                            correction["representative_base_rmse_n"])

    def test_manual_reference_session_does_not_require_roi_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path)
            context = json.loads((path / "context.json").read_text(encoding="utf-8"))
            context["reference_source"] = "manual"
            context["roi_sha256"] = ""
            (path / "context.json").write_text(json.dumps(context), encoding="utf-8")
            (path / "confirmed-rois.json").unlink()
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))

    def test_independent_rod_lines_never_replace_common_dispatch_offset(self):
        offsets = [10.0, -5.0, 20.0, 3.0]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, b_by_rod=offsets)
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            self.assertEqual(result["coupling_bias_ns"], MODEL["coupling_bias_ns"])
            self.assertFalse(result["fitted_intercept_dispatch"])
            for row, expected in zip(result["display_fits_by_rod"], offsets):
                self.assertEqual(row["status"], "diagnostic")
                self.assertFalse(row["dispatch"])
                self.assertAlmostEqual(row["kmat"] / K, 1.0, places=7)
                self.assertAlmostEqual(row["b_ns"], expected, places=4)

    def test_mad_outlier_is_retained_as_crossable_plot_point(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, samples_per_stage=150, sample_step_ms=20, pair_offset_ms=1)
            self.change_rows(path, "camera.jsonl", lambda row: row["force_kN"].__setitem__(0, 50.0)
                             if row["sample_id"] == "0-1" else None)
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            marked = [point for point in result["plot_samples"]
                      if point["camera_sample_id"] == "0-1"]
            self.assertEqual(len(marked), 1)
            self.assertFalse(marked[0]["accepted"])
            self.assertEqual(marked[0]["rejection_reason"], "outlier")
            self.assertGreaterEqual(result["rejections"]["outlier"], 1)

    def test_live_preview_never_writes_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path)
            (path / "session-state.json").write_text(json.dumps({"status": "recording"}), encoding="utf-8")
            stages = json.loads((path / "stages.json").read_text(encoding="utf-8"))["stages"]
            (path / "preview-stages.json").write_text(json.dumps({"stages": stages[:1]}), encoding="utf-8")
            one = fit_session(path, preview=True)
            self.assertEqual(one["status"], "insufficient_data")
            self.assertEqual(len(one["plot_samples"]), 30)
            self.assertEqual(len(one["representatives"]), 4)
            (path / "preview-stages.json").write_text(json.dumps({"stages": stages[:2]}), encoding="utf-8")
            output = io.StringIO()
            with patch.object(sys, "argv", ["fit", "--fit-session", str(path), "--preview-fit"]), redirect_stdout(output):
                self.assertEqual(fit_main(), 0)
            preview = json.loads(output.getvalue())
            self.assertTrue(preview["preview"])
            self.assertEqual(preview["status"], "candidate")
            self.assertEqual(len(preview["plot_samples"]), 60)
            self.assertNotIn("pairs", preview)
            self.assertFalse((path / "candidate.json").exists())
            output = io.StringIO()
            with patch.object(sys, "argv", ["bridge", "--fit-session", str(path), "--preview-fit"]), \
                 patch("reference_force_bridge.verify_frozen_vendor"), redirect_stdout(output):
                self.assertEqual(bridge_main(), 0)
            bridge_preview = json.loads(output.getvalue())
            self.assertTrue(bridge_preview["preview"])
            self.assertEqual(len(bridge_preview["plot_samples"]), 60)
            self.assertFalse((path / "candidate.json").exists())

    def test_bridge_final_fit_emits_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path)
            output, error = io.StringIO(), io.StringIO()
            with patch.object(sys, "argv", ["bridge", "--fit-session", str(path)]), \
                 patch("reference_force_bridge.verify_frozen_vendor"), \
                 redirect_stdout(output), redirect_stderr(error):
                self.assertEqual(bridge_main(), 0)
            self.assertEqual(json.loads(output.getvalue())["status"], "candidate")
            events = [json.loads(line) for line in error.getvalue().splitlines()]
            self.assertTrue(any(event.get("kind") == "fit_progress" for event in events))
            self.assertTrue((path / "candidate.json").exists())

    def test_unidentifiable_load_and_invalid_mask(self):
        for options in ({"same_load": True}, {"invalid_mask": True}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                make_session(path, **options)
                self.assertEqual(fit_session(path)["status"], "insufficient_data")

    def test_clock_jump_rejects_but_drift_is_retained_with_warning(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, clock_jump=True)
            self.assertEqual(fit_session(path)["status"], "insufficient_data")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path, drift=True)
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate", result.get("reason"))
            self.assertTrue(any(stage.get("drift_warning") for stage in result["stages"]))

    def test_pairing_quality_and_geometry(self):
        cases = [
            ("camera.jsonl", lambda row: row.update(confidence=0.5)),
            ("arm.jsonl", lambda row: row.update(observed_utc_ms=row["observed_utc_ms"] + 20000,
                                                    observed_monotonic_ns=row["observed_monotonic_ns"] + 20_000_000_000)),
            ("arm.jsonl", lambda row: row.update(sequence="repeated", frame_counter="repeated",
                                                    published_monotonic_ns="repeated")),
        ]
        for name, change in cases:
            with self.subTest(name=name, change=change), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                make_session(path)
                self.change_rows(path, name, change)
                self.assertEqual(fit_session(path)["status"], "insufficient_data")

    def test_roi_size_and_image_integrity(self):
        class Image:
            shape = (1000, 1400, 3)

        rois = {name: [10, 10, 100, 100]
                for name in ("S1", "S2", "S3", "S4", "Total", "Other")}
        rois["_image_size"] = [1400, 1000]
        _check_image_size(Image(), rois)
        rois["_image_size"] = [1000, 1400]
        with self.assertRaises(ValueError):
            _check_image_size(Image(), rois)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            make_session(path)
            (path / "camera" / "0-0.png").write_bytes(b"tampered")
            result = fit_session(path)
            self.assertEqual(result["status"], "candidate")
            self.assertEqual(result["rejections"]["image_evidence"], 1)
            self.assertEqual(result["accepted_pairs"], 89)

    def test_camera_absent_has_structured_error(self):
        error = io.StringIO()
        with patch("sys.argv", ["reference_force_bridge.py", "--capture-image", "preview.bmp"]), \
             patch("reference_force_bridge.capture_image", side_effect=RuntimeError("No Hikrobot camera found")), \
             redirect_stderr(error):
            self.assertEqual(bridge_main(), 2)
        payload = json.loads(error.getvalue())
        self.assertEqual(payload["schema"], "ucm-reference-force-tool-error/v1")
        self.assertIn("未发现海康相机", payload["message"])


if __name__ == "__main__":
    unittest.main()
