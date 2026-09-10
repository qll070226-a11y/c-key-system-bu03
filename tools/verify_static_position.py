from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))

from c_key_debugger.acceptance import (  # noqa: E402
    StaticAcceptanceConfig,
    assess_static_capture,
    static_report_markdown,
)
from c_key_debugger.telemetry import (  # noqa: E402
    DiagnosticFrame,
    TelemetryParseError,
    parse_diagnostic_line,
)


def parse_address(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError('标签短地址必须在0x0000到0xFFFF之间')
    return value


def file_token(value: float) -> str:
    return f'{value:+.2f}'.replace('+', 'p').replace('-', 'n').replace('.', 'p')


def write_reports(
    report: object,
    markdown: str,
    output_dir: Path,
    distance_m: float,
    angle_deg: float,
) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    stem = (
        f'static_d{file_token(distance_m)}_a{file_token(angle_deg)}_{stamp}'
    )
    json_path = output_dir / f'{stem}.json'
    md_path = output_dir / f'{stem}.md'
    json_path.write_text(
        json.dumps(asdict(report), ensure_ascii=False, indent=2),
        encoding='utf-8',
    )
    md_path.write_text(markdown, encoding='utf-8')
    return json_path, md_path


def collect_frames(
    port_name: str,
    baud: int,
    config: StaticAcceptanceConfig,
    minimum_seconds: float,
    startup_timeout_s: float,
    maximum_seconds: float,
) -> tuple[list[DiagnosticFrame], int]:
    import serial

    frames: list[DiagnosticFrame] = []
    parse_errors = 0
    capture_started: float | None = None
    opened_at = time.monotonic()

    with serial.Serial(port_name, baud, timeout=0.2) as port:
        port.reset_input_buffer()
        while True:
            now = time.monotonic()
            if capture_started is None and now - opened_at >= startup_timeout_s:
                break
            if capture_started is not None:
                elapsed = now - capture_started
                if elapsed >= maximum_seconds:
                    break
                if elapsed >= minimum_seconds and len(frames) >= config.minimum_frames:
                    break

            raw = port.readline()
            if not raw:
                continue
            line = raw.decode('utf-8', errors='replace').strip()
            try:
                frame = parse_diagnostic_line(line)
            except TelemetryParseError:
                parse_errors += 1
                continue
            if frame is None:
                continue

            if capture_started is None:
                ready = (
                    frame.link_ok
                    and frame.tag_address == config.expected_address
                    and frame.tag_id == config.expected_id
                    and frame.accepted_id == config.expected_id
                    and frame.measurement_ready
                    and frame.pose_valid
                )
                if not ready:
                    continue
                capture_started = time.monotonic()
                print('定位已稳定，开始正式采集。', flush=True)
            frames.append(frame)
    return frames, parse_errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description='验收C题静态定位：径向距离、方位角、身份和TFT显示'
    )
    parser.add_argument('port', help='ESP32诊断串口，例如COM21')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument(
        '--boundary-distance-m', type=float, required=True,
        help='标签定位点到门锁60cm圆柱边界的卷尺距离',
    )
    parser.add_argument(
        '--angle-deg', type=float, required=True,
        help='相对门锁正前方的真实角度，右正左负',
    )
    parser.add_argument('--expected-address', type=parse_address, default=0x6E19)
    parser.add_argument('--expected-id', type=int, default=0)
    parser.add_argument('--minimum-frames', type=int, default=300)
    parser.add_argument('--minimum-seconds', type=float, default=8.0)
    parser.add_argument('--startup-timeout-s', type=float, default=12.0)
    parser.add_argument('--maximum-seconds', type=float, default=20.0)
    parser.add_argument('--minimum-pose-ratio', type=float, default=0.95)
    parser.add_argument('--minimum-frame-rate-hz', type=float, default=5.0)
    parser.add_argument('--maximum-gap-s', type=float, default=0.6)
    parser.add_argument('--distance-tolerance-m', type=float, default=0.3)
    parser.add_argument('--angle-tolerance-deg', type=float, default=10.0)
    parser.add_argument(
        '--expected-state',
        choices=('SENSING', 'WELCOME', 'UNLOCKED', 'OUT_OF_ANGLE'),
    )
    parser.add_argument('--display-confirmed', action='store_true')
    parser.add_argument('--notes', default='')
    parser.add_argument(
        '--output-dir', type=Path, default=ROOT / 'reports' / 'static',
    )
    args = parser.parse_args()

    if args.boundary_distance_m < 0.0:
        parser.error('卷尺边界距离不能为负数')
    if not -180.0 <= args.angle_deg <= 180.0:
        parser.error('真实角度必须在-180到180度之间')
    if not 0 <= args.expected_id <= 15:
        parser.error('逻辑ID必须在0到15之间')
    if args.minimum_seconds <= 0.0 or args.maximum_seconds < args.minimum_seconds:
        parser.error('采集时长参数无效')

    config = StaticAcceptanceConfig(
        expected_boundary_m=args.boundary_distance_m,
        expected_angle_deg=args.angle_deg,
        expected_address=args.expected_address,
        expected_id=args.expected_id,
        minimum_frames=args.minimum_frames,
        minimum_pose_ratio=args.minimum_pose_ratio,
        minimum_frame_rate_hz=args.minimum_frame_rate_hz,
        maximum_gap_s=args.maximum_gap_s,
        distance_tolerance_m=args.distance_tolerance_m,
        angle_tolerance_deg=args.angle_tolerance_deg,
        display_confirmed=args.display_confirmed,
        expected_state=args.expected_state,
    )
    print(
        f'请保持标签静止：边界距离{args.boundary_distance_m:.3f}m，'
        f'真实角度{args.angle_deg:+.1f}度。',
        flush=True,
    )
    frames, parse_errors = collect_frames(
        args.port,
        args.baud,
        config,
        args.minimum_seconds,
        args.startup_timeout_s,
        args.maximum_seconds,
    )
    report = assess_static_capture(frames, parse_errors, config)
    tested_at = datetime.now().astimezone().isoformat(timespec='seconds')
    markdown = static_report_markdown(
        report, tested_at=tested_at, port=args.port, notes=args.notes,
    )
    json_path, md_path = write_reports(
        report,
        markdown,
        args.output_dir,
        args.boundary_distance_m,
        args.angle_deg,
    )
    print(markdown)
    print(f'JSON报告：{json_path}')
    print(f'MD报告：{md_path}')
    return 0 if report.result == '通过' else 1


if __name__ == '__main__':
    raise SystemExit(main())
