from __future__ import annotations

from math import isfinite
from typing import Any


FORCE_INPUT_SOURCES = frozenset({"force_input_camera", "simulated_force", "operator_csv"})


class ForceInputValidationError(ValueError):
    def __init__(self, reason_code: str, message: str | None = None):
        self.reason_code = reason_code
        super().__init__(message or reason_code)


def map_camera_result_to_force_input(
    camera_result: dict[str, Any],
    *,
    min_confidence: float = 0.9,
) -> dict[str, Any]:
    if not isinstance(camera_result, dict):
        raise ForceInputValidationError("schema_type_mismatch")
    confidence = _finite_number(camera_result.get("confidence"), "confidence")
    if confidence < min_confidence:
        raise ForceInputValidationError("force_low_confidence")
    if camera_result.get("status") != "ok":
        raise ForceInputValidationError("force_value_invalid", "status")
    timestamp_ms = _finite_number(camera_result.get("timestamp_ms"), "timestamp_ms")
    values = camera_result.get("values")
    if not isinstance(values, dict):
        raise ForceInputValidationError("force_value_invalid", "values")
    force_kN = [_parse_force_value(values.get(field_name), field_name) for field_name in ("S1", "S2", "S3", "S4")]
    output = {
        "schema_version": "forceInput_v1",
        "timestamp_ms": int(timestamp_ms) if timestamp_ms.is_integer() else timestamp_ms,
        "force_kN": force_kN,
        "source": "force_input_camera",
        "evidence_level": "force_input_camera",
        "confidence": confidence,
        "status": "ok",
    }
    if "sample_id" in camera_result:
        output["sample_id"] = str(camera_result["sample_id"])
    return output


def validate_force_input_payload(payload: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ForceInputValidationError("schema_type_mismatch")
    if payload.get("schema_version") != "forceInput_v1":
        raise ForceInputValidationError("schema_version_mismatch")
    timestamp_ms = _finite_number(payload.get("timestamp_ms"), "timestamp_ms")
    force_values = payload.get("force_kN")
    if not isinstance(force_values, list) or len(force_values) != 4:
        raise ForceInputValidationError("force_value_invalid", "force_kN")
    output = dict(payload)
    output["timestamp_ms"] = int(timestamp_ms) if timestamp_ms.is_integer() else timestamp_ms
    output["force_kN"] = [_parse_force_value(item, f"force_kN[{index}]") for index, item in enumerate(force_values)]
    source = output.get("source")
    if source not in FORCE_INPUT_SOURCES:
        raise ForceInputValidationError("force_value_invalid", "source")
    evidence_level = output.get("evidence_level")
    if source == "simulated_force":
        if evidence_level != "simulated":
            raise ForceInputValidationError("force_value_invalid", "evidence_level")
    elif source == "operator_csv":
        if evidence_level not in (None, "operator_csv"):
            raise ForceInputValidationError("force_value_invalid", "evidence_level")
        output["evidence_level"] = "operator_csv"
    else:
        if evidence_level not in (None, "force_input_camera"):
            raise ForceInputValidationError("force_value_invalid", "evidence_level")
        output["evidence_level"] = "force_input_camera"
    confidence = _finite_number(output.get("confidence"), "confidence")
    if confidence < 0.0 or confidence > 1.0:
        raise ForceInputValidationError("force_value_invalid", "confidence")
    if output.get("status") != "ok":
        raise ForceInputValidationError("force_value_invalid", "status")
    output["confidence"] = confidence
    return output


def build_simulated_force_input(
    *,
    timestamp_ms: int | float,
    force_kN: list[Any],
    sample_id: str | None = None,
    confidence: float = 1.0,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "schema_version": "forceInput_v1",
        "timestamp_ms": timestamp_ms,
        "force_kN": force_kN,
        "source": "simulated_force",
        "evidence_level": "simulated",
        "confidence": confidence,
        "status": "ok",
    }
    if sample_id is not None:
        payload["sample_id"] = str(sample_id)
    return validate_force_input_payload(payload)


def _parse_force_value(value: Any, field_name: str) -> float:
    if isinstance(value, bool):
        raise ForceInputValidationError("schema_type_mismatch", field_name)
    if isinstance(value, str):
        text = value.strip()
        if not text:
            raise ForceInputValidationError("force_value_invalid", field_name)
        try:
            value_float = float(text)
        except ValueError as exc:
            raise ForceInputValidationError("force_value_invalid", field_name) from exc
    elif isinstance(value, int | float):
        value_float = float(value)
    else:
        raise ForceInputValidationError("force_value_invalid", field_name)
    if not isfinite(value_float) or value_float < 0.0:
        raise ForceInputValidationError("force_value_invalid", field_name)
    return value_float


def _finite_number(value: Any, field_name: str) -> float:
    if not isinstance(value, int | float) or isinstance(value, bool):
        raise ForceInputValidationError("schema_type_mismatch", field_name)
    value_float = float(value)
    if not isfinite(value_float):
        raise ForceInputValidationError("schema_type_mismatch", field_name)
    return value_float
