from __future__ import annotations

import math
import statistics
from dataclasses import dataclass, field
from typing import Callable, Iterable

from .telemetry import DiagnosticFrame


@dataclass(frozen=True, slots=True)
class SeriesStats:
    count: int
    mean: float
    median: float
    stdev: float
    mad: float
    minimum: float
    maximum: float


@dataclass(frozen=True, slots=True)
class MetricSummary:
    name: str
    unit: str
    stats: SeriesStats | None
    reference: float | None = None

    @property
    def median_error(self) -> float | None:
        if self.stats is None or self.reference is None:
            return None
        return self.stats.median - self.reference


@dataclass(slots=True)
class CapturePoint:
    label: str
    true_x_m: float
    true_y_m: float
    anchor_height_cm: float = 0.0
    tag_height_cm: float = 0.0
    notes: str = ''
    frames: list[DiagnosticFrame] = field(default_factory=list)


@dataclass(frozen=True, slots=True)
class LinearFit:
    scale: float
    offset_mm: float
    mae_mm: float
    maximum_error_mm: float
    sample_count: int


def summarize(values: Iterable[float]) -> SeriesStats | None:
    data = [float(value) for value in values if math.isfinite(float(value))]
    if not data:
        return None
    median = statistics.median(data)
    deviations = [abs(value - median) for value in data]
    return SeriesStats(
        count=len(data),
        mean=statistics.fmean(data),
        median=median,
        stdev=statistics.stdev(data) if len(data) > 1 else 0.0,
        mad=statistics.median(deviations),
        minimum=min(data),
        maximum=max(data),
    )


def expected_ranges_mm(
    x_m: float,
    y_m: float,
    anchor_positions_m: tuple[tuple[float, float], tuple[float, float]],
    anchor_height_cm: float = 0.0,
    tag_height_cm: float = 0.0,
) -> tuple[float, float]:
    height_delta_m = (tag_height_cm - anchor_height_cm) / 100.0
    values = tuple(
        math.sqrt(
            (x_m - anchor_x) ** 2
            + (y_m - anchor_y) ** 2
            + height_delta_m ** 2
        ) * 1000.0
        for anchor_x, anchor_y in anchor_positions_m
    )
    return values[0], values[1]


def true_angle_deg(x_m: float, y_m: float) -> float:
    return math.degrees(math.atan2(x_m, y_m))


def summarize_capture(
    capture: CapturePoint,
    anchor_positions_m: tuple[tuple[float, float], tuple[float, float]],
) -> list[MetricSummary]:
    expected_a0, expected_a1 = expected_ranges_mm(
        capture.true_x_m,
        capture.true_y_m,
        anchor_positions_m,
        capture.anchor_height_cm,
        capture.tag_height_cm,
    )
    valid_link = [frame for frame in capture.frames if frame.link_ok]
    ready = [frame for frame in capture.frames if frame.measurement_ready]
    poses = [frame for frame in capture.frames if frame.pose_valid]
    definitions: list[
        tuple[str, str, list[DiagnosticFrame], Callable[[DiagnosticFrame], float], float | None]
    ] = [
        ('A0原始', 'mm', valid_link, lambda f: f.raw_a0_mm, expected_a0),
        ('A1原始', 'mm', valid_link, lambda f: f.raw_a1_mm, expected_a1),
        ('A0校正', 'mm', valid_link, lambda f: f.corrected_a0_mm, expected_a0),
        ('A1校正', 'mm', valid_link, lambda f: f.corrected_a1_mm, expected_a1),
        ('A0滤波', 'mm', ready, lambda f: f.filtered_a0_mm, expected_a0),
        ('A1滤波', 'mm', ready, lambda f: f.filtered_a1_mm, expected_a1),
        ('X坐标', 'm', poses, lambda f: f.x_m, capture.true_x_m),
        ('Y坐标', 'm', poses, lambda f: f.y_m, capture.true_y_m),
        ('方位角', 'deg', poses, lambda f: f.angle_deg,
         true_angle_deg(capture.true_x_m, capture.true_y_m)),
        ('定位残差', 'm', poses, lambda f: f.residual_m, 0.0),
    ]
    return [
        MetricSummary(name, unit, summarize(selector(frame) for frame in frames), reference)
        for name, unit, frames, selector, reference in definitions
    ]


def _linear_fit(pairs: list[tuple[float, float]]) -> LinearFit:
    if len(pairs) < 2:
        raise ValueError('线性标定至少需要两个有效样本')
    xs = [pair[0] for pair in pairs]
    ys = [pair[1] for pair in pairs]
    x_mean = statistics.fmean(xs)
    y_mean = statistics.fmean(ys)
    denominator = sum((x - x_mean) ** 2 for x in xs)
    if denominator <= 1e-9:
        raise ValueError('标定样本原始距离没有足够变化')
    scale = sum((x - x_mean) * (y - y_mean) for x, y in pairs) / denominator
    offset = y_mean - scale * x_mean
    errors = [scale * x + offset - y for x, y in pairs]
    return LinearFit(
        scale=scale,
        offset_mm=offset,
        mae_mm=statistics.fmean(abs(error) for error in errors),
        maximum_error_mm=max(abs(error) for error in errors),
        sample_count=len(pairs),
    )


def fit_anchor_calibration(
    captures: Iterable[CapturePoint],
    anchor_positions_m: tuple[tuple[float, float], tuple[float, float]],
) -> tuple[LinearFit, LinearFit]:
    pairs: tuple[list[tuple[float, float]], list[tuple[float, float]]] = ([], [])
    for capture in captures:
        expected = expected_ranges_mm(
            capture.true_x_m,
            capture.true_y_m,
            anchor_positions_m,
            capture.anchor_height_cm,
            capture.tag_height_cm,
        )
        for frame in capture.frames:
            if not frame.link_ok:
                continue
            pairs[0].append((float(frame.raw_a0_mm), expected[0]))
            pairs[1].append((float(frame.raw_a1_mm), expected[1]))
    return _linear_fit(pairs[0]), _linear_fit(pairs[1])
