from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
VENDOR_ROOT = ROOT / "vendor"
CAMERA_PACKAGE = VENDOR_ROOT / "windows" / "force_input_camera"
PROVENANCE_PATH = CAMERA_PACKAGE / "SOURCE_PROVENANCE.json"


def emit_json(payload: dict, *, stream=None) -> None:
    # Qt reads QProcess pipes as UTF-8. A GUI-launched console process on
    # Windows can instead inherit the system code page, so keep the wire ASCII.
    print(json.dumps(payload, ensure_ascii=True, separators=(",", ":")),
          file=sys.stdout if stream is None else stream, flush=True)


def verify_frozen_vendor() -> dict[str, object]:
    provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
    if provenance.get("schema") != "ucm2-force-camera-source-provenance/v1":
        raise RuntimeError("unexpected force-camera provenance schema")
    expected = provenance.get("files_sha256")
    if not isinstance(expected, dict) or not expected:
        raise RuntimeError("force-camera provenance has no file hashes")
    for relative, expected_hash in expected.items():
        path = CAMERA_PACKAGE / str(relative)
        if not path.is_file():
            raise RuntimeError(f"frozen force-camera file is missing: {relative}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual.lower() != str(expected_hash).lower():
            raise RuntimeError(f"frozen force-camera hash mismatch: {relative}")
    return provenance


def status(state: str, message: str) -> None:
    payload = {
        "schema": "ucm-reference-force-status/v1",
        "state": state,
        "message": message,
        "timestamp_ms": time.time_ns() // 1_000_000,
    }
    emit_json(payload, stream=sys.stderr)


def friendly_camera_error(exc: BaseException) -> str:
    message = str(exc)
    if message.startswith("No Hikrobot camera found"):
        return "未发现海康相机，请检查相机供电、网线、IP、MVS驱动和防火墙"
    if "Cannot find Hikrobot MVS Python SDK wrapper" in message:
        return "未找到海康 MVS SDK，请先安装 MVS 或配置 SDK 路径"
    if message.startswith("MVS SDK import failed"):
        return "海康 MVS SDK 加载失败，请检查位数和运行库"
    return message


def one_shot_error(exc: BaseException) -> int:
    emit_json({"schema": "ucm-reference-force-tool-error/v1",
               "message": friendly_camera_error(exc)}, stream=sys.stderr)
    return 2


def _load_recognizer(rois_path: Path, config_path: Path | None = None,
                     fields: str = "S1,S2,S3,S4"):
    sys.path.insert(0, str(VENDOR_ROOT))
    from windows.force_input_camera.recognition_quality import apply_field_filter
    from windows.force_input_camera.seven_segment_recognize import load_json
    config = apply_field_filter(load_json(config_path or CAMERA_PACKAGE / "config.json"), fields)
    rois = load_json(rois_path)
    for name in ("S1", "S2", "S3", "S4", "Total", "Other"):
        value = rois.get(name)
        if not isinstance(value, list) or len(value) != 4 or any(
            not isinstance(number, int) or number < 0 for number in value
        ) or value[2] <= 0 or value[3] <= 0:
            raise ValueError(f"invalid ROI: {name}")
    return config, rois


def _check_image_size(frame, rois: dict) -> None:
    height, width = frame.shape[:2]
    expected = rois.get("_image_size")
    if expected is not None and expected != [width, height]:
        raise ValueError("camera image dimensions changed; ROI confirmation required")
    if any(x + w > width or y + h > height
           for x, y, w, h in (rois[name] for name in ("S1", "S2", "S3", "S4", "Total", "Other"))):
        raise ValueError("ROI exceeds camera image")


def _recognize(frame, name: str, config: dict, rois: dict) -> tuple[dict, float]:
    from windows.force_input_camera.recognition_quality import confidence_from_result
    from windows.force_input_camera.seven_segment_recognize import process_bgr_image
    result = process_bgr_image(name, frame, config, rois, ROOT / "debug", False)
    return result, float(confidence_from_result(result, config))


def _atomic_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".writing")
    with temporary.open("xb") as target:
        target.write(content)
        target.flush()
        os.fsync(target.fileno())
    temporary.replace(path)


