import csv
import tempfile
import unittest
from pathlib import Path

from c_key_debugger.recording import PointMetadata, SessionRecorder, load_session
from c_key_debugger.telemetry import DIAGNOSTIC_FIELDS
from test_analysis import make_frame


class RecordingTests(unittest.TestCase):
    def test_write_and_read(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'session.csv'
            original = make_frame(1000, 1010)
            metadata = PointMetadata(
                label='P1', true_x_m=0.0, true_y_m=1.0,
                anchor_height_cm=20.0, tag_height_cm=45.0, notes='抬高测试',
            )
            with SessionRecorder(path) as recorder:
                recorder.record(original, metadata)
            loaded = list(load_session(path))
            self.assertEqual(len(loaded), 1)
            self.assertEqual(loaded[0][0], metadata)
            self.assertEqual(loaded[0][1], original)

    def test_load_legacy_height_as_tag_height(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'legacy_session.csv'
            original = make_frame(1000, 1010)
            fields = (
                'host_time_iso', 'point_label', 'true_x_m', 'true_y_m',
                'height_cm', 'notes', *DIAGNOSTIC_FIELDS,
            )
            row = {
                'host_time_iso': '2026-07-31T00:00:00+00:00',
                'point_label': 'P1', 'true_x_m': 0.0, 'true_y_m': 1.0,
                'height_cm': 26.0, 'notes': '旧格式',
            }
            row.update(dict(zip(DIAGNOSTIC_FIELDS, original.as_csv_values(), strict=True)))
            with path.open('w', newline='', encoding='utf-8-sig') as file:
                writer = csv.DictWriter(file, fieldnames=fields)
                writer.writeheader()
                writer.writerow(row)

            metadata, loaded_frame = next(load_session(path))
            self.assertEqual(metadata.anchor_height_cm, 0.0)
            self.assertEqual(metadata.tag_height_cm, 26.0)
            self.assertEqual(loaded_frame, original)


if __name__ == '__main__':
    unittest.main()
