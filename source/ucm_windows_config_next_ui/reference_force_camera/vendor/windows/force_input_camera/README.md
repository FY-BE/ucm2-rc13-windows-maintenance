# Force Input Camera Tool

Python tool for Hikrobot MVS camera capture and fixed-position seven-segment display recognition used as Windows calibration external force input.

This directory was migrated from the former top-level `seven_segment_camera/`. In calibration mode, `S1/S2/S3/S4` are no longer generic display fields: they are the external force values for tiebar 1/2/3/4, in kN.

## 1. Environment

Install dependencies:

```bat
pip install -r requirements.txt
```

MVS must be installed. The tool searches these default locations:

```text
C:\Program Files (x86)\MVS
C:\Program Files\MVS
C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64
```

If the SDK is elsewhere, set:

```bat
set HIKROBOT_MVS_PYTHON=C:\Program Files (x86)\MVS\Development\Samples\Python\MvImport
```

Close the MVS client before running Python capture. Otherwise the camera may return `0x80000203` access denied.

## 2. Capture

```bat
cd "G:\Ultra ClampMonitor\Project refactoring code"
python -m windows_calibration.force_input_camera.capture_once --output captures\panel.bmp --timeout-ms 3000
```

Optional exposure and gain:

```bat
python -m windows_calibration.force_input_camera.capture_once --output captures\panel.bmp --exposure-us 11361 --gain 0
```

## 3. Calibrate ROI

Use a clear live-captured image:

```bat
python -m windows_calibration.force_input_camera.calibrate_rois captures\panel.bmp
```

Select in order:

```text
S1
S2
S3
S4
Total
Other
```

Only box the numeric display window. Do not include labels, buttons, enclosure edges, or unit LEDs.

## 4. Recognize

Capture and recognize in one command:

```bat
python -m windows_calibration.force_input_camera.capture_once --output captures\panel.bmp --recognize --recognize-output output_live --debug
```

Recognize an existing image:

```bat
python -m windows_calibration.force_input_camera.seven_segment_recognize captures\panel.bmp --output output_live --debug
```

Outputs:

```text
output_live\results.csv
output_live\details.json
output_live\debug\<image_name>\*.jpg / *.bmp
```

Debug order:

1. `*_red_mask.bmp`: whether red extraction is correct.
2. `*_split_debug.jpg`: whether each digit cell is split correctly.
3. `*_segments_debug.jpg`: whether seven-segment boxes match lit segments.
4. `*_decimal_debug.jpg`: whether decimal point ROI is correct.

## 5. Current Notes

The current focus looks usable. If recognition still returns many `?`, first recalibrate ROI from a fresh live image, then tune `config.json` segment polygons and thresholds.

## 6. Offline Capture + Recognition

Recommended workflow when capture timing is more important than real-time recognition:

1. Capture frames into a temporary queue. Each image filename stores frame index and millisecond timestamp.

```bat
python -m windows_calibration.force_input_camera.capture_queue --hz 35 --frames 300 --queue-dir frame_queue
```

`capture_queue.py` sets the camera-side `AcquisitionFrameRate` to `--hz` by default so the camera buffer does not accumulate stale frames. Use `--frame-rate-hz` only when you intentionally want a different camera-side rate.

2. Recognize queued images later. Only high-confidence rows are written to CSV. Processed images are deleted by default.

```bat
python -m windows_calibration.force_input_camera.recognize_queue --queue-dir frame_queue --fields S1,S2,S3,S4 --min-confidence 0.9 --output output_live\values.csv
```

Continuous delayed recognition is also possible:

```bat
python -m windows_calibration.force_input_camera.recognize_queue --queue-dir frame_queue --fields S1,S2,S3,S4 --min-confidence 0.9 --output output_live\values.csv --watch
```

Useful options:

```text
--fields S1,S2,S3,S4     Process calibration force input fields.
--keep-rejected          Keep low-confidence images for ROI/threshold tuning.
--debug                  Save recognition debug images; slow, use only for tuning.
```

Recognition rejects empty selected fields by default (`require_nonempty_values=true` in `config.json`). Corrupted images are moved to `frame_queue\failed\` instead of being retried forever. Existing CSV files are checked before appending; if the header does not match the current `--fields`, use a new output file.

The timestamp is the PC-side frame receive/write time with millisecond formatting. If exposure-time hardware timestamp is required later, extend the camera wrapper to export MVS frame metadata.

`force_input_mapper.py` converts recognized S1/S2/S3/S4 values into `forceInput_v1`, with `force_kN[4]` mapped in tiebar order.
