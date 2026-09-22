from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import cv2
import numpy as np

from .image_io import imread_unicode, imwrite_unicode


SEGMENT_ORDER = ["A", "B", "C", "D", "E", "F", "G"]

DIGIT_MAP = {
    (1, 1, 1, 1, 1, 1, 0): "0",
    (0, 1, 1, 0, 0, 0, 0): "1",
    (1, 1, 0, 1, 1, 0, 1): "2",
    (1, 1, 1, 1, 0, 0, 1): "3",
    (0, 1, 1, 0, 0, 1, 1): "4",
    (1, 0, 1, 1, 0, 1, 1): "5",
    (1, 0, 1, 1, 1, 1, 1): "6",
    (1, 1, 1, 0, 0, 0, 0): "7",
    (1, 1, 1, 1, 1, 1, 1): "8",
    (1, 1, 1, 1, 0, 1, 1): "9",
    (0, 0, 0, 0, 0, 0, 1): "-",
    (0, 0, 0, 0, 0, 0, 0): "",
}


def load_json(path: str | Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def ensure_dir(path: Path):
    path.mkdir(parents=True, exist_ok=True)


def rotate_if_needed(img, cfg: dict):
    if cfg.get("rotate_180", False):
        return cv2.rotate(img, cv2.ROTATE_180)
    return img


def extract_red_mask(bgr_img, cfg: dict):
    hsv = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2HSV)
    thresholds = cfg["red_thresholds"]
    mask1 = cv2.inRange(hsv, tuple(thresholds["lower_red_1"]), tuple(thresholds["upper_red_1"]))
    mask2 = cv2.inRange(hsv, tuple(thresholds["lower_red_2"]), tuple(thresholds["upper_red_2"]))
    mask = cv2.bitwise_or(mask1, mask2)

    morph = cfg.get("morphology", {})
    open_k = tuple(morph.get("open_kernel", [3, 3]))
    close_k = tuple(morph.get("close_kernel", [5, 5]))
    if open_k[0] > 0 and open_k[1] > 0:
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, open_k))
    if close_k[0] > 0 and close_k[1] > 0:
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_RECT, close_k))
    return mask


def split_fixed_digits(mask, digit_count: int, trim_ratio: float):
    h, w = mask.shape[:2]
    cell_w = w / digit_count
    cells = []
    for i in range(digit_count):
        x1 = int(round(i * cell_w))
        x2 = int(round((i + 1) * cell_w))
        trim = int((x2 - x1) * trim_ratio)
        xx1 = max(0, x1 + trim)
        xx2 = min(w, x2 - trim)
        cells.append(
            {
                "digit": mask[:, xx1:xx2],
                "full": mask[:, x1:x2],
                "digit_box": (xx1, 0, xx2 - xx1, h),
                "full_box": (x1, 0, x2 - x1, h),
            }
        )
    return cells


def white_ratio(mask_roi):
    total = mask_roi.shape[0] * mask_roi.shape[1]
    if total == 0:
        return 0.0
    return cv2.countNonZero(mask_roi) / total


def polygon_to_points(points_norm, std_w: int, std_h: int):
    return np.array([[int(round(x * std_w)), int(round(y * std_h))] for x, y in points_norm], dtype=np.int32)


def white_ratio_polygon(mask, points_norm, std_w: int, std_h: int):
    pts = polygon_to_points(points_norm, std_w, std_h)
    poly_mask = np.zeros(mask.shape[:2], dtype=np.uint8)
    cv2.fillPoly(poly_mask, [pts], 255)
    area = cv2.countNonZero(poly_mask)
    if area == 0:
        return 0.0
    selected = cv2.bitwise_and(mask, mask, mask=poly_mask)
    return cv2.countNonZero(selected) / area


def get_segment_ratio(std, seg: str, cfg: dict, std_w: int, std_h: int):
    segment_polygons = cfg.get("segment_polygons", {})
    if seg in segment_polygons:
        return white_ratio_polygon(std, segment_polygons[seg], std_w, std_h)
    x1r, y1r, x2r, y2r = cfg["segment_rois"][seg]
    roi = std[int(y1r * std_h) : int(y2r * std_h), int(x1r * std_w) : int(x2r * std_w)]
    return white_ratio(roi)


def get_decimal_box(full_cell, cfg: dict):
    dp_cfg = cfg.get("decimal_point", {})
    x1r, y1r, x2r, y2r = dp_cfg.get("roi", [0.72, 0.68, 0.98, 0.98])
    h, w = full_cell.shape[:2]
    x1 = max(0, min(int(round(x1r * w)), w))
    x2 = max(0, min(int(round(x2r * w)), w))
    y1 = max(0, min(int(round(y1r * h)), h))
    y2 = max(0, min(int(round(y2r * h)), h))
    return x1, y1, x2, y2