def capture_image(args: argparse.Namespace) -> int:
    verify_frozen_vendor()
    sys.path.insert(0, str(VENDOR_ROOT))
    import cv2
    from windows.force_input_camera.hikrobot_camera import HikrobotCamera
    with HikrobotCamera(sdk_dirs=args.sdk_dir, device_index=args.device_index,
                        exposure_us=args.exposure_us, gain=args.gain) as camera:
        frame = camera.grab_bgr_frame(timeout_ms=args.timeout_ms)
    captured_ms = time.time_ns() // 1_000_000
    monotonic_ns = time.perf_counter_ns()
    ok, encoded = cv2.imencode(".bmp", frame)
    if not ok:
        raise RuntimeError("cannot encode camera preview")
    destination = Path(args.capture_image).resolve()
    _atomic_bytes(destination, encoded.tobytes())
    emit_json({"schema": "ucm-calibration-preview/v1",
               "image": str(destination), "width": int(frame.shape[1]),
               "height": int(frame.shape[0]), "timestamp_ms": captured_ms,
               "monotonic_ns": monotonic_ns,
               "sha256": hashlib.sha256(encoded.tobytes()).hexdigest()})
    return 0


def recognize_image(args: argparse.Namespace) -> int:
    verify_frozen_vendor()
    sys.path.insert(0, str(VENDOR_ROOT))
    from windows.force_input_camera.image_io import imread_unicode
    path = Path(args.recognize_image).resolve()
    frame = imread_unicode(path)
    if frame is None:
        raise ValueError(f"cannot read image: {path}")
    rois_path = Path(args.rois) if args.rois else CAMERA_PACKAGE / "rois.json"
    config_path = Path(args.config) if args.config else None
    config, rois = _load_recognizer(rois_path, config_path)
    height, width = frame.shape[:2]
    _check_image_size(frame, rois)
    result, confidence = _recognize(frame, path.name, config, rois)
    values = dict(result.get("values") or {})
    auxiliary = {}
    for field in ("Total", "Other"):
        field_config, _ = _load_recognizer(rois_path, config_path, field)
        field_result, field_confidence = _recognize(frame, path.name, field_config, rois)
        values[field] = (field_result.get("values") or {}).get(field)
        auxiliary[field] = {"status": field_result.get("status"),
                            "confidence": field_confidence}
    emit_json({"schema": "ucm-calibration-recognition/v1",
               "status": result.get("status"), "values": values,
               "confidence": confidence, "width": width, "height": height,
               "auxiliary": auxiliary,
               "image_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return 0


def run_capture(args: argparse.Namespace) -> int:
    verify_frozen_vendor()
    sys.path.insert(0, str(VENDOR_ROOT))

    from windows.force_input_camera.force_input_mapper import (
        ForceInputValidationError,
        map_camera_result_to_force_input,
    )
    from windows.force_input_camera.hikrobot_camera import (
        HikrobotCamera,
        MvsCameraError,
        MvsSdkNotFoundError,
    )
    rois_path = Path(args.rois) if args.rois else CAMERA_PACKAGE / "rois.json"
    config, rois = _load_recognizer(rois_path, Path(args.config) if args.config else None)
    session_dir = Path(args.session_dir).resolve() if args.session_dir else None
    if session_dir and not session_dir.is_dir():
        raise ValueError("calibration session directory is missing")
    interval_s = 1.0 / args.hz
    sequence = 0
    status("starting", "海康标准力相机桥正在启动")

    while True:
        try:
            with HikrobotCamera(
                sdk_dirs=args.sdk_dir,
                device_index=args.device_index,
                exposure_us=args.exposure_us,
                gain=args.gain,
                frame_rate_hz=args.hz,
            ) as camera:
                status("connected", "海康相机已连接，等待有效七段码")
                next_due = time.perf_counter()
                while True:
                    frame = camera.grab_bgr_frame(timeout_ms=args.timeout_ms)
                    timestamp_ms = time.time_ns() // 1_000_000
                    monotonic_ns = time.perf_counter_ns()
                    _check_image_size(frame, rois)
                    result, confidence = _recognize(
                        frame, f"live-{sequence:012d}.bmp", config, rois)
                    candidate = {
                        "timestamp_ms": timestamp_ms,
                        "confidence": confidence,
                        "status": result.get("status"),
                        "values": result.get("values"),
                        "sample_id": f"camera-{sequence:012d}",
                    }
                    try:
                        payload = map_camera_result_to_force_input(
                            candidate, min_confidence=args.min_confidence
                        )
                    except ForceInputValidationError as exc:
                        status("rejected", exc.reason_code)
                        if session_dir:
                            emit_json({
                                "schema": "ucm-calibration-camera-frame/v1",
                                "sample_id": candidate["sample_id"],
                                "timestamp_ms": timestamp_ms,
                                "monotonic_ns": monotonic_ns,
                                "confidence": confidence,
                                "status": "rejected",
                                "reason": exc.reason_code,
                                "force_kN": None,
                            })
                    else:
                        if session_dir:
                            event = {
                                "schema": "ucm-calibration-camera-frame/v1",
                                "sample_id": candidate["sample_id"],
                                "timestamp_ms": timestamp_ms,
                                "monotonic_ns": monotonic_ns,
                                "confidence": confidence,
                                "status": "idle",
                                "force_kN": payload["force_kN"],
                            }
                            if (session_dir / "recording.flag").exists():
                                import cv2
                                ok, encoded = cv2.imencode(".png", frame)
                                if not ok:
                                    raise RuntimeError("cannot encode calibration image")
                                image = session_dir / "camera" / f"frame-{sequence:012d}.png"
                                content = encoded.tobytes()
                                _atomic_bytes(image, content)
                                event["status"] = "ok"
                                event["image"] = str(image.relative_to(session_dir)).replace("\\", "/")
                                event["image_sha256"] = hashlib.sha256(content).hexdigest()
                            emit_json(event)
                        emit_json(payload)
                    sequence += 1
                    next_due += interval_s
                    delay = next_due - time.perf_counter()
                    if delay > 0:
                        time.sleep(delay)
                    else:
                        next_due = time.perf_counter()
        except KeyboardInterrupt:
            status("stopped", "标准力相机桥已停止")
            return 0
        except (MvsSdkNotFoundError, MvsCameraError, OSError, ValueError) as exc:
            status("retrying", friendly_camera_error(exc))
            try:
                time.sleep(args.reconnect_sec)
            except KeyboardInterrupt:
                status("stopped", "标准力相机桥已停止")
                return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Hikrobot seven-segment reference-force JSONL bridge"
    )
    parser.add_argument("--verify-vendor", action="store_true")
    parser.add_argument("--hz", type=float, default=10.0)
    parser.add_argument("--min-confidence", type=float, default=0.8)
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--sdk-dir", action="append", default=[])
    parser.add_argument("--config")
    parser.add_argument("--rois")
    parser.add_argument("--capture-image")
    parser.add_argument("--recognize-image")
    parser.add_argument("--session-dir")
    parser.add_argument("--fit-session")
    parser.add_argument("--preview-fit", action="store_true")
    parser.add_argument("--exposure-us", type=float)
    parser.add_argument("--gain", type=float)
    parser.add_argument("--timeout-ms", type=int, default=1000)
    parser.add_argument("--reconnect-sec", type=float, default=2.0)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if not 0.1 <= args.hz <= 35.0:
        parser.error("--hz must be between 0.1 and 35")
    if not 0.0 <= args.min_confidence <= 1.0:
        parser.error("--min-confidence must be between 0 and 1")
    if args.device_index < 0:
        parser.error("--device-index must be non-negative")
    if args.timeout_ms < 1 or args.reconnect_sec < 0.1:
        parser.error("timeouts must be positive")
    if args.verify_vendor:
        provenance = verify_frozen_vendor()
        emit_json({
            "status": "ok",
            "schema": provenance["schema"],
            "files_verified": len(provenance["files_sha256"]),
            "historical_freeze_commit": provenance["historical_freeze_commit"],
        })
        return 0
    if args.capture_image:
        try:
            return capture_image(args)
        except (OSError, ValueError, RuntimeError) as exc:
            return one_shot_error(exc)
    if args.recognize_image:
        try:
            return recognize_image(args)
        except (OSError, ValueError, RuntimeError) as exc:
            return one_shot_error(exc)
    if args.fit_session:
        verify_frozen_vendor()
        from calibration_candidate import fit_session, write_result
        session = Path(args.fit_session)
        if args.preview_fit:
            report = fit_session(session, preview=True)
            emit_json({key: value for key, value in report.items()
                       if key not in ("pairs", "used_images", "residuals_ns")})
            return 0
        report = fit_session(
            session,
            progress=lambda step, percent, message: emit_json(
                {"kind": "fit_progress", "step": step,
                 "percent": percent, "message": message}, stream=sys.stderr),
        )
        write_result(session, report)
        emit_json({"schema": report["schema"], "status": report["status"],
                   "reason": report.get("reason", ""),
                   "kmat_unified": report.get("kmat_unified"),
                   "coupling_bias_ns": report.get("coupling_bias_ns"),
                   "b_shared_ns": report.get("b_shared_ns"),
                   "force_correction": report.get("force_correction"),
                   "accepted_stages": report["accepted_stages"],
                   "accepted_pairs": report["accepted_pairs"],
                   "result": str(session / "candidate.json")})
        return 0 if report["status"] == "candidate" else 2
    return run_capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
