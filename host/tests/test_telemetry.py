import unittest

from c_key_debugger.telemetry import (
    DIAGNOSTIC_FIELDS,
    TelemetryParseError,
    frame_from_csv_mapping,
    parse_diagnostic_line,
)


VALID = (
    'I (1234) C_KEY_DIAG: '
    'C_KEY_DIAG_V2,1234,42,28185,0,0,1,1,1,143,8,1430.0,1412.0,7.50,'
    '0.184,1.400,1.112,WELCOME,2,42,3'
)


class TelemetryTests(unittest.TestCase):
    def test_parse_prefixed_log_line(self) -> None:
        frame = parse_diagnostic_line(VALID)
        self.assertIsNotNone(frame)
        assert frame is not None
        self.assertEqual(frame.sequence, 42)
        self.assertEqual(frame.tag_address, 0x6E19)
        self.assertEqual(frame.raw_distance_cm, 143)
        self.assertAlmostEqual(frame.filtered_angle_deg, 7.5)
        self.assertEqual(frame.state, 'WELCOME')

    def test_ignore_unrelated_line(self) -> None:
        self.assertIsNone(parse_diagnostic_line('I (1) boot complete'))

    def test_reject_bad_columns_and_non_finite(self) -> None:
        with self.assertRaises(TelemetryParseError):
            parse_diagnostic_line('C_KEY_DIAG_V2,1,2')
        with self.assertRaises(TelemetryParseError):
            parse_diagnostic_line(VALID.replace('1.112', 'nan'))

    def test_csv_round_trip(self) -> None:
        frame = parse_diagnostic_line(VALID)
        assert frame is not None
        mapping = dict(zip(DIAGNOSTIC_FIELDS, frame.as_csv_values(), strict=True))
        self.assertEqual(frame_from_csv_mapping(mapping), frame)


if __name__ == '__main__':
    unittest.main()
