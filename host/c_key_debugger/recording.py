from __future__ import annotations

import csv
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterator, TextIO

from .telemetry import DIAGNOSTIC_FIELDS, DiagnosticFrame, frame_from_csv_mapping


METADATA_FIELDS = (
    'host_time_iso', 'point_label', 'true_x_m', 'true_y_m', 'height_cm', 'notes',
)
SESSION_FIELDS = METADATA_FIELDS + DIAGNOSTIC_FIELDS


@dataclass(frozen=True, slots=True)
class PointMetadata:
    label: str = ''
    true_x_m: float = 0.0
    true_y_m: float = 0.0
    height_cm: float = 0.0
    notes: str = ''


class SessionRecorder:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self._file: TextIO | None = None
        self._writer: csv.DictWriter | None = None

    @property
    def active(self) -> bool:
        return self._file is not None

    def start(self) -> None:
        if self.active:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open('w', newline='', encoding='utf-8-sig')
        self._writer = csv.DictWriter(self._file, fieldnames=SESSION_FIELDS)
        self._writer.writeheader()

    def record(self, frame: DiagnosticFrame, metadata: PointMetadata) -> None:
        if self._writer is None or self._file is None:
            raise RuntimeError('记录器尚未启动')
        row = {
            'host_time_iso': datetime.now(timezone.utc).isoformat(),
            'point_label': metadata.label,
            'true_x_m': metadata.true_x_m,
            'true_y_m': metadata.true_y_m,
            'height_cm': metadata.height_cm,
            'notes': metadata.notes,
        }
        row.update(dict(zip(DIAGNOSTIC_FIELDS, frame.as_csv_values(), strict=True)))
        self._writer.writerow(row)
        self._file.flush()

    def close(self) -> None:
        if self._file is not None:
            self._file.close()
        self._file = None
        self._writer = None

    def __enter__(self) -> 'SessionRecorder':
        self.start()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def load_session(path: str | Path) -> Iterator[tuple[PointMetadata, DiagnosticFrame]]:
    with Path(path).open('r', newline='', encoding='utf-8-sig') as file:
        reader = csv.DictReader(file)
        missing = [field for field in SESSION_FIELDS if field not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(f'会话文件缺少字段: {missing}')
        for row in reader:
            metadata = PointMetadata(
                label=row['point_label'],
                true_x_m=float(row['true_x_m']),
                true_y_m=float(row['true_y_m']),
                height_cm=float(row['height_cm']),
                notes=row['notes'],
            )
            yield metadata, frame_from_csv_mapping(row)
