from __future__ import annotations

import re
from datetime import datetime
from pathlib import Path


FRAME_NAME_RE = re.compile(r"^frame_(?P<index>\d{8})_(?P<ts>\d{8}T\d{9}[+-]\d{4})\.(?P<ext>bmp|png|jpg|jpeg)$", re.IGNORECASE)


def timestamp_token(timestamp: datetime) -> str:
    if timestamp.tzinfo is None or timestamp.utcoffset() is None:
        raise ValueError("timestamp must be timezone-aware")
    return (
        timestamp.strftime("%Y%m%dT%H%M%S")
        + f"{timestamp.microsecond // 1000:03d}"
        + timestamp.strftime("%z")
    )


def build_frame_path(queue_dir: Path, frame_index: int, timestamp: datetime, ext: str = "bmp") -> Path:
    clean_ext = ext.lower().lstrip(".")
    return queue_dir / f"frame_{int(frame_index):08d}_{timestamp_token(timestamp)}.{clean_ext}"


def parse_frame_path(path: Path) -> tuple[int, datetime]:
    match = FRAME_NAME_RE.match(path.name)
    if not match:
        raise ValueError(f"not a queued frame filename: {path.name}")
    token = match.group("ts")
    timestamp = datetime.strptime(
        token[:15] + token[15:18] + "000" + token[18:],
        "%Y%m%dT%H%M%S%f%z",
    )
    return int(match.group("index")), timestamp


def iter_frame_paths(queue_dir: Path):
    paths = []
    for path in Path(queue_dir).iterdir():
        if path.is_file() and FRAME_NAME_RE.match(path.name):
            paths.append(path)
    return sorted(paths, key=lambda path: parse_frame_path(path)[0])
