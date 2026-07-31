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


def expected_distance_mm(
    x_m: float,
    y_m: float,
    anchor_height_cm: float = 0.0,
    tag_height_cm: float = 0.0,
) -> float:
    height_delta_m = (tag_height_cm - anchor_height_cm) / 100.0
    return math.sqrt(x_m ** 2 + y_m ** 2 + height_delta_m ** 2) * 1000.0


def true_angle_deg(x_m: float, y_m: float) -> float:
    return math.degrees(math.atan2(x_m, y_m))


def summarize_capture(capture: CapturePoint) -> list[MetricSummary]:
    expected_distance = expected_distance_mm(
        capture.true_x_m,
        capture.true_y_m,
        capture.anchor_height_cm,
        capture.tag_height_cm,
    )
    expected_angle = true_angle_deg(capture.true_x_m, capture.true_y_m)
    valid_link = [frame for frame in capture.frames if frame.link_ok]
    ready = [frame for frame in capture.frames if frame.measurement_ready]
    poses = [frame for frame in capture.frames if frame.pose_valid]
    definitions: list[
        tuple[str, str, list[DiagnosticFrame], Callable[[DiagnosticFrame], float], float | None]
    ] = [
        ('原始距离', 'mm', valid_link, lambda f: f.raw_distance_cm * 10.0,
         expected_distance),
        ('校正距离', 'mm', valid_link, lambda f: f.corrected_distance_mm,
         expected_distance),
        ('滤波距离', 'mm', ready, lambda f: f.filtered_distance_mm,
         expected_distance),
        ('原始方位角', 'deg', valid_link, lambda f: f.raw_angle_deg,
         expected_angle),
        ('滤波方位角', 'deg', poses, lambda f: f.filtered_angle_deg,
         expected_angle),
        ('X坐标', 'm', poses, lambda f: f.x_m, capture.true_x_m),
        ('Y坐标', 'm', poses, lambda f: f.y_m, capture.true_y_m),
        ('门锁边界距离', 'm', poses, lambda f: f.boundary_m,
         max(math.hypot(capture.true_x_m, capture.true_y_m) - 0.3, 0.0)),
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


def fit_distance_calibration(captures: Iterable[CapturePoint]) -> LinearFit:
    pairs: list[tuple[float, float]] = []
    for capture in captures:
        expected = expected_distance_mm(
            capture.true_x_m,
            capture.true_y_m,
            capture.anchor_height_cm,
            capture.tag_height_cm,
        )
        for frame in capture.frames:
            if frame.link_ok:
                pairs.append((frame.raw_distance_cm * 10.0, expected))
    return _linear_fit(pairs)
