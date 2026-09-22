from __future__ import annotations


def _segment_confidence(detail: dict, cfg: dict) -> float:
    if detail.get("digit") == "?":
        return 0.0

    thresholds = cfg.get("segment_thresholds", {})
    ratios = detail.get("ratios", {})
    states = detail.get("state", [])
    seg_names = ["A", "B", "C", "D", "E", "F", "G"]
    scores = []
    for seg, state in zip(seg_names, states):
        threshold = float(thresholds.get(seg, 0.2))
        ratio = float(ratios.get(seg, 0.0))
        if state:
            raw_margin = (ratio - threshold) / max(1e-9, 1.0 - threshold)
        else:
            raw_margin = (threshold - ratio) / max(1e-9, threshold)
        scores.append(max(0.0, min(1.0, raw_margin / 0.35)))

    dp_cfg = cfg.get("decimal_point", {})
    if dp_cfg.get("enabled", False):
        threshold = float(dp_cfg.get("threshold", 0.3))
        ratio = float(detail.get("decimal_ratio", 0.0))
        if detail.get("decimal_point", False):
            raw_margin = (ratio - threshold) / max(1e-9, 1.0 - threshold)
        else:
            raw_margin = (threshold - ratio) / max(1e-9, threshold)
        scores.append(max(0.0, min(1.0, raw_margin / 0.35)))

    if not scores:
        return 0.0
    return sum(scores) / len(scores)


def confidence_from_result(result: dict, cfg: dict) -> float:
    if cfg.get("require_nonempty_values", True):
        values = result.get("values") or {}
        for name in cfg.get("roi_names", []):
            if str(values.get(name, "")).strip() == "":
                return 0.0

    details = result.get("details") or []
    if not details:
        return 1.0 if result.get("status") == "ok" else 0.0

    unknown_count = int(result.get("unknown_count") or 0)
    known_fraction = max(0.0, 1.0 - unknown_count / max(1, len(details)))
    segment_scores = [_segment_confidence(detail, cfg) for detail in details]
    segment_confidence = sum(segment_scores) / len(segment_scores)
    return max(0.0, min(1.0, known_fraction * segment_confidence))


def should_accept_result(result: dict, cfg: dict, min_confidence: float) -> bool:
    return confidence_from_result(result, cfg) >= float(min_confidence)


def apply_field_filter(cfg: dict, fields_csv: str | None) -> dict:
    if not fields_csv:
        return cfg

    requested = [name.strip() for name in fields_csv.split(",") if name.strip()]
    known = set(cfg["roi_names"])
    unknown = [name for name in requested if name not in known]
    if unknown:
        raise ValueError(f"unknown field(s): {', '.join(unknown)}")

    filtered = dict(cfg)
    filtered["roi_names"] = requested
    return filtered
