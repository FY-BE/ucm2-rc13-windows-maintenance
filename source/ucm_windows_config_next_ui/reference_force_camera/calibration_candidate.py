"""Offline, non-authoritative fit of camera force to ARM NCC delay.

The input files are an immutable Windows capture session.  This module never
communicates with ARM or changes a device configuration.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import math
import sys
from pathlib import Path
from statistics import median
from typing import Callable

import numpy as np

PAIR_LIMIT_MS = 500
EDGE_TRIM_MS = 250
MIN_STAGE_MS = 2000
MIN_PAIRS = 8
MIN_CAMERA_CONFIDENCE = 0.80
OUTLIER_MAD_MULTIPLIER = 6.0
MIN_BETWEEN_STAGE_LOAD_KN = 0.1
MIN_BETWEEN_STAGE_LOAD_FRACTION = 0.01
MAX_FORCE_CORRECTION_KNOTS = 8
GW_MODEL_ID = 37
GW_GEOMETRY_ID = "GW_DRAWING_FE_ENGINEERING_V1"
GW_TOTAL_LENGTH_MM = 5230.0
GW_ROD_DIAMETER_MM = 280.0
GW_ROOT_DIAMETER_MM = 250.0
GW_TOOTH_AREA_MM2 = 53816.963
GW_SMOOTH_AREA_MM2 = 61575.216
GW_REFERENCE_MOLD_MM = 662.0
GW_SMOOTH_LOADED_MM = 1320.0
GW_TOOTH_OFFSET_MM = 490.0
GW_LOCK_TRANSFER_MM = 288.0
GW_LOCK_PARTICIPATION = 0.35


def _rows(path: Path, allow_partial: bool = False) -> list[dict]:
    if not path.exists():
        return []
    with path.open(encoding="utf-8") as source:
        lines = [line for line in source if line.strip()]
    rows = []
    for index, line in enumerate(lines):
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            if not allow_partial or index != len(lines) - 1:
                raise
    return rows


def geometry(model: dict, mold_mm: float) -> float:
    if (model.get("device_model_id"), model.get("model_name")) == (GW_MODEL_ID, "GW1850R"):
        required = {
            "l_total_mm": GW_TOTAL_LENGTH_MM,
            "l_b_mm": 0.0,
            "l_d_mm": 0.0,
            "l_e_mm": 0.0,
            "abeq_mm2": GW_TOOTH_AREA_MM2,
            "ac_mm2": GW_SMOOTH_AREA_MM2,
            "adeq_mm2": GW_TOOTH_AREA_MM2,
            "phi_b": 0.0,
            "phi_d": 0.0,
            "thread_root_diameter_mm": GW_ROOT_DIAMETER_MM,
            "mold_reference_mm": GW_REFERENCE_MOLD_MM,
            "fixed_mold_thickness_mm": GW_REFERENCE_MOLD_MM,
            "body_reference_mm": 0.0,
        }
        if (model.get("schema_version") != 3 or
                model.get("geometry_model") != GW_GEOMETRY_ID or
                any(key not in model or not isinstance(model[key], (int, float)) or
                    isinstance(model[key], bool) or not math.isfinite(float(model[key])) or
                    not math.isclose(float(model[key]), expected, rel_tol=0.0, abs_tol=1e-3)
                    for key, expected in required.items())):
            raise ValueError("GW1850R active model does not match the frozen drawing/FE profile")
        rods = model.get("rod_diameter_mm")
        if (not isinstance(rods, list) or len(rods) != 4 or
                any(not isinstance(value, (int, float)) or isinstance(value, bool) or
                    not math.isclose(float(value), GW_ROD_DIAMETER_MM,
                                     rel_tol=0.0, abs_tol=1e-6)
                    for value in rods)):
            raise ValueError("GW1850R active model requires four 280 mm rods")
        if not math.isfinite(mold_mm) or not 1.0 <= mold_mm <= 950.0:
            raise ValueError("GW1850R paired PLC mold thickness is unavailable or out of range")
        tooth_effective_mm = (mold_mm + GW_TOOTH_OFFSET_MM -
                              (1.0 - GW_LOCK_PARTICIPATION) * GW_LOCK_TRANSFER_MM)
        if tooth_effective_mm <= 0.0:
            raise ValueError("GW1850R effective tooth length is non-positive")
        return 1000.0 * (GW_SMOOTH_LOADED_MM / GW_SMOOTH_AREA_MM2 +
                         tooth_effective_mm / GW_TOOTH_AREA_MM2)

    # A001/DE168 retain their fixed reference geometry.
    if (model.get("device_model_id"), model.get("model_name")) not in ((1, "A001"), (168, "DE168")):
        raise ValueError("unsupported or mismatched active device model")
    required = ("mold_reference_mm", "body_reference_mm", "l_total_mm",
                "l_b_mm", "l_d_mm", "l_e_mm", "abeq_mm2", "ac_mm2",
                "adeq_mm2", "phi_b", "phi_d")
    if model.get("schema_version") != 3 or model.get("geometry_model") != "BODY_REFERENCE_V1":
        raise ValueError("ARM active model is not Candidate 391 BODY_REFERENCE schema 3")
    values = {key: float(model[key]) for key in required}
    if not math.isfinite(mold_mm) or mold_mm <= 0 or any(
        not math.isfinite(value) or value <= 0 for value in values.values()
    ):
        raise ValueError("invalid active geometry or paired mold thickness")
    c = values["body_reference_mm"]
    a = values["l_total_mm"] - values["l_b_mm"] - values["l_d_mm"] - values["l_e_mm"] - c
    if a <= 0 or c <= 0:
        raise ValueError("A or C segment is non-positive")
    g = 1000.0 * (
        values["l_b_mm"] / values["abeq_mm2"] * values["phi_b"]
        + c / values["ac_mm2"]
        + values["l_d_mm"] / values["adeq_mm2"] * values["phi_d"]
    )
    if not math.isfinite(g) or g <= 0:
        raise ValueError("invalid G geometry factor")
    return g


def _coupling_bias(model: dict) -> list[float]:
    if "b_total_ns" in model or "relative_delay_intercept_ns" in model:
        raise ValueError("legacy delay-offset fields are not accepted by the force model V2")
    bias = model.get("coupling_bias_ns")
    if not isinstance(bias, list) or len(bias) != 4 or any(
        isinstance(value, bool) or not isinstance(value, (int, float)) or
        not math.isfinite(value) or abs(value) > 1e6 for value in bias
    ):
        raise ValueError("active model requires four finite coupling_bias_ns values")
    return [float(value) for value in bias]


def _clock_ok(rows: list[dict], utc: str, monotonic: str) -> bool:
    for first, second in zip(rows, rows[1:]):
        wall_delta = int(second[utc]) - int(first[utc])
        monotonic_delta = (int(second[monotonic]) - int(first[monotonic])) / 1_000_000
        if monotonic_delta < 0 or abs(wall_delta - monotonic_delta) > 100:
            return False
    return True


def _mad(values: list[float]) -> float:
    center = median(values)
    return median(abs(value - center) for value in values)


def _theil_sen(times: list[float], values: list[float]) -> float:
    # At most 50 evenly spread samples avoid quadratic cost on long plateaus.
    if len(times) > 50:
        indices = np.linspace(0, len(times) - 1, 50, dtype=int)
        times = [times[index] for index in indices]
        values = [values[index] for index in indices]
    slopes = [(values[j] - values[i]) / (times[j] - times[i])
              for i in range(len(times)) for j in range(i + 1, len(times))
              if times[j] > times[i]]
    return median(slopes) if slopes else 0.0


def _robust_coefficients(design: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray, int]:
    coefficients = np.linalg.lstsq(design, y, rcond=None)[0]
    iterations = 0
    for _ in range(30):
        iterations += 1
        residual = y - design @ coefficients
        sigma = max(1.4826 * _mad(residual.tolist()), 1e-9)
        weights = np.minimum(1.0, 1.345 * sigma / np.maximum(np.abs(residual), 1e-12))
        updated = np.linalg.lstsq(design * np.sqrt(weights[:, None]),
                                  y * np.sqrt(weights), rcond=None)[0]
        if np.linalg.norm(updated - coefficients) <= 1e-11 * max(1.0, np.linalg.norm(coefficients)):
            coefficients = updated
            break
        coefficients = updated
    return coefficients, y - design @ coefficients, iterations


def _display_fits_by_rod(representatives: list[dict]) -> list[dict]:
    """Independent rod lines are diagnostic only and never enter ARM values."""
    result = []
    for rod in range(4):
        rows = [row for row in representatives if row["rod"] == rod]
        item = {"rod": rod, "status": "insufficient_data", "dispatch": False}
        if len(rows) < 2:
            result.append(item)
            continue
        x = np.array([row["x"] for row in rows], dtype=float)
        y = np.array([row["dt_ns"] for row in rows], dtype=float)
        scale = float(np.max(np.abs(x)))
        if not math.isfinite(scale) or scale <= 0:
            result.append(item)
            continue
        design = np.column_stack((x / scale, np.ones(len(x))))
        if np.linalg.matrix_rank(design) < 2 or np.linalg.cond(design) > 1e8:
            result.append(item)
            continue
        coefficients, residual, _ = _robust_coefficients(design, y)
        kmat = float(coefficients[0] / scale)
        offset = float(coefficients[1])
        if math.isfinite(kmat) and kmat > 0 and math.isfinite(offset):
            item.update(status="diagnostic", kmat=kmat, b_ns=offset,
                        rmse_ns=float(np.sqrt(np.mean(residual ** 2))))
        result.append(item)
    return result


def _pwl_value(inputs: list[float], outputs: list[float], value: float) -> float:
    """Continuous linear interpolation with end-segment extrapolation."""
    if len(inputs) < 2:
        return value
    segment = len(inputs) - 2
    for index in range(len(inputs) - 1):
        if value <= inputs[index + 1]:
            segment = index
            break
    span = inputs[segment + 1] - inputs[segment]
    return outputs[segment] + (value - inputs[segment]) * (
        outputs[segment + 1] - outputs[segment]) / span


def _piecewise_force_correction(representatives: list[dict], kmat: float) -> dict:
    """Build one shared, zero-anchored monotone correction from stage medians.

    The affine intercept remains calibration evidence.  Runtime observations
    are coupling-corrected template-relative delays, so conversion uses Kmat and lets
    this curve absorb repeatable real-world residuals without subtracting the
    zero reference a second time.
    """
    by_stage: dict[int, list[dict]] = {}
    for row in representatives:
        by_stage.setdefault(int(row["stage"]), []).append(row)
    stage_points = []
    for stage, rows in sorted(by_stage.items()):
        base = [float(row["dt_ns"]) /
                (2.0 * float(row["g"]) * 1e9 * kmat) for row in rows]
        target = [float(row["force_kN"]) * 1000.0 for row in rows]
        stage_points.append({"stage": stage, "input_force_n": median(base),
                             "output_force_n": median(target)})
    positive = [point for point in stage_points
                if point["input_force_n"] > 0.0 and point["output_force_n"] > 0.0]
    if len(positive) < 2:
        return {"model": "IDENTITY", "knot_count": 0,
                "input_force_n": [], "output_force_n": [],
                "activation_reason": "fewer than two positive load levels"}

    positive.sort(key=lambda point: point["output_force_n"])
    merge_tolerance = max(100.0, 0.005 * positive[-1]["output_force_n"])
    groups: list[list[dict]] = []
    for point in positive:
        if groups and abs(point["output_force_n"] -
                          median(item["output_force_n"] for item in groups[-1])) <= merge_tolerance:
            groups[-1].append(point)
        else:
            groups.append([point])
    merged = [{"input_force_n": median(item["input_force_n"] for item in group),
               "output_force_n": median(item["output_force_n"] for item in group),
               "source_stages": [item["stage"] for item in group]}
              for group in groups]
    if len(merged) < 2 or any(
        merged[index]["input_force_n"] <= merged[index - 1]["input_force_n"] or
        merged[index]["output_force_n"] <= merged[index - 1]["output_force_n"]
        for index in range(1, len(merged))
    ):
        return {"model": "IDENTITY", "knot_count": 0,
                "input_force_n": [], "output_force_n": [],
                "activation_reason": "stage response is not strictly monotone"}

    if len(merged) > MAX_FORCE_CORRECTION_KNOTS - 1:
        indices = sorted(set(int(round(value)) for value in np.linspace(
            0, len(merged) - 1, MAX_FORCE_CORRECTION_KNOTS - 1)))
        merged = [merged[index] for index in indices]
    inputs = [0.0] + [point["input_force_n"] for point in merged]
    outputs = [0.0] + [point["output_force_n"] for point in merged]
    base_errors = []
    corrected_errors = []
    for row in representatives:
        base = float(row["dt_ns"]) / (2.0 * float(row["g"]) * 1e9 * kmat)
        target = float(row["force_kN"]) * 1000.0
        base_errors.append(base - target)
        corrected_errors.append(_pwl_value(inputs, outputs, base) - target)
    base_rmse = math.sqrt(sum(error * error for error in base_errors) / len(base_errors))
    corrected_rmse = math.sqrt(sum(error * error for error in corrected_errors) /
                               len(corrected_errors))
    if not math.isfinite(corrected_rmse) or corrected_rmse >= base_rmse:
        return {"model": "IDENTITY", "knot_count": 0,
                "input_force_n": [], "output_force_n": [],
                "activation_reason": "shared curve did not improve representative force RMSE",
                "representative_base_rmse_n": base_rmse,
                "representative_corrected_rmse_n": corrected_rmse}
    return {"model": "MONOTONE_PWL_ZERO_V1", "knot_count": len(inputs),
            "input_force_n": inputs, "output_force_n": outputs,
            "activation_reason": "lower representative force RMSE with monotone zero anchor",
            "representative_base_rmse_n": base_rmse,
            "representative_corrected_rmse_n": corrected_rmse,
            "stage_nodes": merged}


def _fit(representatives: list[dict]) -> dict:
    by_stage: dict[int, list[float]] = {}
    for row in representatives:
        by_stage.setdefault(row["stage"], []).append(float(row["force_kN"]))
    if len(by_stage) < 2:
        raise ValueError("at least two distinct stable load stages are required")
    stage_loads = [sum(values) / len(values) for values in by_stage.values()]
    minimum_load_change = max(MIN_BETWEEN_STAGE_LOAD_KN,
                              MIN_BETWEEN_STAGE_LOAD_FRACTION * max(abs(value) for value in stage_loads))
    if max(stage_loads) - min(stage_loads) < minimum_load_change:
        raise ValueError("load coverage is too narrow across stages")
    x = np.array([row["x"] for row in representatives], dtype=float)
    y = np.array([row["dt_ns"] for row in representatives], dtype=float)
    scale = float(np.max(np.abs(x))) if len(x) else 0.0
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError("load variation cannot identify Kmat")
    design = np.column_stack((x / scale, np.ones(len(x))))
    if np.linalg.matrix_rank(design) < 2:
        raise ValueError("shared Kmat and common b are not identifiable")
    if np.linalg.cond(design) > 1e8:
        raise ValueError("load coverage is too narrow for a stable fit")
    coefficients, residual, iterations = _robust_coefficients(design, y)
    kmat = float(coefficients[0] / scale)
    if not math.isfinite(kmat) or kmat <= 0 or not np.all(np.isfinite(coefficients)):
        raise ValueError("fitted Kmat must be positive and finite")
    degrees = max(1, len(y) - 2)
    variance = float(np.sum(residual ** 2) / degrees)
    covariance = variance * np.linalg.pinv(design.T @ design)
    uncertainty = np.sqrt(np.maximum(0, np.diag(covariance)))
    by_rod = {}
    for rod in range(4):
        selected = residual[[row["rod"] == rod for row in representatives]]
        by_rod[f"S{rod + 1}"] = {
            "rmse_ns": float(np.sqrt(np.mean(selected ** 2))),
            "maximum_absolute_ns": float(np.max(np.abs(selected))),
        }
    common_b = float(coefficients[1])
    common_b_uncertainty = float(uncertainty[1])
    correction = _piecewise_force_correction(representatives, kmat)
    return {"fit_constraint": "shared_kmat_shared_b",
            "kmat_unified": kmat, "b_shared_ns": common_b,
            "force_correction": correction,
            "standard_uncertainty": {"kmat_unified": float(uncertainty[0] / scale),
                                     "b_shared_ns": common_b_uncertainty},
            "residuals_by_rod": by_rod, "residuals_ns": residual.tolist(),
            "condition_number": float(np.linalg.cond(design)), "x_scale": scale,
            "irls_iterations": iterations}


def fit_session(session: Path, progress: Callable[[str, int, str], None] | None = None,
                preview: bool = False) -> dict:
    def update(step: str, percent: int, message: str) -> None:
        if progress is not None:
            progress(step, percent, message)

    update("evidence", 10, "核对 ROI、会话与原始采样")
    session = session.resolve()
    context = json.loads((session / "context.json").read_text(encoding="utf-8"))
    model = context["active_model"]
    camera = _rows(session / "camera.jsonl", allow_partial=preview)
    arm = _rows(session / "arm.jsonl", allow_partial=preview)
    stage_file = session / ("preview-stages.json" if preview else "stages.json")
    stage_data = json.loads(stage_file.read_text(encoding="utf-8"))
    stages = stage_data["stages"] if isinstance(stage_data, dict) else stage_data
    report: dict = {"schema": "ucm-windows-calibration-candidate/v3",
                    "status": "insufficient_data", "formal_qualification": False,
                    "preview": preview,
                    "preview_cutoff_utc_ms": max((int(stage["end_utc_ms"]) for stage in stages), default=0) if preview else 0,
                    "session_id": session.name, "stages": [], "rejections": {},
                    "plot_samples": [],
                    "input_counts": {"camera_frames": len(camera), "arm_frames": len(arm),
                                     "marked_stages": len(stages)},
                    "screening_rules": {"pair_limit_ms": PAIR_LIMIT_MS,
                                        "edge_trim_ms": EDGE_TRIM_MS,
                                        "min_stage_ms": MIN_STAGE_MS,
                                        "min_pairs_per_stage": MIN_PAIRS,
                                        "min_between_stage_load_kN": MIN_BETWEEN_STAGE_LOAD_KN,
                                        "min_between_stage_load_fraction": MIN_BETWEEN_STAGE_LOAD_FRACTION,
                                        "camera_confidence_min": MIN_CAMERA_CONFIDENCE,
                                        "outlier_mad_multiplier": OUTLIER_MAD_MULTIPLIER,
                                        "drift_policy": "record_warning_do_not_reject_stage"},
                    "roi_sha256": context["roi_sha256"],
                    "active_model_identity": context["active_model_identity"]}
    try:
        coupling = _coupling_bias(model)
        geometry(model, float(model["mold_reference_mm"]))
    except (KeyError, TypeError, ValueError) as exc:
        report["reason"] = str(exc)
        return report
    report.update(coupling_bias_ns=coupling,
                  coupling_bias_source="ARM_ACTIVE_MODEL_PRESERVED",
                  delay_coordinate="TEMPLATE_RELATIVE_MINUS_COUPLING_ONCE",
                  fitted_intercept_dispatch=False,
                  geometry_credit={"layer": "L0/L1", "board_credit": False,
                                   "device_model_id": model["device_model_id"],
                                   "model_name": model["model_name"],
                                   "geometry_model": model["geometry_model"]})
    if context.get("reference_source", "camera") == "camera":
        roi_file = session / "confirmed-rois.json"
        if not roi_file.is_file() or hashlib.sha256(roi_file.read_bytes()).hexdigest() != context["roi_sha256"]:
            report["reason"] = "confirmed ROI evidence is missing or has changed"
            return report
    elif context.get("reference_source") != "manual":
        report["reason"] = "unknown reference-force source"
        return report
    state_path = session / "session-state.json"
    valid_states = {"recording", "complete"} if preview else {"complete"}
    if not state_path.is_file() or json.loads(state_path.read_text(encoding="utf-8")).get("status") not in valid_states:
        report["reason"] = "session is incomplete or was interrupted"
        return report
    if not _clock_ok(camera, "timestamp_ms", "monotonic_ns") or not _clock_ok(
        arm, "observed_utc_ms", "observed_monotonic_ns"
    ):
        report["reason"] = "Windows clock discontinuity detected"
        return report
    update("identity", 22, "检查 ARM 帧身份、配置与时钟连续性")
    frame_ids = set()
    valid_arm = []
    for row in arm:
        identity = tuple(str(row.get(key, "")) for key in
                         ("generation", "published_monotonic_ns", "session_id",
                          "sequence", "frame_counter", "capture_request_id"))
        if identity in frame_ids or any(not value for value in identity):
            report["rejections"]["duplicate_or_invalid_arm_identity"] = report["rejections"].get(
                "duplicate_or_invalid_arm_identity", 0) + 1
            continue
        frame_ids.add(identity)
        valid_arm.append(row)
    arm = valid_arm
    if any(row.get("active_model_identity") != context["active_model_identity"] for row in arm):
        report["reason"] = "ARM configuration identity changed within the session"
        return report
    publisher_progress = {}
    for row in arm:
        publisher = (str(row["generation"]), str(row["session_id"]))
        try:
            current = int(row["published_monotonic_ns"])
        except (TypeError, ValueError):
            report["reason"] = "ARM publication identity is malformed"
            return report
        if publisher in publisher_progress and current <= publisher_progress[publisher]:
            report["reason"] = "ARM publication order or identity is inconsistent"
            return report
        publisher_progress[publisher] = current
    arm.sort(key=lambda row: int(row["observed_utc_ms"]))
    arm_times = [int(row["observed_utc_ms"]) for row in arm]
    used_arm: set[int] = set()
    baseline_g: float | None = None
    representatives: list[dict] = []
    accepted_pairs: list[dict] = []
    def reject(reason: str, summary: dict, count: int = 1) -> None:
        report["rejections"][reason] = report["rejections"].get(reason, 0) + count
        local = summary["rejections"]
        local[reason] = local.get(reason, 0) + count

    def publish_plot(points: list[dict]) -> None:
        accepted = [point for point in points if point["accepted"]]
        outliers = [point for point in points if point["rejection_reason"] == "outlier"]
        other = [point for point in points if not point["accepted"]
                 and point["rejection_reason"] != "outlier"]
        report["plot_samples"].extend(
            accepted[::max(1, math.ceil(len(accepted) / 80))]
            + outliers
            + other[::max(1, math.ceil(len(other) / 80))]
        )

    for stage_number, stage in enumerate(stages, 1):
        update("screening", 25 + int(42 * stage_number / max(1, len(stages))),
               f"配对并筛查第 {stage_number}/{len(stages)} 档")
        start = int(stage["start_utc_ms"])
        end = int(stage["end_utc_ms"])
        summary = {"number": stage_number, "duration_ms": end - start,
                   "matched": 0, "accepted": 0, "status": "rejected", "reason": "",
                   "rejections": {}}
        report["stages"].append(summary)
        summary["camera_in_stage"] = sum(start <= int(row["timestamp_ms"]) <= end for row in camera)
        if end - start < MIN_STAGE_MS:
            summary["reason"] = f"stage shorter than {MIN_STAGE_MS / 1000:g} seconds"
            continue
        stage_camera = [row for row in camera if start + EDGE_TRIM_MS <=
                        int(row["timestamp_ms"]) <= end - EDGE_TRIM_MS]
        summary["camera_after_trim"] = len(stage_camera)
        summary["transition_trimmed"] = summary["camera_in_stage"] - len(stage_camera)
        pairs = []
        for cam in stage_camera:
            if cam.get("status") != "ok" or float(cam.get("confidence", 0)) < MIN_CAMERA_CONFIDENCE:
                reject("camera_quality", summary)
                continue
            forces = cam.get("force_kN")
            if not isinstance(forces, list) or len(forces) != 4 or any(
                not isinstance(force, (int, float)) or not math.isfinite(force) or force < 0
                for force in forces
            ):
                reject("camera_values", summary)
                continue
            camera_time = int(cam["timestamp_ms"])
            left = bisect.bisect_left(arm_times, camera_time - PAIR_LIMIT_MS)
            right = bisect.bisect_right(arm_times, camera_time + PAIR_LIMIT_MS)
            eligible = [(abs(arm_times[index] - camera_time), index, arm[index])
                        for index in range(left, right)
                        if index not in used_arm and start <= arm_times[index] <= end
                        and int(arm[index].get("measurement_mask", 0)) & 15 == 15
                        and arm[index].get("active_model_identity") == context["active_model_identity"]]
            if not eligible:
                reject("arm_unavailable", summary)
                continue
            distance, index, matched = min(eligible, key=lambda item: item[0])
            if sum(item[0] == distance for item in eligible) != 1:
                reject("ambiguous_pair", summary)
                continue
            if distance > PAIR_LIMIT_MS:
                reject("time_gap", summary)
                continue
            try:
                mold_mm = float(matched["effective_thickness_um"]) / 1000.0
                g = geometry(model, mold_mm)
                delays = [float(value) for value in matched["ncc_delta_ns"]]
                if len(delays) != 4 or not all(math.isfinite(value) for value in delays):
                    raise ValueError("invalid delays")
            except (KeyError, TypeError, ValueError) as exc:
                reject("geometry_or_delay", summary)
                continue
            if baseline_g is not None and abs(g - baseline_g) > 1e-9 * baseline_g:
                report["reason"] = "geometry changed within the calibration session"
                return report
            baseline_g = g
            image = session / cam.get("image", "")
            try:
                if not image.resolve().is_relative_to(session) or not image.is_file():
                    raise ValueError("missing image")
                if not preview and hashlib.sha256(image.read_bytes()).hexdigest() != cam.get("image_sha256"):
                    raise ValueError("image hash mismatch")
            except (OSError, ValueError):
                reject("image_evidence", summary)
                continue
            used_arm.add(index)
            pairs.append({"stage": stage_number, "camera_sample_id": cam["sample_id"],
                          "camera_image": cam["image"], "camera_image_sha256": cam["image_sha256"],
                          "camera_utc_ms": int(cam["timestamp_ms"]),
                          "arm_utc_ms": int(matched["observed_utc_ms"]),
                          "pair_gap_ms": distance, "arm_frame_counter": matched["frame_counter"],
                          "g": g, "force_kN": forces, "ncc_delta_ns": delays})
        summary["matched"] = len(pairs)
        plotted = [{"stage": stage_number, "camera_sample_id": pair["camera_sample_id"],
                    "camera_utc_ms": pair["camera_utc_ms"], "arm_utc_ms": pair["arm_utc_ms"],
                    "pair_gap_ms": pair["pair_gap_ms"],
                    "force_kN": pair["force_kN"], "ncc_delta_ns": pair["ncc_delta_ns"],
                    "g": pair["g"], "accepted": False, "rejection_reason": "stage_rejected"}
                   for pair in pairs]
        if len(pairs) < MIN_PAIRS:
            summary["reason"] = f"fewer than {MIN_PAIRS} matched frames"
            publish_plot(plotted)
            continue
        keep = [True] * len(pairs)
        for rod in range(4):
            for key, floor in (("force_kN", 0.005), ("ncc_delta_ns", 1e-6)):
                values = [pair[key][rod] for pair in pairs]
                center = median(values)
                spread = max(1.4826 * _mad(values), floor)
                for index, value in enumerate(values):
                    if abs(value - center) > OUTLIER_MAD_MULTIPLIER * spread:
                        keep[index] = False
        selected = [pair for pair, admitted in zip(pairs, keep) if admitted]
        for point, admitted in zip(plotted, keep):
            if not admitted:
                point["rejection_reason"] = "outlier"
        summary["outliers"] = len(pairs) - len(selected)
        reject("outlier", summary, len(pairs) - len(selected))
        if len(selected) < MIN_PAIRS:
            summary["reason"] = f"fewer than {MIN_PAIRS} pairs after outlier rejection"
            publish_plot(plotted)
            continue
        selected_times = [(pair["camera_utc_ms"] - start) / 1000.0 for pair in selected]
        drifting = False
        summary["trend_checks"] = []
        for rod in range(4):
            for key, floor in (("force_kN", 0.005), ("ncc_delta_ns", 1e-6)):
                values = [pair[key][rod] for pair in selected]
                slope = _theil_sen(selected_times, values)
                detrended = [value - slope * time for time, value in zip(selected_times, values)]
                noise = max(1.4826 * _mad(detrended), floor)
                drift = abs(slope * (selected_times[-1] - selected_times[0]))
                summary["trend_checks"].append({"rod": rod + 1, "channel": key,
                    "slope_per_s": slope, "drift_over_stage": drift,
                    "noise_mad": noise, "limit": 3 * noise,
                    "passed": drift <= 3 * noise})
                if drift > 3 * noise:
                    drifting = True
        summary["drift_warning"] = drifting
        if drifting:
            summary["warnings"] = ["sustained force or delay drift recorded; stage retained"]
        summary["status"] = "accepted"
        summary["accepted"] = len(selected)
        selected_ids = {pair["camera_sample_id"] for pair in selected}
        for point in plotted:
            point["accepted"] = point["camera_sample_id"] in selected_ids
            if point["accepted"]:
                point["rejection_reason"] = ""
        publish_plot(plotted)
        summary["median_pair_gap_ms"] = median(pair["pair_gap_ms"] for pair in selected)
        accepted_pairs.extend(selected)
        for rod in range(4):
            force = median(pair["force_kN"][rod] for pair in selected)
            g = median(pair["g"] for pair in selected)
            representatives.append({
                "stage": stage_number, "rod": rod,
                "x": median(2.0 * pair["g"] * 1e9 * pair["force_kN"][rod] * 1000.0
                            for pair in selected),
                "dt_ns": median(pair["ncc_delta_ns"][rod] for pair in selected) - coupling[rod],
                "raw_dt_ns": median(pair["ncc_delta_ns"][rod] for pair in selected),
                "coupling_bias_ns": coupling[rod],
                "force_kN": force, "g": g, "pair_count": len(selected),
            })
    report["accepted_pairs"] = len(accepted_pairs)
    if accepted_pairs:
        gaps = [pair["pair_gap_ms"] for pair in accepted_pairs]
        report["pair_gap_ms"] = {"median": median(gaps),
                                 "p95": float(np.percentile(gaps, 95)),
                                 "maximum": max(gaps)}
    report["accepted_stages"] = sum(stage["status"] == "accepted" for stage in report["stages"])
    report["used_images"] = sorted({pair["camera_image"] for pair in accepted_pairs})
    report["load_range_kN_by_rod"] = [
        [min(pair["force_kN"][rod] for pair in accepted_pairs),
         max(pair["force_kN"][rod] for pair in accepted_pairs)]
        for rod in range(4)] if accepted_pairs else []
    report["pairs"] = accepted_pairs
    if not representatives:
        report["reason"] = "no accepted stable stages"
        return report
    report["display_fits_by_rod"] = _display_fits_by_rod(representatives)
    update("regression", 75, "按档形成四杆代表值，求共享 Kmat 与共同 b")
    try:
        fitted = _fit(representatives)
    except ValueError as exc:
        report["reason"] = str(exc)
        report["representatives"] = representatives
        return report
    report.update(fitted)
    report["representatives"] = [{**row,
        "predicted_dt_ns": fitted["kmat_unified"] * row["x"] + fitted["b_shared_ns"],
        "predicted_raw_dt_ns": fitted["kmat_unified"] * row["x"] + fitted["b_shared_ns"] + coupling[row["rod"]],
        "residual_ns": row["dt_ns"] - (fitted["kmat_unified"] * row["x"]
                                      + fitted["b_shared_ns"])}
        for row in representatives]
    report["status"] = "candidate"
    update("validation", 88, "计算逐杆残差、不确定度和逐档留一验证")
    report["independent_validation"] = "insufficient_stages"
    if report["accepted_stages"] >= 3:
        validation = []
        for withheld in range(1, len(stages) + 1):
            training = [row for row in representatives if row["stage"] != withheld]
            held = [row for row in representatives if row["stage"] == withheld]
            if not held:
                continue
            try:
                trial = _fit(training)
            except ValueError:
                continue
            validation.extend(row["dt_ns"] - (
                trial["kmat_unified"] * row["x"]
                + trial["b_shared_ns"]) for row in held)
        if validation:
            report["independent_validation"] = {
                "leave_one_stage_out_rmse_ns": math.sqrt(
                    sum(value * value for value in validation) / len(validation)),
                "tested_values": len(validation),
            }
    update("report", 96, "生成拟合结果与逐档证据")
    return report


def write_result(session: Path, report: dict) -> None:
    result = session / "candidate.json"
    temporary = result.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(result)
    csv_path = session / "matched-pairs.csv"
    csv_temporary = csv_path.with_suffix(".csv.tmp")
    with csv_temporary.open("w", newline="", encoding="utf-8-sig") as target:
        writer = csv.writer(target)
        writer.writerow(["stage", "camera_sample_id", "camera_utc_ms", "arm_utc_ms",
                         "pair_gap_ms", "arm_frame_counter", "g", "rod",
                         "camera_force_kN", "arm_ncc_delta_ns", "image_sha256"])
        for pair in report.get("pairs", []):
            for rod in range(4):
                writer.writerow([pair["stage"], pair["camera_sample_id"],
                                 pair["camera_utc_ms"], pair["arm_utc_ms"],
                                 pair["pair_gap_ms"], pair["arm_frame_counter"],
                                 pair["g"], rod + 1, pair["force_kN"][rod],
                                 pair["ncc_delta_ns"][rod], pair["camera_image_sha256"]])
    csv_temporary.replace(csv_path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fit-session", type=Path, required=True)
    parser.add_argument("--preview-fit", action="store_true")
    args = parser.parse_args()
    if args.preview_fit:
        report = fit_session(args.fit_session, preview=True)
        compact = {key: value for key, value in report.items()
                   if key not in ("pairs", "used_images", "residuals_ns")}
        print(json.dumps(compact, ensure_ascii=True, separators=(",", ":")), flush=True)
        return 0
    def progress(step: str, percent: int, message: str) -> None:
        print(json.dumps({"kind": "fit_progress", "step": step,
                          "percent": percent, "message": message},
                         ensure_ascii=True), file=sys.stderr, flush=True)

    report = fit_session(args.fit_session, progress)
    write_result(args.fit_session, report)
    progress("saved", 100, "拟合报告已保存" if report["status"] == "candidate"
             else "筛查报告已保存；未生成候选")
    print(json.dumps({"schema": report["schema"], "status": report["status"],
                      "reason": report.get("reason", ""),
                      "kmat_unified": report.get("kmat_unified"),
                      "coupling_bias_ns": report.get("coupling_bias_ns"),
                      "b_shared_ns": report.get("b_shared_ns"),
                      "force_correction": report.get("force_correction"),
                      "accepted_stages": report["accepted_stages"],
                      "accepted_pairs": report["accepted_pairs"],
                      "result": str(args.fit_session / "candidate.json")},
                     ensure_ascii=False, separators=(",", ":")))
    return 0 if report["status"] == "candidate" else 2


if __name__ == "__main__":
    raise SystemExit(main())
