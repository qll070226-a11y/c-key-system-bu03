from __future__ import annotations

import csv
import math
from dataclasses import asdict, dataclass


DIAGNOSTIC_PREFIX = 'C_KEY_DIAG_V3'
LEGACY_DIAGNOSTIC_PREFIX = 'C_KEY_DIAG_V2'
LEGACY_DIAGNOSTIC_FIELDS = (
    'timestamp_ms', 'sequence', 'tag_address', 'tag_id', 'accepted_id',
    'link_ok', 'measurement_ready', 'pose_valid', 'raw_distance_cm',
    'raw_angle_deg', 'corrected_distance_mm', 'filtered_distance_mm',
    'filtered_angle_deg', 'x_m', 'y_m', 'boundary_m', 'state', 'events',
    'accepted_frames', 'rejected_frames',
)
DIAGNOSTIC_FIELDS = (
    'timestamp_ms', 'sequence', 'tag_address', 'tag_id', 'accepted_id',
    'link_ok', 'measurement_ready', 'pose_valid', 'raw_distance_cm',
    'raw_angle_deg', 'first_path_power', 'rx_level',
    'corrected_distance_mm', 'filtered_distance_mm', 'filtered_angle_deg',
    'x_m', 'y_m', 'boundary_m', 'state', 'events', 'accepted_frames',
    'rejected_frames', 'angle_sample_rejected', 'angle_rejected_samples',
)


class TelemetryParseError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class DiagnosticFrame:
    timestamp_ms: int
    sequence: int
    tag_address: int
    tag_id: int
    accepted_id: int
    link_ok: bool
    measurement_ready: bool
    pose_valid: bool
    raw_distance_cm: int
    raw_angle_deg: float
    corrected_distance_mm: float
    filtered_distance_mm: float
    filtered_angle_deg: float
    x_m: float
    y_m: float
    boundary_m: float
    state: str
    events: int
    accepted_frames: int
    rejected_frames: int
    first_path_power: int = 0
    rx_level: int = 0
    angle_sample_rejected: bool = False
    angle_rejected_samples: int = 0

    def as_csv_values(self) -> list[str]:
        values = asdict(self)
        return [
            '1' if values[name] is True else
            '0' if values[name] is False else
            str(values[name])
            for name in DIAGNOSTIC_FIELDS
        ]


def extract_diagnostic_payload(line: str) -> str | None:
    for prefix in (DIAGNOSTIC_PREFIX, LEGACY_DIAGNOSTIC_PREFIX):
        marker = f'{prefix},'
        index = line.find(marker)
        if index >= 0:
            return line[index:].strip()
    return None


def _parse_int(text: str, name: str, minimum: int | None = 0) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise TelemetryParseError(f'{name} is not an integer: {text!r}') from exc
    if minimum is not None and value < minimum:
        raise TelemetryParseError(f'{name} is below {minimum}: {value}')
    return value


def _parse_float(text: str, name: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise TelemetryParseError(f'{name} is not a float: {text!r}') from exc
    if not math.isfinite(value):
        raise TelemetryParseError(f'{name} is not finite: {value}')
    return value


def _parse_bool(text: str, name: str) -> bool:
    value = _parse_int(text, name)
    if value not in (0, 1):
        raise TelemetryParseError(f'{name} must be 0 or 1: {value}')
    return bool(value)


def parse_diagnostic_line(line: str) -> DiagnosticFrame | None:
    payload = extract_diagnostic_payload(line)
    if payload is None:
        return None
    row = next(csv.reader([payload]))
    if row[0] == LEGACY_DIAGNOSTIC_PREFIX:
        expected_columns = len(LEGACY_DIAGNOSTIC_FIELDS) + 1
        legacy = True
    elif row[0] == DIAGNOSTIC_PREFIX:
        expected_columns = len(DIAGNOSTIC_FIELDS) + 1
        legacy = False
    else:
        raise TelemetryParseError(f'unsupported diagnostic version: {row[0]!r}')
    if len(row) != expected_columns:
        raise TelemetryParseError(
            f'wrong diagnostic column count: got {len(row)}, '
            f'expected {expected_columns}'
        )

    quality_offset = 0 if legacy else 2
    return DiagnosticFrame(
        timestamp_ms=_parse_int(row[1], 'timestamp_ms'),
        sequence=_parse_int(row[2], 'sequence'),
        tag_address=_parse_int(row[3], 'tag_address'),
        tag_id=_parse_int(row[4], 'tag_id'),
        accepted_id=_parse_int(row[5], 'accepted_id'),
        link_ok=_parse_bool(row[6], 'link_ok'),
        measurement_ready=_parse_bool(row[7], 'measurement_ready'),
        pose_valid=_parse_bool(row[8], 'pose_valid'),
        raw_distance_cm=_parse_int(row[9], 'raw_distance_cm'),
        raw_angle_deg=_parse_float(row[10], 'raw_angle_deg'),
        first_path_power=0 if legacy else _parse_int(
            row[11], 'first_path_power', None),
        rx_level=0 if legacy else _parse_int(row[12], 'rx_level', None),
        corrected_distance_mm=_parse_float(
            row[11 + quality_offset], 'corrected_distance_mm'),
        filtered_distance_mm=_parse_float(
            row[12 + quality_offset], 'filtered_distance_mm'),
        filtered_angle_deg=_parse_float(
            row[13 + quality_offset], 'filtered_angle_deg'),
        x_m=_parse_float(row[14 + quality_offset], 'x_m'),
        y_m=_parse_float(row[15 + quality_offset], 'y_m'),
        boundary_m=_parse_float(row[16 + quality_offset], 'boundary_m'),
        state=row[17 + quality_offset].strip(),
        events=_parse_int(row[18 + quality_offset], 'events'),
        accepted_frames=_parse_int(
            row[19 + quality_offset], 'accepted_frames'),
        rejected_frames=_parse_int(
            row[20 + quality_offset], 'rejected_frames'),
        angle_sample_rejected=False if legacy else _parse_bool(
            row[23], 'angle_sample_rejected'),
        angle_rejected_samples=0 if legacy else _parse_int(
            row[24], 'angle_rejected_samples'),
    )


def frame_from_csv_mapping(row: dict[str, str]) -> DiagnosticFrame:
    if all(name in row for name in DIAGNOSTIC_FIELDS):
        prefix = DIAGNOSTIC_PREFIX
        fields = DIAGNOSTIC_FIELDS
    else:
        prefix = LEGACY_DIAGNOSTIC_PREFIX
        fields = LEGACY_DIAGNOSTIC_FIELDS
    payload = ','.join([prefix] + [row[name] for name in fields])
    frame = parse_diagnostic_line(payload)
    if frame is None:
        raise TelemetryParseError('CSV row does not contain a diagnostic frame')
    return frame
