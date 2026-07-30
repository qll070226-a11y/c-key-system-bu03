from __future__ import annotations

import threading
import time
from pathlib import Path

import serial
from PySide6.QtCore import QThread, Signal
from serial.tools import list_ports

from .recording import load_session
from .telemetry import DiagnosticFrame


def available_ports() -> list[tuple[str, str]]:
    ports = sorted(list_ports.comports(), key=lambda item: item.device)
    return [
        (port.device, port.description or '未知串口设备')
        for port in ports
    ]


class SerialWorker(QThread):
    line_received = Signal(str)
    source_opened = Signal(str)
    source_closed = Signal()
    source_error = Signal(str)

    def __init__(self, port: str, baud_rate: int) -> None:
        super().__init__()
        self.port = port
        self.baud_rate = baud_rate
        self._stop_event = threading.Event()
        self._serial: serial.Serial | None = None

    def stop(self) -> None:
        self._stop_event.set()

    def run(self) -> None:
        try:
            self._serial = serial.Serial(
                self.port,
                self.baud_rate,
                timeout=0.2,
                write_timeout=0.2,
            )
            self.source_opened.emit(f'{self.port} @ {self.baud_rate}')
            while not self._stop_event.is_set():
                data = self._serial.readline()
                if data:
                    self.line_received.emit(data.decode('utf-8', errors='replace').rstrip())
        except (serial.SerialException, OSError) as exc:
            self.source_error.emit(str(exc))
        finally:
            if self._serial is not None and self._serial.is_open:
                self._serial.close()
            self._serial = None
            self.source_closed.emit()


class ReplayWorker(QThread):
    frame_received = Signal(object)
    source_opened = Signal(str)
    source_closed = Signal()
    source_error = Signal(str)

    def __init__(self, path: str | Path, speed: float = 1.0) -> None:
        super().__init__()
        self.path = Path(path)
        self.speed = max(speed, 0.1)
        self._stop_event = threading.Event()

    def stop(self) -> None:
        self._stop_event.set()

    def run(self) -> None:
        try:
            records = list(load_session(self.path))
            if not records:
                raise ValueError('会话文件没有数据')
            self.source_opened.emit(f'回放: {self.path.name}')
            previous_ms: int | None = None
            for _, frame in records:
                if self._stop_event.is_set():
                    break
                if previous_ms is not None:
                    delay = max(frame.timestamp_ms - previous_ms, 0) / 1000.0
                    time.sleep(min(delay / self.speed, 0.25))
                self.frame_received.emit(frame)
                previous_ms = frame.timestamp_ms
        except (OSError, ValueError, KeyError) as exc:
            self.source_error.emit(str(exc))
        finally:
            self.source_closed.emit()


def frame_signal_type() -> type[DiagnosticFrame]:
    return DiagnosticFrame
