from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PySide6 import QtCore, QtWidgets

from .ui import MainWindow


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description='C题 UWB定位诊断上位机')
    parser.add_argument('--demo', action='store_true', help='使用模拟数据启动')
    parser.add_argument('--screenshot', type=Path, help='启动后保存界面截图')
    parser.add_argument(
        '--quit-after-ms', type=int, default=0,
        help='指定毫秒后自动退出，用于界面自动检查',
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    app = QtWidgets.QApplication(sys.argv[:1])
    app.setApplicationName('C题 UWB定位诊断台')
    app.setStyle('Fusion')
    window = MainWindow()
    window.show()
    if args.demo:
        window.start_demo()
    if args.screenshot:
        args.screenshot.parent.mkdir(parents=True, exist_ok=True)
        QtCore.QTimer.singleShot(1400, lambda: window.save_screenshot(args.screenshot))
    if args.quit_after_ms > 0:
        QtCore.QTimer.singleShot(args.quit_after_ms, app.quit)
    return app.exec()


if __name__ == '__main__':
    raise SystemExit(main())
