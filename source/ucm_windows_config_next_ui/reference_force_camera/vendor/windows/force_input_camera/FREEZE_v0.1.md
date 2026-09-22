# Force Input Camera Freeze v0.1

Date: 2026-06-27

## Frozen Decision

This tool uses an offline capture and recognition pipeline for Windows calibration external force input.

S1/S2/S3/S4 are frozen as tiebar 1/2/3/4 force inputs, in kN.

The capture side prioritizes image quality and timestamp stability. Recognition is allowed to lag behind capture and runs from queued images. After recognition, images are deleted by default. The final retained data is CSV only.

## Frozen Capture Strategy

- Keep current focus, exposure, full image quality, and current ROI calibration.
- Do not reduce image quality just to chase 100Hz.
- Do not enable camera hardware ROI by default, because it changes image coordinates and requires ROI recalibration.
- Do not do real-time recognition in the capture loop.
- Capture frames into a temporary queue with millisecond timestamp embedded in the filename.
- Set camera-side `AcquisitionFrameRate` to the requested `--hz` by default, so queued frames do not become stale due to camera buffer backlog.

## Measured Rate Baseline

Measured on 2026-06-27 with full-image BMP capture and current camera settings:

| Target Hz | Measured Timestamp Hz | Avg Interval |
|---:|---:|---:|
| 10 | 9.97 | 100.3 ms |
| 15 | 14.97 | 66.8 ms |
| 20 | 19.93 | 50.2 ms |
| 25 | 24.89 | 40.2 ms |
| 30 | 29.84 | 33.5 ms |
| 35 | 34.79 | 28.7 ms |
| 40 | about 39.3 | 25.4 ms |
| 45+ | about 39.4 | 25.4 ms |

Practical frozen recommendation:

```bat
python -m windows_calibration.force_input_camera.capture_queue --hz 35 --queue-dir frame_queue
```

Short test bursts may use:

```bat
python -m windows_calibration.force_input_camera.capture_queue --hz 40 --queue-dir frame_queue
```

## Frozen Recognition Strategy

Recognize queued images offline:

```bat
python -m windows_calibration.force_input_camera.recognize_queue --queue-dir frame_queue --fields S1,S2,S3,S4 --min-confidence 0.9 --output output_live\values.csv
```

For continuous delayed recognition:

```bat
python -m windows_calibration.force_input_camera.recognize_queue --queue-dir frame_queue --fields S1,S2,S3,S4 --min-confidence 0.9 --output output_live\values.csv --watch
```

Calibration fields:

- `S1`: tiebar 1 external force, kN
- `S2`: tiebar 2 external force, kN
- `S3`: tiebar 3 external force, kN
- `S4`: tiebar 4 external force, kN

## Retained Data Contract

The final CSV keeps:

```text
frame_index,timestamp_ms,confidence,status,unknown_count,recognition_latency_ms,<field values>
```

The timestamp is PC-side frame receive/write time formatted to milliseconds, for example:

```text
2026-06-27T20:40:50.208+08:00
```

Queued images are deleted after processing by default. Use `--keep-rejected` only when tuning ROI or thresholds.

Frozen data guards:

- Empty selected fields are rejected by default (`require_nonempty_values=true`).
- Corrupted images are moved to `failed/` under the queue directory.
- Existing CSV headers must match the current field list before appending.
- Image reads/writes use Unicode-safe wrappers for Windows Chinese paths.

## Not Frozen Yet

These items are intentionally not frozen:

- `Total` and `Other` use in calibration computation.
- Hardware timestamp extraction from MVS frame metadata.
- Camera hardware ROI / reduced resolution mode.
- 100Hz capture, unless image quality or camera settings are allowed to change later.

## Verification Commands

Unit tests:

```bat
python -m pytest windows_calibration\force_input_camera\tests -q
```

Full-chain smoke test:

```bat
python -m windows_calibration.force_input_camera.capture_queue --hz 35 --frames 80 --queue-dir output_live\freeze_smoke_queue
python -m windows_calibration.force_input_camera.recognize_queue --queue-dir output_live\freeze_smoke_queue --fields S1,S2,S3,S4 --min-confidence 0.9 --output output_live\freeze_smoke_values.csv
```
