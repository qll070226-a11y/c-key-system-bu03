import math
import unittest

from c_key_debugger.analysis import (
    CapturePoint,
    expected_ranges_mm,
    fit_anchor_calibration,
    summarize,
    summarize_capture,
)
from c_key_debugger.telemetry import DiagnosticFrame


def make_frame(raw0: int, raw1: int, x: float = 0.0, y: float = 1.5) -> DiagnosticFrame:
    return DiagnosticFrame(
        timestamp_ms=1, sequence=1, tag_id=0, accepted_id=0,
        link_ok=True, valid_mask=3, measurement_ready=True, pose_valid=True,
        raw_a0_mm=raw0, raw_a1_mm=raw1,
        corrected_a0_mm=float(raw0), corrected_a1_mm=float(raw1),
        filtered_a0_mm=float(raw0), filtered_a1_mm=float(raw1),
        x_m=x, y_m=y, boundary_m=1.2, angle_deg=0.0, residual_m=0.01,
        state='WELCOME', events=0, accepted_frames=1, rejected_frames=0,
    )


class AnalysisTests(unittest.TestCase):
    def test_robust_stats(self) -> None:
        result = summarize([1, 2, 3, 100])
        assert result is not None
        self.assertEqual(result.count, 4)
        self.assertEqual(result.median, 2.5)
        self.assertEqual(result.mad, 1.0)

    def test_expected_ranges(self) -> None:
        ranges = expected_ranges_mm(0.0, 1.0, ((-0.22, 0.0), (0.22, 0.0)))
        self.assertAlmostEqual(ranges[0], 1023.9141, places=3)
        self.assertAlmostEqual(ranges[0], ranges[1])

    def test_expected_ranges_include_height_difference(self) -> None:
        ranges = expected_ranges_mm(
            0.0,
            1.0,
            ((-0.22, 0.0), (0.22, 0.0)),
            anchor_height_cm=20.0,
            tag_height_cm=50.0,
        )
        expected = math.sqrt(1.0 ** 2 + 0.22 ** 2 + 0.30 ** 2) * 1000.0
        self.assertAlmostEqual(ranges[0], expected, places=6)
        self.assertAlmostEqual(ranges[0], ranges[1])

    def test_capture_summary(self) -> None:
        capture = CapturePoint('P1', 0.0, 1.0, frames=[make_frame(1030, 1020)])
        rows = summarize_capture(capture, ((-0.22, 0.0), (0.22, 0.0)))
        self.assertEqual(rows[0].name, 'A0原始')
        self.assertAlmostEqual(rows[0].median_error or 0.0, 6.0859, places=3)

    def test_capture_summary_uses_capture_heights(self) -> None:
        anchors = ((-0.22, 0.0), (0.22, 0.0))
        capture = CapturePoint(
            'P1', 0.0, 1.0,
            anchor_height_cm=20.0,
            tag_height_cm=50.0,
            frames=[make_frame(1070, 1070)],
        )
        rows = summarize_capture(capture, anchors)
        expected = expected_ranges_mm(
            0.0, 1.0, anchors,
            anchor_height_cm=20.0,
            tag_height_cm=50.0,
        )
        self.assertAlmostEqual(rows[0].reference or 0.0, expected[0], places=6)
        self.assertAlmostEqual(rows[1].reference or 0.0, expected[1], places=6)

    def test_linear_calibration(self) -> None:
        anchors = ((-0.22, 0.0), (0.22, 0.0))
        captures = []
        for y in (1.0, 2.0, 3.0):
            true0, true1 = expected_ranges_mm(0.0, y, anchors)
            raw0 = round((true0 + 80.0) / 1.02)
            raw1 = round((true1 - 30.0) / 0.98)
            captures.append(
                CapturePoint(str(y), 0.0, y, frames=[make_frame(raw0, raw1)])
            )
        fit0, fit1 = fit_anchor_calibration(captures, anchors)
        self.assertAlmostEqual(fit0.scale, 1.02, delta=0.002)
        self.assertAlmostEqual(fit0.offset_mm, -80.0, delta=3.0)
        self.assertAlmostEqual(fit1.scale, 0.98, delta=0.002)
        self.assertAlmostEqual(fit1.offset_mm, 30.0, delta=3.0)


if __name__ == '__main__':
    unittest.main()
