from __future__ import annotations

import csv
import math
from dataclasses import asdict, dataclass


DIAGNOSTIC_PREFIX = 'C_KEY_DIAG_V1'
DIAGNOSTIC_FIELDS = (
    'timestamp_ms', 'sequence', 'tag_id', 'accepted_id', 'link_ok',
    'valid_mask', 'measurement_ready', 'pose_valid', 'raw_a0_mm',
    'raw_a1_mm', 'corrected_a0_mm', 'corrected_a1_mm',
    'filtered_a0_mm', 'filtered_a1_mm', 'x_m', 'y_m', 'boundary_m',
    'angle_deg', 'residual_m', 'state', 'events', 'accepted_frames',
    'rejected_frames',
)


class TelemetryParseError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class DiagnosticFrame:
    timestamp_ms: int
    sequence: int
    tag_id: int
    accepted_id: int
    link_ok: bool
    valid_mask: int
    measurement_ready: bool
    pose_valid: bool
    raw_a0_mm: int
    raw_a1_mm: int
    corrected_a0_mm: float
    corrected_a1_mm: float
    filtered_a0_mm: float
    filtered_a1_mm: float
    x_m: float
    y_m: float
    boundary_m: float
    angle_deg: float
    residual_m: float
    state: str
    events: int
    accepted_frames: int
    rejected_frames: int

    def as_csv_values(self) -> list[str]:
        values = asdict(self)
        return [
            '1' if values[name] is True else
            '0' if values[name] is False else
            str(values[name])
            for name in DIAGNOSTIC_FIELDS
        ]


def extract_diagnostic_payload(line: str) -> str | None:
    marker = f'{DIAGNOSTIC_PREFIX},'
    index = line.find(marker)
    if index < 0:
        return None
    return line[index:].strip()


def _parse_int(text: str, name: str, minimum: int = 0) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise TelemetryParseError(f'{name}不是整数: {text!r}') from exc
    if value < minimum:
        raise TelemetryParseError(f'{name}小于{minimum}: {value}')
    return value


def _parse_float(text: str, name: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise TelemetryParseError(f'{name}不是浮点数: {text!r}') from exc
    if not math.isfinite(value):
        raise TelemetryParseError(f'{name}不是有限值: {value}')
    return value


def _parse_bool(text: str, name: str) -> bool:
    value = _parse_int(text, name)
    if value not in (0, 1):
        raise TelemetryParseError(f'{name}必须为0或1: {value}')
    return bool(value)


def parse_diagnostic_line(line: str) -> DiagnosticFrame | None:
    payload = extract_diagnostic_payload(line)
    if payload is None:
        return None
    row = next(csv.reader([payload]))
    expected_columns = len(DIAGNOSTIC_FIELDS) + 1
    if len(row) != expected_columns:
        raise TelemetryParseError(
            f'诊断列数错误: 得到{len(row)}列，期望{expected_columns}列'
        )
    if row[0] != DIAGNOSTIC_PREFIX:
        raise TelemetryParseError(f'诊断版本错误: {row[0]!r}')
    return DiagnosticFrame(
        timestamp_ms=_parse_int(row[1], 'timestamp_ms'),
        sequence=_parse_int(row[2], 'sequence'),
        tag_id=_parse_int(row[3], 'tag_id'),
        accepted_id=_parse_int(row[4], 'accepted_id'),
        link_ok=_parse_bool(row[5], 'link_ok'),
        valid_mask=_parse_int(row[6], 'valid_mask'),
        measurement_ready=_parse_bool(row[7], 'measurement_ready'),
        pose_valid=_parse_bool(row[8], 'pose_valid'),
        raw_a0_mm=_parse_int(row[9], 'raw_a0_mm'),
        raw_a1_mm=_parse_int(row[10], 'raw_a1_mm'),
        corrected_a0_mm=_parse_float(row[11], 'corrected_a0_mm'),
        corrected_a1_mm=_parse_float(row[12], 'corrected_a1_mm'),
        filtered_a0_mm=_parse_float(row[13], 'filtered_a0_mm'),
        filtered_a1_mm=_parse_float(row[14], 'filtered_a1_mm'),
        x_m=_parse_float(row[15], 'x_m'),
        y_m=_parse_float(row[16], 'y_m'),
        boundary_m=_parse_float(row[17], 'boundary_m'),
        angle_deg=_parse_float(row[18], 'angle_deg'),
        residual_m=_parse_float(row[19], 'residual_m'),
        state=row[20].strip(),
        events=_parse_int(row[21], 'events'),
        accepted_frames=_parse_int(row[22], 'accepted_frames'),
        rejected_frames=_parse_int(row[23], 'rejected_frames'),
    )


def frame_from_csv_mapping(row: dict[str, str]) -> DiagnosticFrame:
    payload = ','.join(
        [DIAGNOSTIC_PREFIX] + [row[name] for name in DIAGNOSTIC_FIELDS]
    )
    frame = parse_diagnostic_line(payload)
    if frame is None:
        raise TelemetryParseError('CSV行中没有诊断帧')
    return frame
