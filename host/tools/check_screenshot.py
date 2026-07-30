from __future__ import annotations

import argparse

import numpy as np
from PySide6.QtGui import QImage


def main() -> int:
    parser = argparse.ArgumentParser(description='检查上位机截图是否非空')
    parser.add_argument('path')
    args = parser.parse_args()
    image = QImage(args.path).convertToFormat(QImage.Format.Format_RGBA8888)
    if image.isNull():
        print('image_load_failed')
        return 1
    pixels = np.frombuffer(image.bits(), dtype=np.uint8)
    pixels = pixels.reshape(image.height(), image.bytesPerLine())
    pixels = pixels[:, : image.width() * 4].reshape(image.height(), image.width(), 4)
    rgb = pixels[:, :, :3]
    standard_deviation = float(rgb.std())
    non_white_ratio = float(np.any(rgb < 245, axis=2).mean())
    print(
        f'{image.width()}x{image.height()} std={standard_deviation:.2f} '
        f'non_white={non_white_ratio:.3f}'
    )
    valid = (
        image.width() >= 1100 and image.height() >= 700
        and standard_deviation >= 20.0 and non_white_ratio >= 0.10
    )
    return 0 if valid else 1


if __name__ == '__main__':
    raise SystemExit(main())
