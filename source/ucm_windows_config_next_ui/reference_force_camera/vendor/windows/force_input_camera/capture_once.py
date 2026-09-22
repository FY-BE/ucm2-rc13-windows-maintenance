from __future__ import annotations

import argparse
import csv
import json
from datetime import datetime
from pathlib import Path

from .hikrobot_camera import HikrobotCamera
from .image_io import imwrite_unicode
from .seven_segment_recognize import ensure_dir, load_json, process_image


def default_capture_path() -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path("captures") / f"capture_{stamp}.bmp"


def write_single_result_outputs(output_dir: Path, cfg: dict, result: dict):
    ensure_dir(output_dir)
    csv_path = output_dir / "results.csv"
    header = ["filename"] + [cfg.get("display_names", {}).get(n, n) for n in cfg["roi_names"]] + [
        "status",
        "unknown_count",
    ]
    with csv_path.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        row = [result["filename"]]
        for name in cfg["roi_names"]:
            row.append(result["values"].get(name, ""))
        row += [result["status"], result["unknown_count"]]
        writer.writerow(row)
    with (output_dir / "details.json").open("w", encoding="utf-8") as f:
        json.dump([result], f, ensure_ascii=False, indent=2)


def main():
    parser = argparse.ArgumentParser(description="Capture one frame from a Hikrobot MVS camera")
    parser.add_argument("--output", default=None, help="image output path; default captures/capture_timestamp.bmp")
    parser.add_argument("--sdk-dir", action="append", default=[], help="MVS install dir or MvImport dir")
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--exposure-us", type=float, default=None)
    parser.add_argument("--gain", type=float, default=None)
    parser.add_argument("--timeout-ms", type=int, default=2000)
    parser.add_argument("--recognize", action="store_true", help="run recognition after capture")
    parser.add_argument("--config", default="config.json")
    parser.add_argument("--rois", default="rois.json")
    parser.add_argument("--recognize-output", default="output_live")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    output_path = Path(args.output) if args.output else default_capture_path()
    ensure_dir(output_path.parent)
    with HikrobotCamera(
        sdk_dirs=args.sdk_dir,
        device_index=args.device_index,
        exposure_us=args.exposure_us,
        gain=args.gain,
    ) as camera:
        frame = camera.grab_bgr_frame(timeout_ms=args.timeout_ms)
    if not imwrite_unicode(output_path, frame):
        raise RuntimeError(f"Failed to write image: {output_path}")
    print(f"Captured: {output_path}")

    if args.recognize:
        cfg = load_json(args.config)
        rois = load_json(args.rois)
        recognize_output = Path(args.recognize_output)
        result = process_image(output_path, cfg, rois, recognize_output, args.debug)
        write_single_result_outputs(recognize_output, cfg, result)
        print(json.dumps(result["values"], ensure_ascii=False, indent=2))
        print(f"status={result['status']}, unknown_count={result['unknown_count']}")


if __name__ == "__main__":
    main()
