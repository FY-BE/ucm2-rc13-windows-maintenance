from __future__ import annotations

from pathlib import Path

import cv2
import numpy as np


def imread_unicode(path: str | Path, flags: int = cv2.IMREAD_COLOR):
    try:
        data = np.fromfile(str(path), dtype=np.uint8)
    except OSError:
        return None
    if data.size == 0:
        return None
    return cv2.imdecode(data, flags)


def imwrite_unicode(path: str | Path, image, params=None) -> bool:
    ext = Path(path).suffix
    if not ext:
        raise ValueError(f"image path has no extension: {path}")
    ok, encoded = cv2.imencode(ext, image, params or [])
    if not ok:
        return False
    encoded.tofile(str(path))
    return True