def detect_decimal_point(full_cell, cfg: dict):
    dp_cfg = cfg.get("decimal_point", {})
    if not dp_cfg.get("enabled", False):
        return False, 0.0, 0, (0, 0, 0, 0)
    x1, y1, x2, y2 = get_decimal_box(full_cell, cfg)
    roi = full_cell[y1:y2, x1:x2]
    ratio = white_ratio(roi)
    nonzero = cv2.countNonZero(roi) if roi.size else 0
    is_on = ratio >= float(dp_cfg.get("threshold", 0.03)) and nonzero >= int(dp_cfg.get("min_pixels", 6))
    return is_on, ratio, nonzero, (x1, y1, x2, y2)


def recognize_digit(cell_mask, cfg: dict):
    std_w = int(cfg["std_size"]["width"])
    std_h = int(cfg["std_size"]["height"])
    if cell_mask.size == 0 or cell_mask.shape[0] == 0 or cell_mask.shape[1] == 0:
        state_tuple = tuple([0] * len(SEGMENT_ORDER))
        ratios = {seg: 0.0 for seg in SEGMENT_ORDER}
        std = np.zeros((std_h, std_w), dtype=np.uint8)
        return "", state_tuple, ratios, std
    std = cv2.resize(cell_mask, (std_w, std_h), interpolation=cv2.INTER_NEAREST)
    states = []
    ratios = {}
    for seg in SEGMENT_ORDER:
        ratio = get_segment_ratio(std, seg, cfg, std_w, std_h)
        ratios[seg] = ratio
        states.append(1 if ratio >= float(cfg["segment_thresholds"][seg]) else 0)
    state_tuple = tuple(states)
    if cv2.countNonZero(cell_mask) < int(cfg.get("min_nonzero_per_digit", 10)):
        return "", state_tuple, ratios, std
    return DIGIT_MAP.get(state_tuple, "?"), state_tuple, ratios, std


def make_segment_debug(std_mask, digit, state_tuple, ratios, cfg: dict):
    std_w = int(cfg["std_size"]["width"])
    std_h = int(cfg["std_size"]["height"])
    debug = cv2.cvtColor(std_mask, cv2.COLOR_GRAY2BGR)
    segment_polygons = cfg.get("segment_polygons", {})
    for idx, seg in enumerate(SEGMENT_ORDER):
        color = (0, 255, 0) if state_tuple[idx] else (0, 0, 255)
        if seg in segment_polygons:
            pts = polygon_to_points(segment_polygons[seg], std_w, std_h)
            cv2.polylines(debug, [pts], isClosed=True, color=color, thickness=1)
            label_x, label_y = int(pts[0][0]), int(pts[0][1])
        else:
            x1r, y1r, x2r, y2r = cfg["segment_rois"][seg]
            x1, y1 = int(x1r * std_w), int(y1r * std_h)
            x2, y2 = int(x2r * std_w), int(y2r * std_h)
            cv2.rectangle(debug, (x1, y1), (x2, y2), color, 1)
            label_x, label_y = x1, y1
        cv2.putText(debug, f"{seg}:{ratios[seg]:.2f}", (label_x, max(label_y - 3, 10)), cv2.FONT_HERSHEY_SIMPLEX, 0.32, color, 1)
    cv2.putText(debug, f"digit={digit}", (5, std_h - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 0), 1)
    return debug


def make_decimal_debug(full_cell, is_on, ratio, nonzero, box):
    debug = cv2.cvtColor(full_cell, cv2.COLOR_GRAY2BGR)
    x1, y1, x2, y2 = box
    color = (0, 255, 0) if is_on else (0, 0, 255)
    cv2.rectangle(debug, (x1, y1), (x2, y2), color, 1)
    cv2.putText(debug, f"DP:{ratio:.2f}/{nonzero}", (3, max(y1 - 4, 12)), cv2.FONT_HERSHEY_SIMPLEX, 0.38, color, 1)
    return debug


