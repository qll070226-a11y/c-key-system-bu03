from __future__ import annotations

import math
import random

from .telemetry import DiagnosticFrame


class DemoGenerator:
    def __init__(self, seed: int = 2026) -> None:
        self._random = random.Random(seed)
        self._sequence = 0
        self._time_ms = 0
        self._filtered_distance = 0.0
        self._filtered_angle = 0.0

    def next_frame(self) -> DiagnosticFrame:
        self._sequence += 1
        self._time_ms += 100
        phase = self._sequence * 0.025
        true_x = 1.45 * math.sin(phase)
        true_y = 1.65 + 0.70 * math.sin(phase * 0.43)
        true_distance_mm = math.hypot(true_x, true_y) * 1000.0
        true_angle = math.degrees(math.atan2(true_x, true_y))

        raw_distance_mm = (true_distance_mm + 65.0) / 1.015
        raw_distance_mm += self._random.gauss(0.0, 22.0)
        raw_angle = true_angle + self._random.gauss(0.0, 2.5)
        if true_x < -0.45:
            raw_angle += 7.0
        corrected_distance = raw_distance_mm * 1.015 - 65.0
        if self._filtered_distance == 0.0:
            self._filtered_distance = corrected_distance
            self._filtered_angle = raw_angle
        else:
            self._filtered_distance += 0.30 * (
                corrected_distance - self._filtered_distance
            )
            self._filtered_angle += 0.22 * (raw_angle - self._filtered_angle)

        distance_m = self._filtered_distance / 1000.0
        angle_rad = math.radians(self._filtered_angle)
        measured_x = distance_m * math.sin(angle_rad)
        measured_y = distance_m * math.cos(angle_rad)
        boundary = max(distance_m - 0.3, 0.0)
        if abs(self._filtered_angle) > 45.0:
            state = 'OUT_OF_ANGLE'
        elif boundary < 0.9:
            state = 'UNLOCKED'
        elif boundary < 1.9:
            state = 'WELCOME'
        else:
            state = 'SENSING'

        return DiagnosticFrame(
            timestamp_ms=self._time_ms,
            sequence=self._sequence & 0xFF,
            tag_address=0x6E19,
            tag_id=0,
            accepted_id=0,
            link_ok=True,
            measurement_ready=True,
            pose_valid=True,
            raw_distance_cm=max(round(raw_distance_mm / 10.0), 0),
            raw_angle_deg=raw_angle,
            corrected_distance_mm=corrected_distance,
            filtered_distance_mm=self._filtered_distance,
            filtered_angle_deg=self._filtered_angle,
            x_m=measured_x,
            y_m=measured_y,
            boundary_m=boundary,
            state=state,
            events=0,
            accepted_frames=self._sequence,
            rejected_frames=self._sequence // 137,
        )
