from __future__ import annotations

import argparse
import time
from datetime import datetime
from pathlib import Path

from .hikrobot_camera import HikrobotCamera
from .image_io import imwrite_unicode
from .offline_queue import build_frame_path
from .seven_segment_recognize import ensure_dir


def next_due_after_frame(previous_next_due_s: float, interval_s: float, now_s: float) -> float:
    scheduled_next_s = previous_next_due_s + interval_s
    if scheduled_next_s < now_s:
        return now_s + interval_s
    return scheduled_next_s


def camera_frame_rate_hz(args) -> float:
    if args.frame_rate_hz is not None:
        return float(args.frame_rate_hz)
    return float(args.hz)


def run_capture(args) -> dict:
    queue_dir = Path(args.queue_dir)
    ensure_dir(queue_dir)

    interval_s = 1.0 / float(args.hz)
    max_frames = int(args.frames) if args.frames is not None else None
    deadline_s = None if args.duration_sec is None else time.perf_counter() + float(args.duration_sec)

    stats = {"frames": 0, "overruns": 0}
    start_s = time.perf_counter()
    next_due_s = start_s

    with HikrobotCamera(
        sdk_dirs=args.sdk_dir,
        device_index=args.device_index,
        exposure_us=args.exposure_us,
        gain=args.gain,
        frame_rate_hz=camera_frame_rate_hz(args),
        width=args.camera_width,
        height=args.camera_height,
        offset_x=args.camera_offset_x,
        offset_y=args.camera_offset_y,
    ) as camera:
        frame_index = 0
        while True:
            if max_frames is not None and frame_index >= max_frames:
                break
            if deadline_s is not None and time.perf_counter() >= deadline_s:
                break

            now_s = time.perf_counter()
            if now_s < next_due_s:
                time.sleep(next_due_s - now_s)
            elif frame_index > 0:
                stats["overruns"] += 1

            frame = camera.grab_bgr_frame(timeout_ms=args.timeout_ms)
            timestamp = datetime.now().astimezone()
            frame_path = build_frame_path(queue_dir, frame_index, timestamp, args.image_ext)
            temp_path = frame_path.with_name("_writing_" + frame_path.name)
            if not imwrite_unicode(temp_path, frame):
                raise RuntimeError(f"failed to write frame: {temp_path}")
            temp_path.replace(frame_path)

            stats["frames"] += 1
            frame_index += 1
            next_due_s = next_due_after_frame(next_due_s, interval_s, time.perf_counter())

    elapsed_s = max(1e-9, time.perf_counter() - start_s)
    stats["elapsed_s"] = elapsed_s
    stats["achieved_hz"] = stats["frames"] / elapsed_s
    return stats


def main():
    parser = argparse.ArgumentParser(description="Capture camera frames into an offline recognition queue")
    parser.add_argument("--hz", type=float, default=10.0, help="target capture rate")
    parser.add_argument("--duration-sec", type=float, default=None, help="stop after this many seconds")
    parser.add_argument("--frames", type=int, default=None, help="stop after this many frames")
    parser.add_argument("--queue-dir", default="frame_queue")
    parser.add_argument("--image-ext", default="bmp", choices=["bmp", "png", "jpg", "jpeg"])
    parser.add_argument("--sdk-dir", action="append", default=[])
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--exposure-us", type=float, default=None)
    parser.add_argument("--gain", type=float, default=None)
    parser.add_argument("--frame-rate-hz", type=float, default=None, help="optional camera-side acquisition frame rate; defaults to --hz")
    parser.add_argument("--camera-width", type=int, default=None, help="optional hardware ROI width; leave unset to keep full image")
    parser.add_argument("--camera-height", type=int, default=None, help="optional hardware ROI height; leave unset to keep full image")
    parser.add_argument("--camera-offset-x", type=int, default=None, help="optional hardware ROI horizontal offset")
    parser.add_argument("--camera-offset-y", type=int, default=None, help="optional hardware ROI vertical offset")
    parser.add_argument("--timeout-ms", type=int, default=1000)
    args = parser.parse_args()

    if args.hz > 30 and args.image_ext == "bmp":
        print("warning: high-rate BMP capture can consume a lot of disk bandwidth.")
    if args.camera_width or args.camera_height:
        print("warning: camera hardware ROI changes the image coordinate system; recalibrate rois.json before recognition.")

    stats = run_capture(args)
    print(
        "done: frames={frames}, overruns={overruns}, "
        "elapsed={elapsed_s:.3f}s, achieved_hz={achieved_hz:.2f}".format(**stats)
    )


if __name__ == "__main__":
    main()
