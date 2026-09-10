from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Sequence

from .analysis import SeriesStats, summarize
from .telemetry import DiagnosticFrame


@dataclass(frozen=True, slots=True)
class StaticAcceptanceConfig:
    expected_boundary_m: float
    expected_angle_deg: float
    expected_address: int = 0x6E19
    expected_id: int = 0
    minimum_frames: int = 300
    minimum_pose_ratio: float = 0.95
    minimum_frame_rate_hz: float = 5.0
    maximum_gap_s: float = 0.6
    distance_tolerance_m: float = 0.3
    angle_tolerance_deg: float = 10.0
    display_confirmed: bool = False
    expected_state: str | None = None


@dataclass(frozen=True, slots=True)
class StaticAcceptanceReport:
    result: str
    expected_boundary_m: float
    expected_angle_deg: float
    expected_address: str
    expected_id: int
    display_confirmed: bool
    diagnostic_frames: int
    identity_frames: int
    pose_frames: int
    parse_errors: int
    observed_duration_s: float
    frame_rate_hz: float
    maximum_gap_s: float
    pose_ratio: float
    raw_center_distance_m: SeriesStats | None
    boundary_distance_m: SeriesStats | None
    filtered_angle_deg: SeriesStats | None
    boundary_error_m: float | None
    angle_error_deg: float | None
    states: tuple[str, ...]
    checks: dict[str, bool]


def circular_error_deg(measured_deg: float, reference_deg: float) -> float:
    return (measured_deg - reference_deg + 180.0) % 360.0 - 180.0


def _timing(frames: Sequence[DiagnosticFrame]) -> tuple[float, float, float]:
    if len(frames) < 2:
        return 0.0, 0.0, 0.0
    gaps_ms = [
        later.timestamp_ms - earlier.timestamp_ms
        for earlier, later in zip(frames, frames[1:])
        if later.timestamp_ms >= earlier.timestamp_ms
    ]
    if not gaps_ms:
        return 0.0, 0.0, 0.0
    duration_s = sum(gaps_ms) / 1000.0
    frame_rate_hz = (
        (len(frames) - 1) / duration_s if duration_s > 0.0 else 0.0
    )
    return duration_s, frame_rate_hz, max(gaps_ms) / 1000.0


