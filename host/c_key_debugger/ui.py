from __future__ import annotations

import csv
import math
from collections import deque
from datetime import datetime
from pathlib import Path

import pyqtgraph as pg
from PySide6 import QtCore, QtGui, QtWidgets

from .analysis import (
    CapturePoint,
    fit_distance_calibration,
    summarize_capture,
)
from .demo import DemoGenerator
from .recording import PointMetadata, SessionRecorder
from .serial_io import ReplayWorker, SerialWorker, available_ports
from .telemetry import DiagnosticFrame, TelemetryParseError, parse_diagnostic_line


PLOT_HISTORY = 360


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle('C题 UWB定位诊断台')
        self.resize(1440, 900)
        self.setMinimumSize(1120, 720)

        self._worker: SerialWorker | ReplayWorker | None = None
        self._demo_timer: QtCore.QTimer | None = None
        self._demo_generator: DemoGenerator | None = None
        self._recorder: SessionRecorder | None = None
        self._captures: list[CapturePoint] = []
        self._active_capture: CapturePoint | None = None
        self._capture_target = 0
        self._parse_errors = 0
        self._last_sequence: int | None = None
        self._sequence_gaps = 0

        self._time = deque(maxlen=PLOT_HISTORY)
        self._series = {
            name: deque(maxlen=PLOT_HISTORY)
            for name in (
                'raw_distance', 'corrected_distance', 'filtered_distance',
                'raw_angle', 'filtered_angle', 'boundary',
            )
        }
        self._trail_x = deque(maxlen=PLOT_HISTORY)
        self._trail_y = deque(maxlen=PLOT_HISTORY)
        self._metric_values: dict[str, QtWidgets.QLabel] = {}
        self._build_ui()
        self._apply_style()
        self.refresh_ports()

    def _build_ui(self) -> None:
        root = QtWidgets.QWidget()
        root_layout = QtWidgets.QVBoxLayout(root)
        root_layout.setContentsMargins(14, 14, 14, 14)
        root_layout.setSpacing(10)
        root_layout.addWidget(self._build_connection_bar())
        self.tabs = QtWidgets.QTabWidget()
        self.tabs.addTab(self._build_live_tab(), '实时诊断')
        self.tabs.addTab(self._build_capture_tab(), '定点采集')
        self.tabs.addTab(self._build_log_tab(), '原始日志')
        root_layout.addWidget(self.tabs, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage('未连接')

    def _icon_button(
        self,
        text: str,
        icon: QtWidgets.QStyle.StandardPixmap,
        checkable: bool = False,
    ) -> QtWidgets.QPushButton:
        button = QtWidgets.QPushButton(text)
        button.setIcon(self.style().standardIcon(icon))
        button.setCheckable(checkable)
        button.setMinimumHeight(34)
        return button

    def _build_connection_bar(self) -> QtWidgets.QWidget:
        bar = QtWidgets.QFrame()
        bar.setObjectName('connectionBar')
        layout = QtWidgets.QHBoxLayout(bar)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.addWidget(QtWidgets.QLabel('ESP32诊断串口'))
        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setMinimumWidth(250)
        layout.addWidget(self.port_combo)
        self.refresh_button = self._icon_button(
            '刷新', QtWidgets.QStyle.StandardPixmap.SP_BrowserReload
        )
        self.refresh_button.clicked.connect(self.refresh_ports)
        layout.addWidget(self.refresh_button)
        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.addItems(('115200', '460800', '921600'))
        self.baud_combo.setCurrentText('115200')
        self.baud_combo.setFixedWidth(100)
        layout.addWidget(self.baud_combo)
        self.connect_button = self._icon_button(
            '连接', QtWidgets.QStyle.StandardPixmap.SP_DialogApplyButton
        )
        self.connect_button.clicked.connect(self.connect_serial)
        layout.addWidget(self.connect_button)
        self.disconnect_button = self._icon_button(
            '断开', QtWidgets.QStyle.StandardPixmap.SP_DialogCancelButton
        )
        self.disconnect_button.setEnabled(False)
        self.disconnect_button.clicked.connect(self.stop_source)
        layout.addWidget(self.disconnect_button)
        line = QtWidgets.QFrame()
        line.setFrameShape(QtWidgets.QFrame.Shape.VLine)
        layout.addWidget(line)
        self.record_button = self._icon_button(
            '记录CSV',
            QtWidgets.QStyle.StandardPixmap.SP_DialogSaveButton,
            checkable=True,
        )
        self.record_button.clicked.connect(self.toggle_recording)
        layout.addWidget(self.record_button)
        self.replay_button = self._icon_button(
            '回放', QtWidgets.QStyle.StandardPixmap.SP_MediaPlay
        )
        self.replay_button.clicked.connect(self.open_replay)
        layout.addWidget(self.replay_button)
        layout.addStretch()
        self.source_label = QtWidgets.QLabel('未连接')
        self.source_label.setObjectName('sourceState')
        layout.addWidget(self.source_label)
        return bar

    def _metric_widget(self, key: str, title: str) -> QtWidgets.QWidget:
        frame = QtWidgets.QFrame()
        frame.setObjectName('metric')
        layout = QtWidgets.QVBoxLayout(frame)
        layout.setContentsMargins(10, 7, 10, 7)
        layout.setSpacing(2)
        title_label = QtWidgets.QLabel(title)
        title_label.setObjectName('metricTitle')
        value = QtWidgets.QLabel('--')
        value.setObjectName('metricValue')
        value.setMinimumWidth(105)
        layout.addWidget(title_label)
        layout.addWidget(value)
        self._metric_values[key] = value
        return frame

    def _configure_plot(self, plot: pg.PlotWidget, title: str, unit: str) -> None:
        plot.setBackground('#ffffff')
        plot.setTitle(title, color='#111827', size='10pt')
        plot.setLabel('left', unit)
        plot.setLabel('bottom', '时间', units='s')
        plot.showGrid(x=True, y=True, alpha=0.18)
        plot.getAxis('left').setTextPen('#4b5563')
        plot.getAxis('bottom').setTextPen('#4b5563')

    def _build_live_tab(self) -> QtWidgets.QWidget:
        tab = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(tab)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(8)
        metrics = QtWidgets.QHBoxLayout()
        for key, title in (
            ('state', '门锁状态'), ('ids', '钥匙 / 门锁ID'),
            ('address', 'UWB标签地址'), ('raw', '原始距离 / 角度'),
            ('corrected', '校正距离'), ('filtered', '滤波距离 / 角度'),
            ('pose', '位置 X / Y'), ('boundary', '门锁边界距离'),
            ('quality', '帧质量'),
        ):
            metrics.addWidget(self._metric_widget(key, title), 1)
        layout.addLayout(metrics)

        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        plots = QtWidgets.QWidget()
        plots_layout = QtWidgets.QVBoxLayout(plots)
        plots_layout.setContentsMargins(0, 0, 0, 0)
        plots_layout.setSpacing(6)
        self.distance_plot = pg.PlotWidget()
        self.angle_plot = pg.PlotWidget()
        self.boundary_plot = pg.PlotWidget()
        self._configure_plot(self.distance_plot, 'BU04 PDOA距离分层', 'mm')
        self._configure_plot(self.angle_plot, 'BU04 PDOA方位角', 'deg')
        self._configure_plot(self.boundary_plot, '门锁外壳边界距离', 'cm')
        plots_layout.addWidget(self.distance_plot)
        plots_layout.addWidget(self.angle_plot)
        plots_layout.addWidget(self.boundary_plot)
        self.curves = {
            'raw_distance': self.distance_plot.plot(
                pen=pg.mkPen('#6b7280', width=1), name='原始'
            ),
            'corrected_distance': self.distance_plot.plot(
                pen=pg.mkPen('#2563eb', width=2), name='校正'
            ),
            'filtered_distance': self.distance_plot.plot(
                pen=pg.mkPen('#16a34a', width=2), name='滤波'
            ),
            'raw_angle': self.angle_plot.plot(
                pen=pg.mkPen('#9ca3af', width=1), name='原始'
            ),
            'filtered_angle': self.angle_plot.plot(
                pen=pg.mkPen('#7c3aed', width=2), name='滤波'
            ),
            'boundary': self.boundary_plot.plot(
                pen=pg.mkPen('#dc2626', width=2), name='边界距离'
            ),
        }
        self.distance_plot.addLegend(offset=(8, 8))
        self.angle_plot.addLegend(offset=(8, 8))
        self.boundary_plot.addLegend(offset=(8, 8))

        self.position_plot = pg.PlotWidget()
        self.position_plot.setBackground('#ffffff')
        self.position_plot.setTitle('二维定位轨迹', color='#111827', size='10pt')
        self.position_plot.setLabel('left', 'Y', units='m')
        self.position_plot.setLabel('bottom', 'X', units='m')
        self.position_plot.showGrid(x=True, y=True, alpha=0.2)
        self.position_plot.setAspectLocked(True)
        self.position_plot.setXRange(-2.5, 2.5)
        self.position_plot.setYRange(-0.35, 3.8)
        self._add_zone_circle(0.30, '#111827')
        self._add_zone_circle(1.20, '#dc2626')
        self._add_zone_circle(2.20, '#16a34a')
        self.position_plot.plot(
            [0.0], [0.0],
            pen=None, symbol='t1', symbolSize=15, symbolBrush='#111827',
        )
        self.trail_scatter = self.position_plot.plot(
            pen=pg.mkPen('#93c5fd', width=1), symbol='o',
            symbolSize=4, symbolBrush='#60a5fa',
        )
        self.current_scatter = self.position_plot.plot(
            pen=None, symbol='o', symbolSize=14,
            symbolBrush='#f59e0b', symbolPen='#92400e',
        )
        self.true_scatter = self.position_plot.plot(
            pen=None, symbol='+', symbolSize=18,
            symbolBrush='#16a34a', symbolPen=pg.mkPen('#16a34a', width=3),
        )
        splitter.addWidget(plots)
        splitter.addWidget(self.position_plot)
        splitter.setSizes((850, 520))
        layout.addWidget(splitter, 1)
        return tab

    def _add_zone_circle(self, radius: float, color: str) -> None:
        circle = QtWidgets.QGraphicsEllipseItem(-radius, -radius, radius * 2, radius * 2)
        circle.setPen(pg.mkPen(color, width=1, style=QtCore.Qt.PenStyle.DashLine))
        self.position_plot.addItem(circle)

    def _spin(
        self,
        minimum: float,
        maximum: float,
        value: float,
        decimals: int = 3,
    ) -> QtWidgets.QDoubleSpinBox:
        spin = QtWidgets.QDoubleSpinBox()
        spin.setRange(minimum, maximum)
        spin.setDecimals(decimals)
        spin.setValue(value)
        spin.setSingleStep(0.05)
        return spin

    def _build_capture_tab(self) -> QtWidgets.QWidget:
        tab = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(tab)
        layout.setContentsMargins(8, 8, 8, 8)
        form_box = QtWidgets.QGroupBox('真实测点')
        form = QtWidgets.QGridLayout(form_box)
        for column in (1, 3, 5, 7, 9, 11):
            form.setColumnStretch(column, 1)
        self.point_label = QtWidgets.QLineEdit('P1')
        self.true_x = self._spin(-10.0, 10.0, 0.0)
        self.true_y = self._spin(-1.0, 20.0, 1.3)
        self.anchor_height = self._spin(0.0, 300.0, 0.0, 1)
        self.tag_height = self._spin(0.0, 300.0, 0.0, 1)
        self.anchor_height.setSingleStep(0.5)
        self.tag_height.setSingleStep(0.5)
        self.sample_target = QtWidgets.QComboBox()
        self.sample_target.addItems(('100', '300', '500', '1000'))
        self.sample_target.setCurrentText('300')
        self.point_notes = QtWidgets.QLineEdit()
        self.point_notes.setPlaceholderText('姿态、遮挡和布局变化')
        first_row = (
            ('测点名', self.point_label), ('真实X / m', self.true_x),
            ('真实Y / m', self.true_y), ('样本数', self.sample_target),
        )
        for index, (label, widget) in enumerate(first_row):
            form.addWidget(QtWidgets.QLabel(label), 0, index * 2)
            form.addWidget(widget, 0, index * 2 + 1)
        form.addWidget(QtWidgets.QLabel('基站高度 / cm'), 1, 0)
        form.addWidget(self.anchor_height, 1, 1)
        form.addWidget(QtWidgets.QLabel('标签高度 / cm'), 1, 2)
        form.addWidget(self.tag_height, 1, 3)
        form.addWidget(QtWidgets.QLabel('备注'), 1, 4)
        form.addWidget(self.point_notes, 1, 5, 1, 7)

        self.capture_start = self._icon_button(
            '开始采集', QtWidgets.QStyle.StandardPixmap.SP_MediaPlay
        )
        self.capture_start.clicked.connect(self.start_capture)
        self.capture_stop = self._icon_button(
            '停止', QtWidgets.QStyle.StandardPixmap.SP_MediaStop
        )
        self.capture_stop.setEnabled(False)
        self.capture_stop.clicked.connect(self.stop_capture)
        self.fit_button = self._icon_button(
            '计算标定', QtWidgets.QStyle.StandardPixmap.SP_ComputerIcon
        )
        self.fit_button.clicked.connect(self.show_calibration_fit)
        self.export_button = self._icon_button(
            '导出汇总', QtWidgets.QStyle.StandardPixmap.SP_DialogSaveButton
        )
        self.export_button.clicked.connect(self.export_summary)
        form.addWidget(self.capture_start, 2, 0, 1, 2)
        form.addWidget(self.capture_stop, 2, 2, 1, 2)
        form.addWidget(self.fit_button, 2, 4, 1, 2)
        form.addWidget(self.export_button, 2, 6, 1, 2)
        self.capture_progress = QtWidgets.QProgressBar()
        self.capture_progress.setRange(0, 100)
        form.addWidget(self.capture_progress, 2, 8, 1, 4)
        layout.addWidget(form_box)

        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        self.summary_table = QtWidgets.QTableWidget(0, 9)
        self.summary_table.setHorizontalHeaderLabels(
            ('指标', '单位', '数量', '均值', '中位数', '标准差', 'MAD', '真实值', '中位误差')
        )
        self.summary_table.horizontalHeader().setSectionResizeMode(
            QtWidgets.QHeaderView.ResizeMode.Stretch
        )
        self.summary_table.setAlternatingRowColors(True)
        self.summary_table.setEditTriggers(
            QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers
        )
        self.capture_table = QtWidgets.QTableWidget(0, 7)
        self.capture_table.setHorizontalHeaderLabels(
            ('测点', '真实X/m', '真实Y/m', '基站高/cm', '标签高/cm', '有效帧', '备注')
        )
        self.capture_table.horizontalHeader().setSectionResizeMode(
            QtWidgets.QHeaderView.ResizeMode.Stretch
        )
        self.capture_table.setEditTriggers(
            QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers
        )
        self.capture_table.itemSelectionChanged.connect(self.show_selected_capture)
        splitter.addWidget(self.summary_table)
        splitter.addWidget(self.capture_table)
        splitter.setSizes((470, 200))
        layout.addWidget(splitter, 1)
        return tab

    def _build_log_tab(self) -> QtWidgets.QWidget:
        tab = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(tab)
        self.raw_log = QtWidgets.QPlainTextEdit()
        self.raw_log.setReadOnly(True)
        self.raw_log.document().setMaximumBlockCount(2500)
        self.raw_log.setFont(QtGui.QFontDatabase.systemFont(
            QtGui.QFontDatabase.SystemFont.FixedFont
        ))
        layout.addWidget(self.raw_log)
        return tab

    def _apply_style(self) -> None:
        self.setStyleSheet('''
            QMainWindow, QWidget { background: #f3f4f6; color: #111827; }
            QFrame#connectionBar, QFrame#metric {
                background: #ffffff; border: 1px solid #d1d5db; border-radius: 6px;
            }
            QLabel#metricTitle { color: #6b7280; font-size: 11px; }
            QLabel#metricValue { font-size: 15px; font-weight: 600; }
            QLabel#sourceState { color: #374151; font-weight: 600; }
            QPushButton {
                background: #ffffff; border: 1px solid #9ca3af;
                border-radius: 5px; padding: 5px 10px;
            }
            QPushButton:hover { background: #f9fafb; border-color: #4b5563; }
            QPushButton:checked { background: #dcfce7; border-color: #16a34a; }
            QPushButton:disabled { color: #9ca3af; background: #f3f4f6; }
            QComboBox, QLineEdit, QDoubleSpinBox {
                background: #ffffff; border: 1px solid #9ca3af;
                border-radius: 4px; padding: 5px;
            }
            QTabWidget::pane { border: 1px solid #d1d5db; background: #ffffff; }
            QTabBar::tab { padding: 8px 18px; background: #e5e7eb; }
            QTabBar::tab:selected {
                background: #ffffff; color: #166534; font-weight: 600;
            }
            QGroupBox {
                background: #ffffff; border: 1px solid #d1d5db;
                border-radius: 6px; margin-top: 10px; padding-top: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin; left: 10px; padding: 0 4px;
            }
            QTableWidget { background: #ffffff; alternate-background-color: #f9fafb; }
            QHeaderView::section {
                background: #e5e7eb; color: #111827; padding: 6px;
                border: 0; border-right: 1px solid #d1d5db;
            }
            QPlainTextEdit { background: #111827; color: #d1fae5; border: 0; }
            QProgressBar {
                text-align: center; background: #e5e7eb; border: 0; height: 20px;
            }
            QProgressBar::chunk { background: #16a34a; }
        ''')

    def refresh_ports(self) -> None:
        current = self.port_combo.currentData()
        self.port_combo.clear()
        preferred = ''
        for device, description in available_ports():
            self.port_combo.addItem(f'{device}  {description}', device)
            normalized = description.casefold()
            if 'ch343' in normalized or 'enhanced-serial' in normalized:
                preferred = device
        if current:
            index = self.port_combo.findData(current)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        elif preferred:
            index = self.port_combo.findData(preferred)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        if self.port_combo.count() == 0:
            self.port_combo.addItem('未发现串口', '')

    def connect_serial(self) -> None:
        port = self.port_combo.currentData()
        if not port:
            QtWidgets.QMessageBox.warning(self, '无法连接', '没有可用串口。')
            return
        self.stop_source()
        worker = SerialWorker(str(port), int(self.baud_combo.currentText()))
        worker.line_received.connect(self.handle_serial_line)
        self._bind_worker(worker)
        self._worker = worker
        worker.start()

    def _bind_worker(self, worker: SerialWorker | ReplayWorker) -> None:
        worker.source_opened.connect(self.source_opened)
        worker.source_closed.connect(self.source_closed)
        worker.source_error.connect(self.source_error)

    def source_opened(self, text: str) -> None:
        self.source_label.setText(text)
        self.statusBar().showMessage(f'数据源已连接：{text}')
        self.connect_button.setEnabled(False)
        self.disconnect_button.setEnabled(True)
        self.statusBar().showMessage(
            f'数据源已连接：{text}；等待ESP32输出C_KEY_DIAG_V2'
        )

    def source_closed(self) -> None:
        self.source_label.setText('未连接')
        self.connect_button.setEnabled(True)
        self.disconnect_button.setEnabled(False)
        self.statusBar().showMessage('数据源已断开')

    def source_error(self, text: str) -> None:
        self.raw_log.appendPlainText(f'[数据源错误] {text}')
        QtWidgets.QMessageBox.critical(self, '数据源错误', text)

    def stop_source(self) -> None:
        if self._demo_timer is not None:
            self._demo_timer.stop()
            self._demo_timer.deleteLater()
            self._demo_timer = None
            self._demo_generator = None
        if self._worker is not None:
            self._worker.stop()
            self._worker.wait(1500)
            self._worker = None
        self.source_closed()

    def start_demo(self) -> None:
        self.stop_source()
        self._demo_generator = DemoGenerator()
        self._demo_timer = QtCore.QTimer(self)
        self._demo_timer.timeout.connect(self._emit_demo_frame)
        self._demo_timer.start(100)
        self.source_opened('模拟数据：含左侧多径偏差')

    def _emit_demo_frame(self) -> None:
        if self._demo_generator is not None:
            self.handle_frame(self._demo_generator.next_frame())

    def open_replay(self) -> None:
        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, '打开诊断会话', str(self._output_dir()), 'CSV文件 (*.csv)'
        )
        if not path:
            return
        self.stop_source()
        worker = ReplayWorker(path)
        worker.frame_received.connect(self.handle_frame)
        self._bind_worker(worker)
        self._worker = worker
        worker.start()

    def handle_serial_line(self, line: str) -> None:
        self.raw_log.appendPlainText(line)
        try:
            frame = parse_diagnostic_line(line)
        except TelemetryParseError as exc:
            self._parse_errors += 1
            self.raw_log.appendPlainText(f'[解析错误] {exc}')
            return
        if frame is not None:
            self.handle_frame(frame)

    def handle_frame(self, frame: DiagnosticFrame) -> None:
        if self._last_sequence is not None:
            delta = (frame.sequence - self._last_sequence) & 0xFF
            if 1 < delta < 128:
                self._sequence_gaps += delta - 1
        self._last_sequence = frame.sequence
        self._time.append(frame.timestamp_ms / 1000.0)
        self._series['raw_distance'].append(frame.raw_distance_cm * 10.0)
        self._series['corrected_distance'].append(frame.corrected_distance_mm)
        self._series['filtered_distance'].append(
            frame.filtered_distance_mm if frame.measurement_ready else math.nan
        )
        self._series['raw_angle'].append(frame.raw_angle_deg)
        self._series['filtered_angle'].append(
            frame.filtered_angle_deg if frame.pose_valid else math.nan
        )
        self._series['boundary'].append(
            frame.boundary_m * 100.0 if frame.pose_valid else math.nan
        )
        if frame.pose_valid:
            self._trail_x.append(frame.x_m)
            self._trail_y.append(frame.y_m)
        self._update_metrics(frame)
        self._update_plots(frame)
        self._record_frame(frame)
        self._capture_frame(frame)

    def _update_metrics(self, frame: DiagnosticFrame) -> None:
        self._metric_values['state'].setText(frame.state)
        self._metric_values['ids'].setText(f'{frame.tag_id:02d} / {frame.accepted_id:02d}')
        self._metric_values['address'].setText(f'0x{frame.tag_address:04X}')
        self._metric_values['raw'].setText(
            f'{frame.raw_distance_cm} cm / {frame.raw_angle_deg:+.1f}°'
        )
        self._metric_values['corrected'].setText(
            f'{frame.corrected_distance_mm:.0f} mm'
        )
        self._metric_values['filtered'].setText(
            f'{frame.filtered_distance_mm:.0f} mm / '
            f'{frame.filtered_angle_deg:+.1f}°'
            if frame.measurement_ready else '--'
        )
        self._metric_values['pose'].setText(
            f'{frame.x_m:+.3f} / {frame.y_m:+.3f} m' if frame.pose_valid else '--'
        )
        self._metric_values['boundary'].setText(
            f'{frame.boundary_m:.3f} m' if frame.pose_valid else '--'
        )
        self._metric_values['quality'].setText(
            f'OK {frame.accepted_frames} / BAD {frame.rejected_frames} / '
            f'GAP {self._sequence_gaps}'
        )
        state_colors = {
            'UNLOCKED': '#15803d', 'WELCOME': '#1d4ed8',
            'SENSING': '#374151', 'OUT_OF_ANGLE': '#b45309',
            'INVALID_ID': '#b91c1c', 'NO_KEY': '#b91c1c', 'FAULT': '#b91c1c',
        }
        color = state_colors.get(frame.state, '#111827')
        self._metric_values['state'].setStyleSheet(f'color: {color};')

    def _update_plots(self, frame: DiagnosticFrame) -> None:
        times = list(self._time)
        for name, curve in self.curves.items():
            curve.setData(times, list(self._series[name]))
        self.trail_scatter.setData(list(self._trail_x), list(self._trail_y))
        if frame.pose_valid:
            self.current_scatter.setData([frame.x_m], [frame.y_m])

    def current_metadata(self) -> PointMetadata:
        return PointMetadata(
            label=self.point_label.text().strip(),
            true_x_m=self.true_x.value(),
            true_y_m=self.true_y.value(),
            anchor_height_cm=self.anchor_height.value(),
            tag_height_cm=self.tag_height.value(),
            notes=self.point_notes.text().strip(),
        )

    def _output_dir(self) -> Path:
        path = Path(__file__).resolve().parents[1] / 'output'
        path.mkdir(exist_ok=True)
        return path

    def toggle_recording(self, checked: bool) -> None:
        if checked:
            default = self._output_dir() / datetime.now().strftime(
                'session_%Y%m%d_%H%M%S.csv'
            )
            path, _ = QtWidgets.QFileDialog.getSaveFileName(
                self, '保存诊断会话', str(default), 'CSV文件 (*.csv)'
            )
            if not path:
                self.record_button.setChecked(False)
                return
            self._recorder = SessionRecorder(path)
            self._recorder.start()
            self.record_button.setText('停止记录')
            self.statusBar().showMessage(f'正在记录：{path}')
        else:
            if self._recorder is not None:
                self._recorder.close()
                self._recorder = None
            self.record_button.setText('记录CSV')

    def _record_frame(self, frame: DiagnosticFrame) -> None:
        if self._recorder is None:
            return
        try:
            self._recorder.record(frame, self.current_metadata())
        except OSError as exc:
            self.record_button.setChecked(False)
            self.toggle_recording(False)
            self.source_error(f'写入CSV失败：{exc}')

    def start_capture(self) -> None:
        if self._active_capture is not None:
            return
        metadata = self.current_metadata()
        label = metadata.label or f'P{len(self._captures) + 1}'
        self.point_label.setText(label)
        self._active_capture = CapturePoint(
            label=label,
            true_x_m=metadata.true_x_m,
            true_y_m=metadata.true_y_m,
            anchor_height_cm=metadata.anchor_height_cm,
            tag_height_cm=metadata.tag_height_cm,
            notes=metadata.notes,
        )
        self._capture_target = int(self.sample_target.currentText())
        self.capture_progress.setRange(0, self._capture_target)
        self.capture_progress.setValue(0)
        self.capture_start.setEnabled(False)
        self.capture_stop.setEnabled(True)
        self.true_scatter.setData([metadata.true_x_m], [metadata.true_y_m])
        self.statusBar().showMessage(f'正在采集{label}：0/{self._capture_target}')

    def _capture_frame(self, frame: DiagnosticFrame) -> None:
        capture = self._active_capture
        if capture is None or not frame.link_ok or not frame.measurement_ready:
            return
        capture.frames.append(frame)
        count = len(capture.frames)
        self.capture_progress.setValue(count)
        self.statusBar().showMessage(
            f'正在采集{capture.label}：{count}/{self._capture_target}'
        )
        if count >= self._capture_target:
            self.stop_capture()

    def stop_capture(self) -> None:
        capture = self._active_capture
        self._active_capture = None
        self.capture_start.setEnabled(True)
        self.capture_stop.setEnabled(False)
        if capture is None or not capture.frames:
            return
        self._captures.append(capture)
        self._append_capture_row(capture)
        self._show_capture_summary(capture)
        self.statusBar().showMessage(
            f'{capture.label}采集完成：{len(capture.frames)}个有效帧'
        )
        self.point_label.setText(f'P{len(self._captures) + 1}')

    def _append_capture_row(self, capture: CapturePoint) -> None:
        row = self.capture_table.rowCount()
        self.capture_table.insertRow(row)
        values = (
            capture.label, f'{capture.true_x_m:.3f}', f'{capture.true_y_m:.3f}',
            f'{capture.anchor_height_cm:.1f}', f'{capture.tag_height_cm:.1f}',
            str(len(capture.frames)), capture.notes,
        )
        for column, value in enumerate(values):
            self.capture_table.setItem(row, column, QtWidgets.QTableWidgetItem(value))
        self.capture_table.selectRow(row)

    def show_selected_capture(self) -> None:
        rows = self.capture_table.selectionModel().selectedRows()
        if not rows:
            return
        index = rows[0].row()
        if 0 <= index < len(self._captures):
            capture = self._captures[index]
            self._show_capture_summary(capture)
            self.true_scatter.setData([capture.true_x_m], [capture.true_y_m])

    def _format_number(self, value: float | None, unit: str) -> str:
        if value is None:
            return '--'
        decimals = 3 if unit == 'm' else 2
        return f'{value:.{decimals}f}'

    def _show_capture_summary(self, capture: CapturePoint) -> None:
        rows = summarize_capture(capture)
        self.summary_table.setRowCount(len(rows))
        for row_index, metric in enumerate(rows):
            stats = metric.stats
            values = (
                metric.name, metric.unit, str(stats.count) if stats else '0',
                self._format_number(stats.mean if stats else None, metric.unit),
                self._format_number(stats.median if stats else None, metric.unit),
                self._format_number(stats.stdev if stats else None, metric.unit),
                self._format_number(stats.mad if stats else None, metric.unit),
                self._format_number(metric.reference, metric.unit),
                self._format_number(metric.median_error, metric.unit),
            )
            for column, value in enumerate(values):
                item = QtWidgets.QTableWidgetItem(value)
                if column == 8 and metric.median_error is not None:
                    limit = 10.0 if metric.unit == 'deg' else (
                        0.1 if metric.unit == 'm' else 100.0
                    )
                    if abs(metric.median_error) > limit:
                        item.setForeground(QtGui.QColor('#b91c1c'))
                self.summary_table.setItem(row_index, column, item)

    def show_calibration_fit(self) -> None:
        try:
            fit = fit_distance_calibration(self._captures)
        except ValueError as exc:
            QtWidgets.QMessageBox.warning(self, '无法标定', str(exc))
            return
        text = (
            'BU04距离线性校正建议（不会自动写入固件）：\n\n'
            f'scale={fit.scale:.6f}, offset={fit.offset_mm:+.1f} mm\n'
            f'PPM={round(fit.scale * 1_000_000)}, '
            f'MAE={fit.mae_mm:.1f} mm, MAX={fit.maximum_error_mm:.1f} mm\n\n'
            '角度零偏请查看“滤波方位角”的中位误差，'
            '在多个角度点确认后再写入固件。'
        )
        QtWidgets.QMessageBox.information(self, '线性标定结果', text)

    def export_summary(self) -> None:
        if not self._captures:
            QtWidgets.QMessageBox.warning(self, '无法导出', '尚未完成任何定点采集。')
            return
        default = self._output_dir() / datetime.now().strftime(
            'summary_%Y%m%d_%H%M%S.csv'
        )
        path, _ = QtWidgets.QFileDialog.getSaveFileName(
            self, '导出定点汇总', str(default), 'CSV文件 (*.csv)'
        )
        if not path:
            return
        with Path(path).open('w', newline='', encoding='utf-8-sig') as file:
            writer = csv.writer(file)
            writer.writerow((
                'point', 'true_x_m', 'true_y_m',
                'anchor_height_cm', 'tag_height_cm', 'metric', 'unit',
                'count', 'mean', 'median', 'stdev', 'mad',
                'reference', 'median_error',
            ))
            for capture in self._captures:
                for metric in summarize_capture(capture):
                    stats = metric.stats
                    writer.writerow((
                        capture.label, capture.true_x_m, capture.true_y_m,
                        capture.anchor_height_cm, capture.tag_height_cm,
                        metric.name, metric.unit,
                        stats.count if stats else 0,
                        stats.mean if stats else '',
                        stats.median if stats else '',
                        stats.stdev if stats else '',
                        stats.mad if stats else '',
                        metric.reference if metric.reference is not None else '',
                        metric.median_error if metric.median_error is not None else '',
                    ))
        self.statusBar().showMessage(f'汇总已导出：{path}')

    def save_screenshot(self, path: str | Path) -> bool:
        return self.grab().save(str(path))

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self.stop_source()
        if self._recorder is not None:
            self._recorder.close()
            self._recorder = None
        event.accept()
