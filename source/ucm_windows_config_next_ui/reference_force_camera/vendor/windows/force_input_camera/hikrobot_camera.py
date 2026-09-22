from __future__ import annotations

import ctypes
import importlib
import os
import sys
from pathlib import Path
from typing import Iterable


SDK_MODULE = "MvCameraControl_class"
SDK_FILE = f"{SDK_MODULE}.py"

DEFAULT_SDK_SEARCH_DIRS = [
    Path(r"C:\Program Files (x86)\MVS"),
    Path(r"C:\Program Files\MVS"),
]

DEFAULT_RUNTIME_SEARCH_DIRS = [
    Path(r"C:\Program Files (x86)\Common Files\MVS\Runtime"),
    Path(r"C:\Program Files\Common Files\MVS\Runtime"),
]

KNOWN_SDK_RELATIVE_PATHS = [
    Path(SDK_FILE),
    Path("MvImport") / SDK_FILE,
    Path("Samples") / "Python" / "MvImport" / SDK_FILE,
    Path("Development") / "Samples" / "Python" / "MvImport" / SDK_FILE,
]

PIXEL_TYPE_FALLBACKS = {
    "PixelType_Gvsp_Mono8": 0x01080001,
    "PixelType_Gvsp_BayerGR8": 0x01080008,
    "PixelType_Gvsp_BayerRG8": 0x01080009,
    "PixelType_Gvsp_BayerGB8": 0x0108000A,
    "PixelType_Gvsp_BayerBG8": 0x0108000B,
    "PixelType_Gvsp_RGB8_Packed": 0x02180014,
    "PixelType_Gvsp_BGR8_Packed": 0x02180015,
}

_DLL_DIR_HANDLES = []


class MvsSdkNotFoundError(RuntimeError):
    pass


class MvsCameraError(RuntimeError):
    pass


def _iter_candidate_dirs(extra_dirs: Iterable[str | Path] | None):
    for raw in extra_dirs or []:
        if raw:
            yield Path(raw).expanduser()

    env_value = os.environ.get("HIKROBOT_MVS_PYTHON")
    if env_value:
        for raw in env_value.split(os.pathsep):
            if raw:
                yield Path(raw).expanduser()

    yield from DEFAULT_SDK_SEARCH_DIRS


def find_sdk_python_file(extra_dirs: Iterable[str | Path] | None = None) -> Path | None:
    for candidate in _iter_candidate_dirs(extra_dirs):
        if candidate.is_file() and candidate.name == SDK_FILE:
            return candidate
        if not candidate.is_dir():
            continue
        for relative in KNOWN_SDK_RELATIVE_PATHS:
            sdk_file = candidate / relative
            if sdk_file.is_file():
                return sdk_file
    return None


def _runtime_candidates(extra_dirs: Iterable[str | Path] | None):
    preferred = "Win64_x64" if sys.maxsize > 2**32 else "Win32_i86"
    for raw in extra_dirs or []:
        if raw:
            base = Path(raw).expanduser()
            yield base
            yield base / "Runtime" / preferred
    for base in DEFAULT_RUNTIME_SEARCH_DIRS:
        yield base / preferred
        yield base


def prepare_mvs_runtime_path(extra_dirs: Iterable[str | Path] | None = None) -> list[Path]:
    added: list[Path] = []
    path_parts = os.environ.get("PATH", "").split(os.pathsep) if os.environ.get("PATH") else []
    for candidate in _runtime_candidates(extra_dirs):
        if not candidate.is_dir():
            continue
        if not (candidate / "MvCameraControl.dll").is_file():
            continue
        candidate_str = str(candidate)
        if hasattr(os, "add_dll_directory"):
            _DLL_DIR_HANDLES.append(os.add_dll_directory(candidate_str))
        if candidate_str not in path_parts:
            path_parts.insert(0, candidate_str)
            added.append(candidate)
    if added:
        os.environ["PATH"] = os.pathsep.join(path_parts)
    return added


