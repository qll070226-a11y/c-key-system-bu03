import unittest

from c_key_debugger.acceptance import (
    StaticAcceptanceConfig,
    assess_static_capture,
    circular_error_deg,
    static_report_markdown,
)
from c_key_debugger.telemetry import DiagnosticFrame


def make_frame(
    index: int,
    *,
    boundary_m: float = 2.0,
    angle_deg: float = -30.0,
    state: str = 'SENSING',
    address: int = 0x6E19,
    tag_id: int = 0,
    accepted_id: int = 0,
    pose_valid: bool = True,
) -> DiagnosticFrame:
    center_m = boundary_m + 0.3
    return DiagnosticFrame(
        timestamp_ms=1000 + index * 20,
        sequence=index,
        tag_address=address,
        tag_id=tag_id,
        accepted_id=accepted_id,
        link_ok=True,
        measurement_ready=pose_valid,
        pose_valid=pose_valid,
        raw_distance_cm=round(center_m * 100.0),
        raw_angle_deg=angle_deg,
        corrected_distance_mm=center_m * 1000.0,
        filtered_distance_mm=center_m * 1000.0,
        filtered_angle_deg=angle_deg,
        x_m=0.0,
        y_m=center_m,
        boundary_m=boundary_m,
        state=state,
        events=0,
        accepted_frames=index + 1,
        rejected_frames=0,
    )


class AcceptanceTests(unittest.TestCase):
    def test_circular_error_wraps(self) -> None:
        self.assertAlmostEqual(circular_error_deg(-179.0, 179.0), 2.0)
        self.assertAlmostEqual(circular_error_deg(179.0, -179.0), -2.0)

    def test_static_capture_passes_official_tolerances(self) -> None:
        frames = [
            make_frame(i, boundary_m=2.08, angle_deg=-34.0)
            for i in range(320)
        ]
        report = assess_static_capture(
            frames,
            0,
            StaticAcceptanceConfig(
                expected_boundary_m=2.0,
                expected_angle_deg=-30.0,
                display_confirmed=True,
                expected_state='SENSING',
            ),
        )
        self.assertEqual(report.result, '通过')
        self.assertAlmostEqual(report.boundary_error_m or 0.0, 0.08)
        self.assertAlmostEqual(report.angle_error_deg or 0.0, -4.0)

    def test_static_capture_rejects_distance_and_identity(self) -> None:
        frames = [
            make_frame(i, boundary_m=2.31, address=0x1234)
            for i in range(320)
        ]
        report = assess_static_capture(
            frames,
            0,
            StaticAcceptanceConfig(
                expected_boundary_m=2.0,
                expected_angle_deg=-30.0,
                display_confirmed=True,
            ),
        )
        self.assertEqual(report.result, '未通过')
        self.assertFalse(report.checks['全部诊断帧身份一致'])
        self.assertFalse(report.checks['径向距离误差不大于0.3m'])

    def test_report_contains_measurements_and_checks(self) -> None:
        frames = [make_frame(i) for i in range(320)]
        report = assess_static_capture(
            frames,
            0,
            StaticAcceptanceConfig(
                expected_boundary_m=2.0,
                expected_angle_deg=-30.0,
                display_confirmed=True,
            ),
        )
        markdown = static_report_markdown(
            report,
            tested_at='2026-08-02T12:00:00+08:00',
            port='COM21',
            notes='正前方无遮挡',
        )
        self.assertIn('C题静态定位验收报告', markdown)
        self.assertIn('径向距离误差', markdown)
        self.assertIn('正前方无遮挡', markdown)


if __name__ == '__main__':
    unittest.main()
