import tempfile
import unittest
from pathlib import Path

from c_key_debugger.recording import PointMetadata, SessionRecorder, load_session
from test_analysis import make_frame


class RecordingTests(unittest.TestCase):
    def test_write_and_read(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'session.csv'
            original = make_frame(1000, 1010)
            metadata = PointMetadata('P1', 0.0, 1.0, 20.0, '抬高测试')
            with SessionRecorder(path) as recorder:
                recorder.record(original, metadata)
            loaded = list(load_session(path))
            self.assertEqual(len(loaded), 1)
            self.assertEqual(loaded[0][0], metadata)
            self.assertEqual(loaded[0][1], original)


if __name__ == '__main__':
    unittest.main()
