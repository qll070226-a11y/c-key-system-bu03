import unittest

from c_key_debugger.telemetry import (
    DIAGNOSTIC_FIELDS,
    TelemetryParseError,
    frame_from_csv_mapping,
    parse_diagnostic_line,
)


VALID = (
    'I (1234) C_KEY_DIAG: '
    'C_KEY_DIAG_V1,1234,42,0,0,1,3,1,1,1430,860,1388.0,811.0,'
    '1401.0,824.0,0.250,1.800,1.500,8.00,0.030,WELCOME,2,42,3'
)


class TelemetryTests(unittest.TestCase):
    def test_parse_prefixed_log_line(self) -> None:
        frame = parse_diagnostic_line(VALID)
        self.assertIsNotNone(frame)
        assert frame is not None
        self.assertEqual(frame.sequence, 42)
        self.assertEqual(frame.valid_mask, 3)
        self.assertAlmostEqual(frame.corrected_a0_mm, 1388.0)
        self.assertAlmostEqual(frame.angle_deg, 8.0)
        self.assertEqual(frame.state, 'WELCOME')

    def test_ignore_unrelated_line(self) -> None:
        self.assertIsNone(parse_diagnostic_line('I (1) boot complete'))

    def test_reject_bad_columns_and_non_finite(self) -> None:
        with self.assertRaises(TelemetryParseError):
            parse_diagnostic_line('C_KEY_DIAG_V1,1,2')
        with self.assertRaises(TelemetryParseError):
            parse_diagnostic_line(VALID.replace('0.030', 'nan'))

    def test_csv_round_trip(self) -> None:
        frame = parse_diagnostic_line(VALID)
        assert frame is not None
        mapping = dict(zip(DIAGNOSTIC_FIELDS, frame.as_csv_values(), strict=True))
        self.assertEqual(frame_from_csv_mapping(mapping), frame)


if __name__ == '__main__':
    unittest.main()
