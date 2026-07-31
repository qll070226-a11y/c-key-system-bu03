import math
import unittest

from c_key_debugger.analysis import (
    CapturePoint,
    expected_distance_mm,
    fit_distance_calibration,
    summarize,
    summarize_capture,
)
from c_key_debugger.telemetry import DiagnosticFrame


def make_frame(
    raw_distance_cm: int,
    raw_angle_deg: float = 0.0,
    x: float = 0.0,
    y: float = 1.5,
) -> DiagnosticFrame:
    raw_mm = raw_distance_cm * 10.0
    return DiagnosticFrame(
        timestamp_ms=1, sequence=1, tag_address=0x6E19,
        tag_id=0, accepted_id=0, link_ok=True,
        measurement_ready=True, pose_valid=True,
        raw_distance_cm=raw_distance_cm, raw_angle_deg=raw_angle_deg,
        corrected_distance_mm=raw_mm, filtered_distance_mm=raw_mm,
        filtered_angle_deg=raw_angle_deg,
        x_m=x, y_m=y, boundary_m=max(math.hypot(x, y) - 0.3, 0.0),
        state='WELCOME', events=0, accepted_frames=1, rejected_frames=0,
    )


class AnalysisTests(unittest.TestCase):
    def test_robust_stats(self) -> None:
        result = summarize([1, 2, 3, 100])
        assert result is not None
        self.assertEqual(result.count, 4)
        self.assertEqual(result.median, 2.5)
        self.assertEqual(result.mad, 1.0)

    def test_expected_distance(self) -> None:
        self.assertAlmostEqual(expected_distance_mm(0.6, 0.8), 1000.0)

    def test_expected_distance_includes_height_difference(self) -> None:
        expected = math.sqrt(1.0 ** 2 + 0.30 ** 2) * 1000.0
        self.assertAlmostEqual(
            expected_distance_mm(
                0.0, 1.0, anchor_height_cm=20.0, tag_height_cm=50.0
            ),
            expected,
        )

    def test_capture_summary(self) -> None:
        capture = CapturePoint(
            'P1', 0.0, 1.0, frames=[make_frame(103, raw_angle_deg=2.0)]
        )
        rows = summarize_capture(capture)
        self.assertEqual(rows[0].name, '原始距离')
        self.assertAlmostEqual(rows[0].median_error or 0.0, 30.0)
        self.assertEqual(rows[3].name, '原始方位角')
        self.assertAlmostEqual(rows[3].median_error or 0.0, 2.0)

    def test_capture_summary_uses_capture_heights(self) -> None:
        capture = CapturePoint(
            'P1', 0.0, 1.0,
            anchor_height_cm=20.0,
            tag_height_cm=50.0,
            frames=[make_frame(104)],
        )
        rows = summarize_capture(capture)
        expected = expected_distance_mm(
            0.0, 1.0, anchor_height_cm=20.0, tag_height_cm=50.0
        )
        self.assertAlmostEqual(rows[0].reference or 0.0, expected)

    def test_linear_calibration(self) -> None:
        captures = []
        for distance_m in (1.0, 2.0, 3.0):
            raw_mm = (distance_m * 1000.0 + 80.0) / 1.02
            captures.append(
                CapturePoint(
                    str(distance_m), 0.0, distance_m,
                    frames=[make_frame(round(raw_mm / 10.0))],
                )
            )
        fit = fit_distance_calibration(captures)
        self.assertAlmostEqual(fit.scale, 1.02, delta=0.01)
        self.assertAlmostEqual(fit.offset_mm, -80.0, delta=15.0)


if __name__ == '__main__':
    unittest.main()