def assess_static_capture(
    frames: Sequence[DiagnosticFrame],
    parse_errors: int,
    config: StaticAcceptanceConfig,
) -> StaticAcceptanceReport:
    if config.expected_boundary_m < 0.0:
        raise ValueError('卷尺边界距离不能为负数')
    if not -180.0 <= config.expected_angle_deg <= 180.0:
        raise ValueError('真实角度必须在-180到180度之间')
    if not 0 <= config.expected_address <= 0xFFFF:
        raise ValueError('标签短地址必须在0x0000到0xFFFF之间')
    if not 0 <= config.expected_id <= 15:
        raise ValueError('逻辑ID必须在0到15之间')
    if config.minimum_frames <= 0:
        raise ValueError('最少帧数必须大于0')

    identity_frames = [
        frame
        for frame in frames
        if frame.link_ok
        and frame.tag_address == config.expected_address
        and frame.tag_id == config.expected_id
        and frame.accepted_id == config.expected_id
    ]
    pose_frames = [
        frame
        for frame in identity_frames
        if frame.measurement_ready and frame.pose_valid
    ]
    duration_s, frame_rate_hz, maximum_gap_s = _timing(frames)
    pose_ratio = len(pose_frames) / len(frames) if frames else 0.0

    raw_stats = summarize(frame.raw_distance_cm / 100.0 for frame in pose_frames)
    boundary_stats = summarize(frame.boundary_m for frame in pose_frames)
    angle_stats = summarize(frame.filtered_angle_deg for frame in pose_frames)
    boundary_error = (
        boundary_stats.median - config.expected_boundary_m
        if boundary_stats is not None
        else None
    )
    angle_error = (
        circular_error_deg(angle_stats.median, config.expected_angle_deg)
        if angle_stats is not None
        else None
    )
    states = tuple(sorted({frame.state for frame in identity_frames}))

    checks = {
        '有效诊断帧达到最少帧数': len(frames) >= config.minimum_frames,
        '全部诊断帧身份一致': bool(frames) and len(identity_frames) == len(frames),
        '定位有效帧比例达到下限': pose_ratio >= config.minimum_pose_ratio,
        '遥测解析无错误': parse_errors == 0,
        '持续帧率达到下限': frame_rate_hz >= config.minimum_frame_rate_hz,
        '最大帧间隔不超过上限': maximum_gap_s <= config.maximum_gap_s,
        '径向距离误差不大于0.3m': (
            boundary_error is not None
            and abs(boundary_error) <= config.distance_tolerance_m
        ),
        '方位角误差不大于10度': (
            angle_error is not None
            and abs(angle_error) <= config.angle_tolerance_deg
        ),
        'TFT已人工确认显示ID、距离和角度': config.display_confirmed,
    }
    if config.expected_state is not None:
        checks[f'状态稳定为{config.expected_state}'] = (
            bool(identity_frames)
            and all(frame.state == config.expected_state for frame in identity_frames)
        )

    return StaticAcceptanceReport(
        result='通过' if all(checks.values()) else '未通过',
        expected_boundary_m=config.expected_boundary_m,
        expected_angle_deg=config.expected_angle_deg,
        expected_address=f'0x{config.expected_address:04X}',
        expected_id=config.expected_id,
        display_confirmed=config.display_confirmed,
        diagnostic_frames=len(frames),
        identity_frames=len(identity_frames),
        pose_frames=len(pose_frames),
        parse_errors=parse_errors,
        observed_duration_s=duration_s,
        frame_rate_hz=frame_rate_hz,
        maximum_gap_s=maximum_gap_s,
        pose_ratio=pose_ratio,
        raw_center_distance_m=raw_stats,
        boundary_distance_m=boundary_stats,
        filtered_angle_deg=angle_stats,
        boundary_error_m=boundary_error,
        angle_error_deg=angle_error,
        states=states,
        checks=checks,
    )


def static_report_markdown(
    report: StaticAcceptanceReport,
    *,
    tested_at: str,
    port: str,
    notes: str = '',
) -> str:
    checks = '\n'.join(
        f'- [{"x" if passed else " "}] {name}'
        for name, passed in report.checks.items()
    )

    def median_text(stats: SeriesStats | None, unit: str) -> str:
        return '--' if stats is None else f'{stats.median:.3f} {unit}'

    boundary_error = (
        '--'
        if report.boundary_error_m is None
        else f'{report.boundary_error_m:+.3f} m'
    )
    angle_error = (
        '--'
        if report.angle_error_deg is None
        else f'{report.angle_error_deg:+.3f} deg'
    )
    state_text = ', '.join(report.states) if report.states else '--'
    notes_text = notes.strip() or '无'
    return f'''# C题静态定位验收报告

- 结果：**{report.result}**
- 时间：{tested_at}
- ESP32串口：{port}
- 卷尺边界距离：{report.expected_boundary_m:.3f} m
- 真实方位角：{report.expected_angle_deg:+.1f} deg
- 期望标签：{report.expected_address} / ID {report.expected_id:04d}
- 有效诊断帧：{report.diagnostic_frames}
- 身份一致帧：{report.identity_frames}
- 定位有效帧：{report.pose_frames}（{report.pose_ratio:.1%}）
- 观察时长：{report.observed_duration_s:.3f} s
- 平均帧率：{report.frame_rate_hz:.2f} Hz
- 最大帧间隔：{report.maximum_gap_s:.3f} s
- BU04中心距离中位数：{median_text(report.raw_center_distance_m, 'm')}
- 门锁边界距离中位数：{median_text(report.boundary_distance_m, 'm')}
- 径向距离误差：{boundary_error}
- 滤波方位角中位数：{median_text(report.filtered_angle_deg, 'deg')}
- 方位角误差：{angle_error}
- 观察到的状态：{state_text}
- 现场备注：{notes_text}

## 检查项

{checks}
'''
