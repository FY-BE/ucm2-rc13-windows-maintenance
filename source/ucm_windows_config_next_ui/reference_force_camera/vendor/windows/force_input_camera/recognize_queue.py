from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path

from .image_io import imread_unicode
from .offline_queue import iter_frame_paths, parse_frame_path
from .recognition_quality import apply_field_filter, confidence_from_result
from .seven_segment_recognize import ensure_dir, load_json, process_bgr_image


def csv_fieldnames(cfg: dict) -> list[str]:
    return [
        "frame_index",
        "timestamp_ms",
        "confidence",
        "status",
        "unknown_count",
        "recognition_latency_ms",
        *cfg["roi_names"],
    ]


def append_csv_row(path: Path, fieldnames: list[str], row: dict):
    ensure_dir(path.parent)
    write_header = not path.exists() or path.stat().st_size == 0
    if not write_header:
        with path.open("r", encoding="utf-8-sig", newline="") as f:
            existing = next(csv.reader(f), None)
        if existing != fieldnames:
            raise ValueError(
                "CSV header mismatch; use a new output file or the same --fields setting. "
                f"existing={existing}, current={fieldnames}"
            )
    with path.open("a", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if write_header:
            writer.writeheader()
        writer.writerow(row)


def move_failed_frame(queue_dir: Path, frame_path: Path) -> Path:
    failed_dir = queue_dir / "failed"
    ensure_dir(failed_dir)
    target = failed_dir / frame_path.name
    if target.exists():
        target = failed_dir / f"{frame_path.stem}_{int(time.time() * 1000)}{frame_path.suffix}"
    frame_path.replace(target)
    return target


def result_to_row(frame_path: Path, confidence: float, result: dict, cfg: dict, recognition_latency_ms: float) -> dict:
    frame_index, timestamp = parse_frame_path(frame_path)
    row = {
        "frame_index": frame_index,
        "timestamp_ms": timestamp.isoformat(timespec="milliseconds"),
        "confidence": f"{confidence:.4f}",
        "status": result.get("status", ""),
        "unknown_count": result.get("unknown_count", ""),
        "recognition_latency_ms": f"{recognition_latency_ms:.3f}",
    }
    values = result.get("values", {})
    for name in cfg["roi_names"]:
        row[name] = values.get(name, "")
    return row


def process_available(args, cfg: dict, rois: dict) -> dict:
    queue_dir = Path(args.queue_dir)
    output_path = Path(args.output)
    debug_output = Path(args.debug_output)
    fieldnames = csv_fieldnames(cfg)
    stats = {"processed": 0, "accepted": 0, "rejected": 0, "failed": 0, "remaining": 0}

    frames = iter_frame_paths(queue_dir)
    if args.max_frames is not None:
        frames = frames[: int(args.max_frames)]

    for frame_path in frames:
        image = imread_unicode(frame_path)
        if image is None:
            stats["failed"] += 1
            move_failed_frame(queue_dir, frame_path)
            continue

        start_s = time.perf_counter()
        result = process_bgr_image(frame_path.name, image, cfg, rois, debug_output, args.debug)
        recognition_latency_ms = (time.perf_counter() - start_s) * 1000.0
        confidence = confidence_from_result(result, cfg)

        if confidence >= float(args.min_confidence):
            append_csv_row(output_path, fieldnames, result_to_row(frame_path, confidence, result, cfg, recognition_latency_ms))
            stats["accepted"] += 1
        else:
            stats["rejected"] += 1

        if not args.keep_rejected or confidence >= float(args.min_confidence):
            frame_path.unlink(missing_ok=True)
        stats["processed"] += 1

    stats["remaining"] = len(iter_frame_paths(queue_dir))
    return stats


def run_recognition(args) -> dict:
    cfg = apply_field_filter(load_json(args.config), args.fields)
    rois = load_json(args.rois)
    total = {"processed": 0, "accepted": 0, "rejected": 0, "failed": 0, "remaining": 0}

    while True:
        stats = process_available(args, cfg, rois)
        for key in ["processed", "accepted", "rejected", "failed"]:
            total[key] += stats[key]
        total["remaining"] = stats["remaining"]

        if not args.watch:
            break
        if stats["processed"] == 0:
            time.sleep(float(args.poll_sec))
    return total


def main():
    parser = argparse.ArgumentParser(description="Offline recognition for queued camera frames")
    parser.add_argument("--queue-dir", default="frame_queue")
    parser.add_argument("--output", default="offline_results.csv")
    parser.add_argument("--min-confidence", type=float, default=0.9)
    parser.add_argument("--fields", default=None, help="comma-separated ROI names to process, for example: Total,Other")
    parser.add_argument("--config", default="config.json")
    parser.add_argument("--rois", default="rois.json")
    parser.add_argument("--max-frames", type=int, default=None)
    parser.add_argument("--keep-rejected", action="store_true", help="keep low-confidence images for ROI/threshold tuning")
    parser.add_argument("--watch", action="store_true", help="keep polling the queue directory")
    parser.add_argument("--poll-sec", type=float, default=0.5)
    parser.add_argument("--debug", action="store_true", help="save recognition debug images")
    parser.add_argument("--debug-output", default="output_queue_debug")
    args = parser.parse_args()

    stats = run_recognition(args)
    print(
        "done: processed={processed}, accepted={accepted}, rejected={rejected}, "
        "failed={failed}, remaining={remaining}".format(**stats)
    )


if __name__ == "__main__":
    main()
