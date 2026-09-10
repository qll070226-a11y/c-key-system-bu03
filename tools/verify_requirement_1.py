from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))

from c_key_debugger.telemetry import (  # noqa: E402
    DiagnosticFrame,
    TelemetryParseError,
    parse_diagnostic_line,
)


@dataclass(frozen=True, slots=True)
class Requirement1Report:
    result: str
    tested_at: str
    port: str
    physical_distance_m: float
    expected_address: str
    expected_id: int
    display_confirmed: bool
    listener_to_first_identity_s: float | None
    observed_duration_s: float
    diagnostic_frames: int
    expected_identity_frames: int
    parse_errors: int
    frame_rate_hz: float
    maximum_gap_s: float
    median_raw_distance_m: float | None
    minimum_raw_distance_m: float | None
    maximum_raw_distance_m: float | None
    states: list[str]
    checks: dict[str, bool]
    expected_tft: str


def parse_address(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError('标签短地址必须在0x0000到0xFFFF之间')
    return value


def report_markdown(report: Requirement1Report) -> str:
    checks = '\n'.join(
        f'- [{"x" if passed else " "}] {name}'
        for name, passed in report.checks.items()
    )
    distance = (
        f'{report.median_raw_distance_m:.3f} m'
        if report.median_raw_distance_m is not None
        else '--'
    )
    latency = (
        f'{report.listener_to_first_identity_s:.3f} s'
        if report.listener_to_first_identity_s is not None
        else '--'
    )
    return f'''# C题第1项专项验收报告

- 结果：**{report.result}**
- 时间：{report.tested_at}
- ESP32串口：{report.port}
- 卷尺布置距离：{report.physical_distance_m:.3f} m
- 期望标签短地址：{report.expected_address}
- 期望逻辑ID：{report.expected_id:04d}
- 监听开始至首次识别（含人工操作等待）：{latency}
- 连续观察时间：{report.observed_duration_s:.3f} s
- 有效诊断帧：{report.diagnostic_frames}
- 身份完全一致帧：{report.expected_identity_frames}
- 平均帧率：{report.frame_rate_hz:.2f} Hz
- 最大帧间隔：{report.maximum_gap_s:.3f} s
- 原始距离中位数：{distance}
- TFT期望显示：{report.expected_tft}

## 检查项

{checks}
'''


def write_report(report: Requirement1Report, output_dir: Path) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    base = output_dir / f'requirement_1_{stamp}'
    json_path = base.with_suffix('.json')
    md_path = base.with_suffix('.md')
    json_path.write_text(
        json.dumps(asdict(report), ensure_ascii=False, indent=2),
        encoding='utf-8',
    )
    md_path.write_text(report_markdown(report), encoding='utf-8')
    return json_path, md_path


def main() -> int:
    parser = argparse.ArgumentParser(
        description='验收C题第1项：3米处一键启动、持续ID通信与显示'
    )
    parser.add_argument('port', help='ESP32诊断串口，例如COM21')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--physical-distance-m', type=float, required=True)
    parser.add_argument('--expected-address', type=parse_address, default=0x6E19)
    parser.add_argument('--expected-id', type=int, default=0)
    parser.add_argument('--activation-window-s', type=float, default=12.0)
    parser.add_argument('--continuous-seconds', type=float, default=15.0)
    parser.add_argument('--minimum-frame-rate-hz', type=float, default=5.0)
    parser.add_argument('--maximum-gap-s', type=float, default=0.6)
    parser.add_argument('--distance-tolerance-m', type=float, default=0.45)
    parser.add_argument('--display-confirmed', action='store_true')
    parser.add_argument(
        '--output-dir',
        type=Path,
        default=ROOT / 'reports',
    )
    args = parser.parse_args()

    if args.physical_distance_m < 3.0:
        parser.error('第1项要求通信距离不小于3m，卷尺距离必须至少3.0m')
    if not 0 <= args.expected_id <= 15:
        parser.error('逻辑ID必须在0到15之间')

    import serial

    print('串口已打开后，请在12秒内拨动数字钥匙开关启动。', flush=True)
    started = time.monotonic()
    first_identity_at: float | None = None
    identity_started_device_ms: int | None = None
    frames: list[DiagnosticFrame] = []
    arrival_times: list[float] = []
    parse_errors = 0

    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        deadline = started + args.activation_window_s + args.continuous_seconds
        while time.monotonic() < deadline:
            raw = port.readline()
            now = time.monotonic()
            if not raw:
                if first_identity_at is None and now > started + args.activation_window_s:
                    break
                continue
            line = raw.decode('utf-8', errors='replace').strip()
            try:
                frame = parse_diagnostic_line(line)
            except TelemetryParseError:
                parse_errors += 1
                continue
            if frame is None:
                continue

            frames.append(frame)
            arrival_times.append(now)
            identity_matches = (
                frame.link_ok
                and frame.tag_address == args.expected_address
                and frame.tag_id == args.expected_id
                and frame.accepted_id == args.expected_id
            )
            if identity_matches and first_identity_at is None:
                first_identity_at = now
                identity_started_device_ms = frame.timestamp_ms
                deadline = now + args.continuous_seconds
                print(
                    f'已识别标签0x{frame.tag_address:04X} / ID {frame.tag_id:04d}，'
                    f'开始连续观察{args.continuous_seconds:.0f}秒。',
                    flush=True,
                )

    identity_frames = [
        frame for frame in frames
        if frame.link_ok
        and frame.tag_address == args.expected_address
        and frame.tag_id == args.expected_id
        and frame.accepted_id == args.expected_id
    ]
    if identity_frames and identity_started_device_ms is not None:
        observed_duration_s = max(
            (identity_frames[-1].timestamp_ms - identity_started_device_ms) / 1000.0,
            0.0,
        )
    else:
        observed_duration_s = 0.0
    frame_rate_hz = (
        len(identity_frames) / observed_duration_s
        if observed_duration_s > 0.0
        else 0.0
    )
    gaps = [
        later - earlier
        for earlier, later in zip(arrival_times, arrival_times[1:])
    ]
    maximum_gap = max(gaps, default=0.0)
    distances = [
        frame.raw_distance_cm / 100.0
        for frame in identity_frames
        if frame.measurement_ready
    ]
    identity_only = len(identity_frames) == len(frames) and bool(frames)
    checks = {
        '卷尺布置距离不小于3m': args.physical_distance_m >= 3.0,
        '在启动窗口内收到数字钥匙ID': first_identity_at is not None,
        '无线标签短地址持续为期望值': identity_only,
        '逻辑钥匙ID与门锁ID持续匹配': identity_only,
        '连续观察时间达到设定值': (
            observed_duration_s >= args.continuous_seconds - 0.5
        ),
        '持续帧率达到下限': frame_rate_hz >= args.minimum_frame_rate_hz,
        '通信最大中断不超过上限': maximum_gap <= args.maximum_gap_s,
        '遥测解析无错误': parse_errors == 0,
        '测距结果与3米布置基本一致': (
            bool(distances)
            and statistics.median(distances)
            >= args.physical_distance_m - args.distance_tolerance_m
        ),
        'TFT已人工确认显示钥匙ID和认证结果': args.display_confirmed,
    }
    passed = all(checks.values())
    report = Requirement1Report(
        result='通过' if passed else '未通过',
        tested_at=datetime.now().astimezone().isoformat(timespec='seconds'),
        port=args.port,
        physical_distance_m=args.physical_distance_m,
        expected_address=f'0x{args.expected_address:04X}',
        expected_id=args.expected_id,
        display_confirmed=args.display_confirmed,
        listener_to_first_identity_s=(
            first_identity_at - started if first_identity_at is not None else None
        ),
        observed_duration_s=observed_duration_s,
        diagnostic_frames=len(frames),
        expected_identity_frames=len(identity_frames),
        parse_errors=parse_errors,
        frame_rate_hz=frame_rate_hz,
        maximum_gap_s=maximum_gap,
        median_raw_distance_m=(
            statistics.median(distances) if distances else None
        ),
        minimum_raw_distance_m=min(distances) if distances else None,
        maximum_raw_distance_m=max(distances) if distances else None,
        states=sorted({frame.state for frame in identity_frames}),
        checks=checks,
        expected_tft='钥匙ID 0000 / 门锁ID 0000 / 身份认证 匹配成功',
    )
    json_path, md_path = write_report(report, args.output_dir)
    print(report_markdown(report))
    print(f'JSON报告：{json_path}')
    print(f'MD报告：{md_path}')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
