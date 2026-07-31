import math
import unittest

from c_key_debugger.demo import DemoGenerator


class DemoTests(unittest.TestCase):
    def test_frames_are_finite_and_monotonic(self) -> None:
        generator = DemoGenerator()
        frames = [generator.next_frame() for _ in range(500)]
        self.assertEqual(frames[0].sequence, 1)
        self.assertEqual(frames[-1].sequence, 500 & 0xFF)
        self.assertTrue(all(
            math.isfinite(frame.filtered_angle_deg) and frame.measurement_ready
            for frame in frames
        ))
        self.assertTrue(any(frame.state == 'OUT_OF_ANGLE' for frame in frames))


if __name__ == '__main__':
    unittest.main()