def process_bgr_image(image_name: str, img, cfg: dict, rois: dict, output_dir: Path, debug: bool):
    img = rotate_if_needed(img, cfg)
    values = {}
    details = []
    unknown_count = 0
    debug_img = img.copy()
    debug_base = output_dir / "debug" / Path(image_name).stem
    if debug:
        ensure_dir(debug_base)

    for name in cfg["roi_names"]:
        if name not in rois:
            values[name] = ""
            continue
        x, y, w, h = rois[name]
        crop = img[y : y + h, x : x + w]
        red_mask = extract_red_mask(crop, cfg)
        if debug:
            imwrite_unicode(debug_base / f"{name}_red_mask.bmp", red_mask)
        cells = split_fixed_digits(red_mask, int(cfg["digit_counts"][name]), float(cfg["cell_x_trim_ratio"]))
        value = ""
        split_debug = cv2.cvtColor(red_mask, cv2.COLOR_GRAY2BGR)

        for idx, cell_info in enumerate(cells, start=1):
            cell = cell_info["digit"]
            full_cell = cell_info["full"]
            cx, cy, cw, ch = cell_info["digit_box"]
            fx, fy, fw, fh = cell_info["full_box"]
            digit, state_tuple, ratios, std = recognize_digit(cell, cfg)
            decimal_on, decimal_ratio, decimal_nonzero, decimal_box = detect_decimal_point(full_cell, cfg)
            if digit == "":
                decimal_on = False
            value += digit
            if decimal_on:
                value += "."
            if digit == "?":
                unknown_count += 1
            details.append(
                {
                    "roi": name,
                    "position": idx,
                    "digit": digit,
                    "decimal_point": bool(decimal_on),
                    "decimal_ratio": round(decimal_ratio, 4),
                    "decimal_nonzero": int(decimal_nonzero),
                    "state": list(state_tuple),
                    "ratios": {k: round(v, 4) for k, v in ratios.items()},
                }
            )
            if debug:
                imwrite_unicode(debug_base / f"{name}_{idx}_cell.bmp", cell)
                imwrite_unicode(debug_base / f"{name}_{idx}_segments_debug.jpg", make_segment_debug(std, digit, state_tuple, ratios, cfg))
                imwrite_unicode(debug_base / f"{name}_{idx}_decimal_debug.jpg", make_decimal_debug(full_cell, decimal_on, decimal_ratio, decimal_nonzero, decimal_box))
                cv2.rectangle(split_debug, (cx, cy), (cx + cw, cy + ch), (0, 255, 0), 1)
                dx1, dy1, dx2, dy2 = decimal_box
                cv2.rectangle(split_debug, (fx + dx1, fy + dy1), (fx + dx2, fy + dy2), (255, 0, 0) if decimal_on else (0, 0, 255), 1)
                cv2.putText(split_debug, f"{idx}:{digit}{'.' if decimal_on else ''}", (fx + 2, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

        values[name] = value
        if debug:
            imwrite_unicode(debug_base / f"{name}_split_debug.jpg", split_debug)
            display_name = cfg.get("display_names", {}).get(name, name)
            cv2.rectangle(debug_img, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(debug_img, f"{display_name}: {value}", (x, max(y - 10, 20)), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)

    status = "ok" if unknown_count == 0 else "need_check"
    if debug:
        imwrite_unicode(debug_base / "all_result.jpg", debug_img)
    return {"filename": image_name, "status": status, "unknown_count": unknown_count, "values": values, "details": details}


def process_image(image_path: Path, cfg: dict, rois: dict, output_dir: Path, debug: bool):
    img = imread_unicode(image_path)
    if img is None:
        return {"filename": image_path.name, "status": "read_failed", "unknown_count": "", "values": {}, "details": []}
    return process_bgr_image(image_path.name, img, cfg, rois, output_dir, debug)


def collect_images(input_path: Path):
    exts = {".bmp", ".jpg", ".jpeg", ".png", ".tif", ".tiff"}
    if input_path.is_file():
        return [input_path]
    return [p for p in sorted(input_path.iterdir()) if p.suffix.lower() in exts]


def write_batch_outputs(output_dir: Path, cfg: dict, all_results: list[dict]):
    ensure_dir(output_dir)
    csv_path = output_dir / "results.csv"
    header = ["filename"] + [cfg.get("display_names", {}).get(n, n) for n in cfg["roi_names"]] + ["status", "unknown_count"]
    with csv_path.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for result in all_results:
            row = [result["filename"]]
            for name in cfg["roi_names"]:
                row.append(result["values"].get(name, ""))
            row += [result["status"], result["unknown_count"]]
            writer.writerow(row)
    with (output_dir / "details.json").open("w", encoding="utf-8") as f:
        json.dump(all_results, f, ensure_ascii=False, indent=2)


def main():
    parser = argparse.ArgumentParser(description="Batch recognize fixed-position seven-segment display images")
    parser.add_argument("input", help="image file or image directory")
    parser.add_argument("--config", default="config.json")
    parser.add_argument("--rois", default="rois.json")
    parser.add_argument("--output", default="output_live")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    cfg = load_json(args.config)
    rois = load_json(args.rois)
    output_dir = Path(args.output)
    image_files = collect_images(Path(args.input))
    if not image_files:
        raise FileNotFoundError(f"No images found: {args.input}")
    all_results = []
    for i, image_path in enumerate(image_files, start=1):
        print(f"[{i}/{len(image_files)}] processing: {image_path.name}")
        all_results.append(process_image(image_path, cfg, rois, output_dir, args.debug))
    write_batch_outputs(output_dir, cfg, all_results)
    ok_count = sum(1 for r in all_results if r["status"] == "ok")
    print(f"done: total={len(all_results)}, ok={ok_count}, need_check={len(all_results) - ok_count}")
    print(f"CSV: {output_dir / 'results.csv'}")
    print(f"JSON: {output_dir / 'details.json'}")


if __name__ == "__main__":
    main()
