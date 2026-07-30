from __future__ import annotations

import math
import random

from .telemetry import DiagnosticFrame


class DemoGenerator:
    def __init__(self, seed: int = 2026) -> None:
        self._random = random.Random(seed)
        self._sequence = 0
        self._time_ms = 0
        self._filtered = [0.0, 0.0]

    def next_frame(self) -> DiagnosticFrame:
        self._sequence += 1
        self._time_ms += 100
        phase = self._sequence * 0.025
        true_x = 0.72 * math.sin(phase)
        true_y = 1.65 + 0.70 * math.sin(phase * 0.43)
        true_ranges = (
            math.hypot(true_x + 0.24, true_y) * 1000.0,
            math.hypot(true_x - 0.24, true_y) * 1000.0,
        )

        multipath_bias = 155.0 if true_x < -0.20 else 0.0
        raw0 = (true_ranges[0] + 87.0) / 1.031273
        raw1 = (true_ranges[1] + 45.0) / 0.995319 + multipath_bias
        raw0 += self._random.gauss(0.0, 24.0)
        raw1 += self._random.gauss(0.0, 30.0)
        corrected = (
            raw0 * 1.031273 - 87.0,
            raw1 * 0.995319 - 45.0,
        )
        for index, value in enumerate(corrected):
            if self._filtered[index] == 0.0:
                self._filtered[index] = value
            else:
                self._filtered[index] += 0.35 * (value - self._filtered[index])

        measured_x = true_x - multipath_bias / 480.0 * max(true_y, 1.0)
        measured_x += self._random.gauss(0.0, 0.025)
        measured_y = true_y + self._random.gauss(0.0, 0.018)
        angle = math.degrees(math.atan2(measured_x, measured_y))
        boundary = max(math.hypot(measured_x, measured_y) - 0.3, 0.0)
        if abs(angle) > 47.0:
            state = 'OUT_OF_ANGLE'
        elif boundary < 0.9:
            state = 'UNLOCKED'
        elif boundary < 1.9:
            state = 'WELCOME'
        else:
            state = 'SENSING'

        return DiagnosticFrame(
            timestamp_ms=self._time_ms,
            sequence=self._sequence,
            tag_id=0,
            accepted_id=0,
            link_ok=True,
            valid_mask=3,
            measurement_ready=True,
            pose_valid=True,
            raw_a0_mm=max(round(raw0), 0),
            raw_a1_mm=max(round(raw1), 0),
            corrected_a0_mm=corrected[0],
            corrected_a1_mm=corrected[1],
            filtered_a0_mm=self._filtered[0],
            filtered_a1_mm=self._filtered[1],
            x_m=measured_x,
            y_m=measured_y,
            boundary_m=boundary,
            angle_deg=angle,
            residual_m=abs(self._random.gauss(0.025, 0.012)),
            state=state,
            events=0,
            accepted_frames=self._sequence,
            rejected_frames=self._sequence // 137,
        )