def load_mvs_sdk(extra_dirs: Iterable[str | Path] | None = None):
    sdk_file = find_sdk_python_file(extra_dirs)
    if sdk_file is None:
        searched = [str(p) for p in _iter_candidate_dirs(extra_dirs)]
        raise MvsSdkNotFoundError(
            "Cannot find Hikrobot MVS Python SDK wrapper MvCameraControl_class.py.\n"
            "Install MVS SDK or set HIKROBOT_MVS_PYTHON to the MvImport directory.\n"
            "Searched:\n- " + "\n- ".join(searched)
        )

    sdk_dir = str(sdk_file.parent)
    if sdk_dir not in sys.path:
        sys.path.insert(0, sdk_dir)
    prepare_mvs_runtime_path(extra_dirs)
    try:
        return importlib.import_module(SDK_MODULE)
    except Exception as exc:
        raise MvsCameraError(f"MVS SDK import failed: {sdk_file}\n{exc}") from exc


def _sdk_const(sdk, name: str):
    return int(getattr(sdk, name, PIXEL_TYPE_FALLBACKS[name]))


def _check_ret(ret: int, sdk, action: str):
    ok = int(getattr(sdk, "MV_OK", 0))
    if int(ret) != ok:
        raise MvsCameraError(f"{action} failed, return=0x{int(ret) & 0xFFFFFFFF:08X}")


def bayer_conversion_code(pixel_type: int, sdk=None):
    import cv2

    if sdk is None:
        sdk = object()
    return {
        _sdk_const(sdk, "PixelType_Gvsp_BayerGR8"): cv2.COLOR_BAYER_GB2BGR,
        _sdk_const(sdk, "PixelType_Gvsp_BayerRG8"): cv2.COLOR_BAYER_BG2BGR,
        _sdk_const(sdk, "PixelType_Gvsp_BayerGB8"): cv2.COLOR_BAYER_GR2BGR,
        _sdk_const(sdk, "PixelType_Gvsp_BayerBG8"): cv2.COLOR_BAYER_RG2BGR,
    }.get(int(pixel_type))


def convert_frame_to_bgr(raw: bytes, width: int, height: int, pixel_type: int, sdk=None):
    import cv2
    import numpy as np

    if sdk is None:
        sdk = object()
    arr = np.frombuffer(raw, dtype=np.uint8)

    if pixel_type == _sdk_const(sdk, "PixelType_Gvsp_Mono8"):
        return cv2.cvtColor(arr.reshape((height, width)), cv2.COLOR_GRAY2BGR)
    if pixel_type == _sdk_const(sdk, "PixelType_Gvsp_RGB8_Packed"):
        return cv2.cvtColor(arr.reshape((height, width, 3)), cv2.COLOR_RGB2BGR)
    if pixel_type == _sdk_const(sdk, "PixelType_Gvsp_BGR8_Packed"):
        return arr.reshape((height, width, 3)).copy()

    bayer_code = bayer_conversion_code(pixel_type, sdk)
    if bayer_code is not None:
        return cv2.cvtColor(arr.reshape((height, width)), bayer_code)

    raise MvsCameraError(
        f"Unsupported PixelType=0x{int(pixel_type) & 0xFFFFFFFF:08X}. "
        "Set PixelFormat to Mono8, Bayer8, RGB8Packed, or BGR8Packed."
    )


