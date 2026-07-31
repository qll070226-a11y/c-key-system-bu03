from __future__ import annotations

import argparse
import time
from collections import deque

import serial

from c_key_debugger.telemetry import TelemetryParseError, parse_diagnostic_line


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
                if first is None:
                    first = frame

    print(
        f'lines={line_count} diagnostics={diagnostics} '
        f'parse_errors={parse_errors}'
    )
    if first is not None:
        print(
            f'first: seq={first.sequence} mask=0x{first.valid_mask:02X} '
            f'raw=({first.raw_a0_mm},{first.raw_a1_mm}) '
            f'state={first.state}'
        )
        print(
            f'last: seq={last.sequence} ready={int(last.measurement_ready)} '
            f'pose={int(last.pose_valid)} '
            f'corrected=({last.corrected_a0_mm:.1f},{last.corrected_a1_mm:.1f})mm '
            f'filtered=({last.filtered_a0_mm:.1f},{last.filtered_a1_mm:.1f})mm '
            f'xy=({last.x_m:+.3f},{last.y_m:+.3f})m '
            f'angle={last.angle_deg:+.2f}deg residual={last.residual_m:.3f}m '
            f'state={last.state}'
        )
    else:
        print('last serial lines:')
        for line in samples:
            print(line)
    return 0 if diagnostics > 0 and parse_errors == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
