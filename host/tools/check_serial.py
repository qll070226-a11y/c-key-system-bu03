from __future__ import annotations

import argparse
import statistics
import time
from collections import deque

import serial

from c_key_debugger.telemetry import TelemetryParseError, parse_diagnostic_line


def _normalize_angle(angle_deg: float) -> float:
    while angle_deg > 180.0:
        angle_deg -= 360.0
    while angle_deg < -180.0:
        angle_deg += 360.0
    return angle_deg


def _legacy_filtered_angles(raw_angles: list[float]) -> list[float]:
    if not raw_angles:
        return []
    samples: list[float] = []
    filtered = _normalize_angle(raw_angles[0])
    outputs: list[float] = []
    for raw_angle in raw_angles:
        raw_angle = _normalize_angle(raw_angle)
        samples.append(raw_angle)
        samples = samples[-5:]
        unwrapped = sorted(
            filtered + _normalize_angle(sample - filtered)
            for sample in samples
        )
        median = statistics.median(unwrapped)
        error = _normalize_angle(median - filtered)
        alpha = 0.60 if abs(error) >= 3.0 else 0.15
        filtered = _normalize_angle(filtered + alpha * error)
        outputs.append(filtered)
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description='检查ESP32诊断串口')
    parser.add_argument('port')
    parser.add_argument('--seconds', type=float, default=8.0)
    parser.add_argument('--baud', type=int, default=115200)
    args = parser.parse_args()

    line_count = 0
    diagnostics = 0
    parse_errors = 0
    first = None
    last = None
    raw_angles: list[float] = []
    filtered_angles: list[float] = []
    first_path_powers: list[int] = []
    rx_levels: list[int] = []
    rejected_frames = 0
    samples: deque[str] = deque(maxlen=12)
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            data = port.readline()
            if not data:
                continue
            line_count += 1
            line = data.decode('utf-8', errors='replace').strip()
            samples.append(line)
            try:
                frame = parse_diagnostic_line(line)
            except TelemetryParseError:
                parse_errors += 1
                continue
            if frame is not None:
                diagnostics += 1
                last = frame
                raw_angles.append(frame.raw_angle_deg)
                first_path_powers.append(frame.first_path_power)
                rx_levels.append(frame.rx_level)
                rejected_frames += int(frame.angle_sample_rejected)
                if frame.pose_valid:
                    filtered_angles.append(frame.filtered_angle_deg)
                if first is None:
                    first = frame

    print(
        f'lines={line_count} diagnostics={diagnostics} '
        f'parse_errors={parse_errors}'
    )
    if first is not None:
        print(
            f'first: seq={first.sequence} address=0x{first.tag_address:04X} '
            f'raw=({first.raw_distance_cm}cm,{first.raw_angle_deg:+.1f}deg) '
            f'state={first.state}'
        )
        print(
            f'last: seq={last.sequence} ready={int(last.measurement_ready)} '
            f'pose={int(last.pose_valid)} '
            f'corrected={last.corrected_distance_mm:.1f}mm '
            f'filtered={last.filtered_distance_mm:.1f}mm '
            f'xy=({last.x_m:+.3f},{last.y_m:+.3f})m '
            f'angle={last.filtered_angle_deg:+.2f}deg '
            f'state={last.state}'
        )
        if len(raw_angles) >= 2 and len(filtered_angles) >= 2:
            legacy_angles = _legacy_filtered_angles(raw_angles)
            print(
                f'angle_stats: raw_std={statistics.pstdev(raw_angles):.3f}deg '
                f'raw_mean={statistics.fmean(raw_angles):+.3f}deg '
                f'raw_median={statistics.median(raw_angles):+.3f}deg '
                f'raw_range=[{min(raw_angles):+.1f},{max(raw_angles):+.1f}]deg '
                f'filtered_std={statistics.pstdev(filtered_angles):.3f}deg '
                f'filtered_mean={statistics.fmean(filtered_angles):+.3f}deg '
                f'filtered_range=[{min(filtered_angles):+.2f},'
                f'{max(filtered_angles):+.2f}]deg '
                f'hampel_rejected={rejected_frames}'
            )
            print(
                f'filter_compare: legacy_std='
                f'{statistics.pstdev(legacy_angles):.3f}deg '
                f'one_euro_std={statistics.pstdev(filtered_angles):.3f}deg '
                f'raw_max_step={max(abs(b - a) for a, b in zip(raw_angles, raw_angles[1:])):.1f}deg '
                f'one_euro_max_step='
                f'{max(abs(b - a) for a, b in zip(filtered_angles, filtered_angles[1:])):.2f}deg'
            )
        if first_path_powers and rx_levels:
            print(
                f'radio_stats: fp_median={statistics.median(first_path_powers):.1f} '
                f'rx_median={statistics.median(rx_levels):.1f}'
            )
    else:
        print('last serial lines:')
        for line in samples:
            print(line)
    return 0 if diagnostics > 0 and parse_errors == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