class HikrobotCamera:
    def __init__(
        self,
        sdk_dirs: Iterable[str | Path] | None = None,
        device_index: int = 0,
        exposure_us: float | None = None,
        gain: float | None = None,
        frame_rate_hz: float | None = None,
        width: int | None = None,
        height: int | None = None,
        offset_x: int | None = None,
        offset_y: int | None = None,
    ):
        self.sdk = load_mvs_sdk(sdk_dirs)
        self.device_index = int(device_index)
        self.exposure_us = exposure_us
        self.gain = gain
        self.frame_rate_hz = frame_rate_hz
        self.width = width
        self.height = height
        self.offset_x = offset_x
        self.offset_y = offset_y
        self.camera = None
        self._is_grabbing = False

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def open(self):
        sdk = self.sdk
        device_list = sdk.MV_CC_DEVICE_INFO_LIST()
        tlayer_type = int(getattr(sdk, "MV_GIGE_DEVICE", 1)) | int(getattr(sdk, "MV_USB_DEVICE", 4))
        _check_ret(sdk.MvCamera.MV_CC_EnumDevices(tlayer_type, device_list), sdk, "EnumDevices")

        device_count = int(device_list.nDeviceNum)
        if device_count <= 0:
            raise MvsCameraError("No Hikrobot camera found. Check power, cable, IP, driver, and firewall.")
        if self.device_index >= device_count:
            raise MvsCameraError(f"device_index={self.device_index} out of range; found {device_count} cameras.")

        device_info = ctypes.cast(
            device_list.pDeviceInfo[self.device_index],
            ctypes.POINTER(sdk.MV_CC_DEVICE_INFO),
        ).contents

        camera = sdk.MvCamera()
        _check_ret(camera.MV_CC_CreateHandle(device_info), sdk, "CreateHandle")
        access_mode = int(getattr(sdk, "MV_ACCESS_Exclusive", 1))
        _check_ret(camera.MV_CC_OpenDevice(access_mode, 0), sdk, "OpenDevice")
        self.camera = camera
        self._configure_after_open(device_info)
        return self

    def _configure_after_open(self, device_info):
        sdk = self.sdk
        assert self.camera is not None
        if int(getattr(device_info, "nTLayerType", 0)) == int(getattr(sdk, "MV_GIGE_DEVICE", 1)):
            packet_size = int(self.camera.MV_CC_GetOptimalPacketSize())
            if packet_size > 0:
                _check_ret(self.camera.MV_CC_SetIntValue("GevSCPSPacketSize", packet_size), sdk, "SetPacketSize")
        _check_ret(self.camera.MV_CC_SetEnumValue("TriggerMode", int(getattr(sdk, "MV_TRIGGER_MODE_OFF", 0))), sdk, "TriggerOff")
        if self.exposure_us is not None:
            _check_ret(self.camera.MV_CC_SetEnumValue("ExposureAuto", 0), sdk, "DisableExposureAuto")
            _check_ret(self.camera.MV_CC_SetFloatValue("ExposureTime", float(self.exposure_us)), sdk, "SetExposureTime")
        if self.gain is not None:
            _check_ret(self.camera.MV_CC_SetEnumValue("GainAuto", 0), sdk, "DisableGainAuto")
            _check_ret(self.camera.MV_CC_SetFloatValue("Gain", float(self.gain)), sdk, "SetGain")
        if self.frame_rate_hz is not None:
            _check_ret(self.camera.MV_CC_SetBoolValue("AcquisitionFrameRateEnable", True), sdk, "EnableFrameRate")
            _check_ret(self.camera.MV_CC_SetFloatValue("AcquisitionFrameRate", float(self.frame_rate_hz)), sdk, "SetFrameRate")
        if self.width is not None:
            _check_ret(self.camera.MV_CC_SetIntValue("Width", int(self.width)), sdk, "SetWidth")
        if self.height is not None:
            _check_ret(self.camera.MV_CC_SetIntValue("Height", int(self.height)), sdk, "SetHeight")
        if self.offset_x is not None:
            _check_ret(self.camera.MV_CC_SetIntValue("OffsetX", int(self.offset_x)), sdk, "SetOffsetX")
        if self.offset_y is not None:
            _check_ret(self.camera.MV_CC_SetIntValue("OffsetY", int(self.offset_y)), sdk, "SetOffsetY")

    def start_grabbing(self):
        if self.camera is None:
            raise MvsCameraError("Camera is not open.")
        if not self._is_grabbing:
            _check_ret(self.camera.MV_CC_StartGrabbing(), self.sdk, "StartGrabbing")
            self._is_grabbing = True

    def stop_grabbing(self):
        if self.camera is not None and self._is_grabbing:
            _check_ret(self.camera.MV_CC_StopGrabbing(), self.sdk, "StopGrabbing")
            self._is_grabbing = False

    def grab_bgr_frame(self, timeout_ms: int = 1000):
        if self.camera is None:
            raise MvsCameraError("Camera is not open.")
        self.start_grabbing()
        sdk = self.sdk
        frame = sdk.MV_FRAME_OUT()
        ctypes.memset(ctypes.byref(frame), 0, ctypes.sizeof(frame))
        _check_ret(self.camera.MV_CC_GetImageBuffer(frame, int(timeout_ms)), sdk, "GetImageBuffer")
        try:
            info = frame.stFrameInfo
            raw = ctypes.string_at(frame.pBufAddr, int(info.nFrameLen))
            return convert_frame_to_bgr(raw, int(info.nWidth), int(info.nHeight), int(info.enPixelType), sdk)
        finally:
            self.camera.MV_CC_FreeImageBuffer(frame)

    def close(self):
        if self.camera is None:
            return
        try:
            self.stop_grabbing()
        finally:
            try:
                self.camera.MV_CC_CloseDevice()
            finally:
                self.camera.MV_CC_DestroyHandle()
                self.camera = None
