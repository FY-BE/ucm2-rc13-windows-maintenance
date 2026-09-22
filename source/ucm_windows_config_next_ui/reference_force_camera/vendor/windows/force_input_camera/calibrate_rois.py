from __future__ import annotations

import argparse
import json
from pathlib import Path

import cv2

from .image_io import imread_unicode, imwrite_unicode
from .seven_segment_recognize import load_json, rotate_if_needed


def resize_for_display(img, max_width=1200, max_height=800):
    h, w = img.shape[:2]
    scale = min(max_width / w, max_height / h, 1.0)
    display = cv2.resize(img, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)
    return display, scale


def main():
    parser = argparse.ArgumentParser(description="Calibrate fixed ROIs for seven-segment panel")
    parser.add_argument("image", help="calibration image, captured by capture_once.py")
    parser.add_argument("--config", default="config.json")
    parser.add_argument("--out", default="rois.json")
    parser.add_argument("--max-width", type=int, default=1200)
    parser.add_argument("--max-height", type=int, default=800)
    args = parser.parse_args()

    cfg = load_json(args.config)
    img = imread_unicode(args.image)
    if img is None:
        raise FileNotFoundError(f"Cannot read image: {args.image}")
    img = rotate_if_needed(img, cfg)
    display_img, scale = resize_for_display(img, args.max_width, args.max_height)

    rois = {}
    for name in cfg["roi_names"]:
        display_name = cfg.get("display_names", {}).get(name, name)
        print(f"Select ROI for {display_name}; Enter/Space to confirm, C/Esc to skip.")
        roi = cv2.selectROI(f"Select {display_name}", display_img, showCrosshair=True, fromCenter=False)
        x, y, w, h = map(int, roi)
        cv2.destroyWindow(f"Select {display_name}")
        if w == 0 or h == 0:
            print(f"Skipped {display_name}")
            continue
        rois[name] = [int(round(x / scale)), int(round(y / scale)), int(round(w / scale)), int(round(h / scale))]
        print(f"{display_name}: {rois[name]}")

    cv2.destroyAllWindows()
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(rois, f, ensure_ascii=False, indent=2)

    preview = img.copy()
    for name, (x, y, w, h) in rois.items():
        display_name = cfg.get("display_names", {}).get(name, name)
        cv2.rectangle(preview, (x, y), (x + w, y + h), (0, 255, 0), 2)
        cv2.putText(preview, display_name, (x, max(y - 8, 20)), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
    imwrite_unicode("rois_preview.jpg", preview)
    preview_small, _ = resize_for_display(preview, args.max_width, args.max_height)
    imwrite_unicode("rois_preview_small.jpg", preview_small)
    print(f"Saved: {args.out}")
    print("Preview: rois_preview.jpg, rois_preview_small.jpg")


if __name__ == "__main__":
    main()
