import math
import random
from collections import deque
from typing import Dict

from PyQt5 import QtCore, QtGui, QtWidgets

from ble_work import (
    DEFAULT_DEVICE_NAME,
    DEFAULT_RX_UUID,
    DEFAULT_SERVICE_UUID,
    DEFAULT_TX_UUID,
    BleWorker,
)


class WaveformWidget(QtWidgets.QWidget):
    CHANNEL_COLORS = ["#00e5ff", "#ffb300", "#ff4f8b", "#47d147", "#bf5fff"]
    BASELINES = [0.13, 0.30, 0.46, 0.62, 0.80]

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumHeight(300)
        self._point_count = 340
        self._history = [
            deque([0.0] * self._point_count, maxlen=self._point_count)
            for _ in range(5)
        ]
        self._enabled = [True, True, True, True, True]
        self._paused = False
        self._t = 0.0

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(45)

    def set_channel_enabled(self, channel_index: int, enabled: bool) -> None:
        if 0 <= channel_index < len(self._enabled):
            self._enabled[channel_index] = enabled
            self.update()

    def set_paused(self, paused: bool) -> None:
        self._paused = paused

    def clear_data(self) -> None:
        self._history = [
            deque([0.0] * self._point_count, maxlen=self._point_count)
            for _ in range(5)
        ]
        self.update()

    def _tick(self) -> None:
        if self._paused:
            return

        self._t += 0.08
        for idx, data_buf in enumerate(self._history):
            phase = self._t * (0.85 + idx * 0.18) + idx
            if idx == 2:
                signal = 0.58 if math.sin(phase * 2.4) >= 0 else -0.58
            else:
                signal = math.sin(phase) * 0.48 + math.sin(phase * 0.33) * 0.15
            noise = (random.random() - 0.5) * 0.07
            data_buf.append(signal + noise)

        self.update()

    def paintEvent(self, _event: QtGui.QPaintEvent) -> None:
        painter = QtGui.QPainter(self)
        painter.setRenderHint(QtGui.QPainter.Antialiasing)

        outer_rect = self.rect().adjusted(1, 1, -1, -1)
        painter.fillRect(outer_rect, QtGui.QColor("#040a13"))

        graph_rect = outer_rect.adjusted(12, 12, -12, -12)

        grid_pen = QtGui.QPen(QtGui.QColor("#12263a"), 1)
        painter.setPen(grid_pen)

        v_lines = 13
        for i in range(v_lines + 1):
            x = graph_rect.left() + int((i / v_lines) * graph_rect.width())
            painter.drawLine(x, graph_rect.top(), x, graph_rect.bottom())

        h_lines = 10
        for i in range(h_lines + 1):
            y = graph_rect.top() + int((i / h_lines) * graph_rect.height())
            painter.drawLine(graph_rect.left(), y, graph_rect.right(), y)

        axis_pen = QtGui.QPen(QtGui.QColor("#1d3852"), 1)
        painter.setPen(axis_pen)
        for baseline in self.BASELINES:
            y = graph_rect.top() + int(baseline * graph_rect.height())
            painter.drawLine(graph_rect.left(), y, graph_rect.right(), y)

        for idx, data_buf in enumerate(self._history):
            if not self._enabled[idx]:
                continue

            values = list(data_buf)
            if len(values) < 2:
                continue

            path = QtGui.QPainterPath()
            center_y = graph_rect.top() + self.BASELINES[idx] * graph_rect.height()
            scale = graph_rect.height() * 0.11

            for j, value in enumerate(values):
                x = graph_rect.left() + (j / (len(values) - 1)) * graph_rect.width()
                y = center_y - value * scale
                if j == 0:
                    path.moveTo(x, y)
                else:
                    path.lineTo(x, y)

            line_pen = QtGui.QPen(QtGui.QColor(self.CHANNEL_COLORS[idx]), 1.7)
            painter.setPen(line_pen)
            painter.drawPath(path)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("ESP32-S3 Control Center")
        self.resize(1520, 900)

        self.worker = BleWorker()

        self.header_ble_dot: QtWidgets.QLabel
        self.header_ble_status: QtWidgets.QLabel
        self.header_device_value: QtWidgets.QLabel
        self.header_rssi_value: QtWidgets.QLabel
        self.header_uptime_value: QtWidgets.QLabel
        self.header_clock_value: QtWidgets.QLabel

        self.name_filter_edit: QtWidgets.QLineEdit
        self.scan_button: QtWidgets.QPushButton
        self.connect_button: QtWidgets.QPushButton
        self.disconnect_button: QtWidgets.QPushButton
        self.conn_status_label: QtWidgets.QLabel
        self.device_list: QtWidgets.QListWidget
        self.service_uuid_edit: QtWidgets.QLineEdit
        self.rx_uuid_edit: QtWidgets.QLineEdit
        self.tx_uuid_edit: QtWidgets.QLineEdit

        self.time_div_combo: QtWidgets.QComboBox
        self.autoscale_button: QtWidgets.QPushButton
        self.ch_checks: list[QtWidgets.QCheckBox]
        self.pause_wave_button: QtWidgets.QPushButton
        self.clear_wave_button: QtWidgets.QPushButton
        self.wave_widget: WaveformWidget

        self.log_view: QtWidgets.QPlainTextEdit
        self.rx_view: QtWidgets.QPlainTextEdit
        self.tx_edit: QtWidgets.QLineEdit
        self.send_button: QtWidgets.QPushButton
        self.ping_button: QtWidgets.QPushButton

        self._telemetry_labels: Dict[str, QtWidgets.QLabel] = {}
        self._knob_value_labels: Dict[str, QtWidgets.QLabel] = {}
        self._knob_dials: Dict[str, QtWidgets.QDial] = {}
        self._wave_paused = False
        self._uptime_secs = 0

        self._build_ui()
        self._apply_theme()
        self._bind_signals()
        self._start_timers()

    def _build_ui(self) -> None:
        root = QtWidgets.QWidget(self)
        root.setObjectName("Root")
        self.setCentralWidget(root)

        root_layout = QtWidgets.QVBoxLayout(root)
        root_layout.setContentsMargins(14, 10, 14, 12)
        root_layout.setSpacing(10)

        header_bar = self._build_header_bar()
        root_layout.addWidget(header_bar)

        body_layout = QtWidgets.QHBoxLayout()
        body_layout.setSpacing(10)
        root_layout.addLayout(body_layout, 1)

        left_panel = self._build_left_panel()
        left_panel.setFixedWidth(260)
        body_layout.addWidget(left_panel)

        center_panel = self._build_center_panel()
        body_layout.addWidget(center_panel, 1)

        right_panel = self._build_right_panel()
        right_panel.setFixedWidth(270)
        body_layout.addWidget(right_panel)

    def _build_header_bar(self) -> QtWidgets.QFrame:
        header = QtWidgets.QFrame()
        header.setObjectName("HeaderBar")
        layout = QtWidgets.QHBoxLayout(header)
        layout.setContentsMargins(14, 10, 14, 10)
        layout.setSpacing(18)

        title = QtWidgets.QLabel("ESP32-S3 CONTROL CENTER")
        title.setObjectName("HeaderTitle")
        layout.addWidget(title)

        self.header_ble_dot = QtWidgets.QLabel("o")
        self.header_ble_dot.setObjectName("StatusDot")
        layout.addWidget(self.header_ble_dot)

        self.header_ble_status = QtWidgets.QLabel("BLE Disconnected")
        self.header_ble_status.setObjectName("HeaderValue")
        layout.addWidget(self.header_ble_status)

        sep_1 = QtWidgets.QFrame()
        sep_1.setFrameShape(QtWidgets.QFrame.VLine)
        sep_1.setObjectName("HeaderSep")
        layout.addWidget(sep_1)

        layout.addWidget(QtWidgets.QLabel("Device:"))
        self.header_device_value = QtWidgets.QLabel("N/A")
        self.header_device_value.setObjectName("HeaderValueStrong")
        layout.addWidget(self.header_device_value)

        layout.addWidget(QtWidgets.QLabel("RSSI:"))
        self.header_rssi_value = QtWidgets.QLabel("-- dBm")
        self.header_rssi_value.setObjectName("HeaderValueStrong")
        layout.addWidget(self.header_rssi_value)

        layout.addWidget(QtWidgets.QLabel("Uptime:"))
        self.header_uptime_value = QtWidgets.QLabel("00:00:00")
        self.header_uptime_value.setObjectName("HeaderValueStrong")
        layout.addWidget(self.header_uptime_value)

        layout.addStretch(1)

        self.header_clock_value = QtWidgets.QLabel("--:--:--")
        self.header_clock_value.setObjectName("HeaderClock")
        layout.addWidget(self.header_clock_value)
        return header

    def _build_left_panel(self) -> QtWidgets.QFrame:
        panel = QtWidgets.QFrame()
        panel.setObjectName("Panel")
        layout = QtWidgets.QVBoxLayout(panel)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        title = QtWidgets.QLabel("DEVICE PANEL")
        title.setObjectName("SectionTitle")
        layout.addWidget(title)

        self.name_filter_edit = QtWidgets.QLineEdit(DEFAULT_DEVICE_NAME)
        self.name_filter_edit.setPlaceholderText("Device name filter")
        layout.addWidget(self.name_filter_edit)

        self.scan_button = QtWidgets.QPushButton("SCAN")
        self.scan_button.setProperty("variant", "accent")
        layout.addWidget(self.scan_button)

        self.connect_button = QtWidgets.QPushButton("CONNECT")
        self.connect_button.setProperty("variant", "primary")
        layout.addWidget(self.connect_button)

        self.disconnect_button = QtWidgets.QPushButton("DISCONNECT")
        self.disconnect_button.setProperty("variant", "danger")
        self.disconnect_button.setEnabled(False)
        layout.addWidget(self.disconnect_button)

        self.conn_status_label = QtWidgets.QLabel("Status: Disconnected")
        self.conn_status_label.setObjectName("MutedLabel")
        layout.addWidget(self.conn_status_label)

        self.device_list = QtWidgets.QListWidget()
        self.device_list.setObjectName("DeviceList")
        self.device_list.setMinimumHeight(150)
        layout.addWidget(self.device_list)

        uuid_title = QtWidgets.QLabel("BLE CONFIG")
        uuid_title.setObjectName("SectionTitle")
        layout.addWidget(uuid_title)

        self.service_uuid_edit = QtWidgets.QLineEdit(DEFAULT_SERVICE_UUID)
        self.service_uuid_edit.setPlaceholderText("Service UUID")
        layout.addWidget(self.service_uuid_edit)

        self.rx_uuid_edit = QtWidgets.QLineEdit(DEFAULT_RX_UUID)
        self.rx_uuid_edit.setPlaceholderText("RX UUID")
        layout.addWidget(self.rx_uuid_edit)

        self.tx_uuid_edit = QtWidgets.QLineEdit(DEFAULT_TX_UUID)
        self.tx_uuid_edit.setPlaceholderText("TX UUID")
        layout.addWidget(self.tx_uuid_edit)

        actions_title = QtWidgets.QLabel("QUICK ACTIONS")
        actions_title.setObjectName("SectionTitle")
        layout.addWidget(actions_title)

        for cmd in [
            "read_uid",
            "printer_test",
            "lcd_test",
            "camera_preview",
            "reboot",
        ]:
            btn = QtWidgets.QPushButton(cmd.upper())
            btn.clicked.connect(lambda _checked=False, text=cmd: self.worker.send_text(text))
            layout.addWidget(btn)

        layout.addStretch(1)
        return panel

    def _build_center_panel(self) -> QtWidgets.QWidget:
        center = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(center)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        wave_card = QtWidgets.QFrame()
        wave_card.setObjectName("Card")
        wave_layout = QtWidgets.QVBoxLayout(wave_card)
        wave_layout.setContentsMargins(12, 10, 12, 12)
        wave_layout.setSpacing(8)

        wave_head = QtWidgets.QHBoxLayout()
        wave_head.addWidget(self._title_label("REAL TIME WAVEFORM"))

        self.time_div_combo = QtWidgets.QComboBox()
        self.time_div_combo.addItems(["500 ms", "1 s", "2 s"])
        wave_head.addWidget(QtWidgets.QLabel("Time/Div"))
        wave_head.addWidget(self.time_div_combo)

        self.autoscale_button = QtWidgets.QPushButton("AUTO SCALE")
        self.autoscale_button.setCheckable(True)
        self.autoscale_button.setChecked(True)
        wave_head.addWidget(self.autoscale_button)

        self.ch_checks = []
        for i in range(5):
            chk = QtWidgets.QCheckBox(f"CH{i + 1}")
            chk.setChecked(True)
            self.ch_checks.append(chk)
            wave_head.addWidget(chk)

        wave_head.addStretch(1)

        self.pause_wave_button = QtWidgets.QPushButton("PAUSE")
        wave_head.addWidget(self.pause_wave_button)

        self.clear_wave_button = QtWidgets.QPushButton("CLEAR")
        wave_head.addWidget(self.clear_wave_button)

        wave_layout.addLayout(wave_head)

        self.wave_widget = WaveformWidget()
        wave_layout.addWidget(self.wave_widget, 1)

        layout.addWidget(wave_card, 5)

        knob_card = QtWidgets.QFrame()
        knob_card.setObjectName("Card")
        knob_layout = QtWidgets.QVBoxLayout(knob_card)
        knob_layout.setContentsMargins(12, 10, 12, 10)
        knob_layout.setSpacing(8)
        knob_layout.addWidget(self._title_label("CONTROL KNOBS"))

        knob_grid = QtWidgets.QGridLayout()
        knob_grid.setHorizontalSpacing(8)
        knob_grid.setVerticalSpacing(8)

        knob_defs = [
            ("BAT V", "battery", 75),
            ("TEMP", "temp", 55),
            ("RF POWER", "rf_power", 68),
            ("LCD", "lcd", 80),
            ("SPK", "spk", 60),
            ("PWM1", "pwm1", 45),
            ("PWM2", "pwm2", 60),
            ("PRINTER", "printer", 12),
            ("CAMERA", "camera", 72),
            ("SYSTEM", "system", 40),
        ]

        for idx, (title, knob_key, dial_value) in enumerate(knob_defs):
            row = idx // 5
            col = idx % 5
            knob_grid.addWidget(
                self._open_knob_card(title, knob_key, dial_value), row, col
            )

        knob_layout.addLayout(knob_grid)
        layout.addWidget(knob_card, 3)

        bottom_row = QtWidgets.QHBoxLayout()
        bottom_row.setSpacing(10)

        log_card = QtWidgets.QFrame()
        log_card.setObjectName("Card")
        log_layout = QtWidgets.QVBoxLayout(log_card)
        log_layout.setContentsMargins(12, 10, 12, 10)
        log_layout.setSpacing(8)

        log_head = QtWidgets.QHBoxLayout()
        log_head.addWidget(self._title_label("LOG OUTPUT"))
        log_head.addStretch(1)
        clear_log_button = QtWidgets.QPushButton("CLEAR")
        clear_log_button.clicked.connect(self._clear_log_views)
        log_head.addWidget(clear_log_button)
        log_layout.addLayout(log_head)

        self.log_view = QtWidgets.QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setObjectName("LogView")
        self.log_view.setMaximumBlockCount(2500)
        self.log_view.setLineWrapMode(QtWidgets.QPlainTextEdit.NoWrap)
        self.log_view.setFont(QtGui.QFont("Consolas", 10))
        log_layout.addWidget(self.log_view, 1)

        self.rx_view = QtWidgets.QPlainTextEdit()
        self.rx_view.setReadOnly(True)
        self.rx_view.setVisible(False)

        bottom_row.addWidget(log_card, 2)

        command_card = QtWidgets.QFrame()
        command_card.setObjectName("Card")
        command_layout = QtWidgets.QVBoxLayout(command_card)
        command_layout.setContentsMargins(12, 10, 12, 10)
        command_layout.setSpacing(8)
        command_layout.addWidget(self._title_label("COMMAND CONSOLE"))

        tx_row = QtWidgets.QHBoxLayout()
        self.tx_edit = QtWidgets.QLineEdit()
        self.tx_edit.setPlaceholderText("Enter command, e.g. PING or get_status")
        tx_row.addWidget(self.tx_edit, 1)

        self.send_button = QtWidgets.QPushButton("SEND")
        self.send_button.setEnabled(False)
        tx_row.addWidget(self.send_button)

        self.ping_button = QtWidgets.QPushButton("PING")
        self.ping_button.setEnabled(False)
        tx_row.addWidget(self.ping_button)
        command_layout.addLayout(tx_row)

        cmd_grid = QtWidgets.QGridLayout()
        cmd_grid.setHorizontalSpacing(6)
        cmd_grid.setVerticalSpacing(6)
        cmd_defs = [
            "read_uid",
            "read_battery",
            "read_temp",
            "printer_test",
            "lcd_test",
            "reboot",
            "get_status",
            "ota_start",
        ]
        for idx, cmd in enumerate(cmd_defs):
            btn = QtWidgets.QPushButton(cmd)
            btn.setObjectName("MiniActionButton")
            btn.clicked.connect(lambda _checked=False, text=cmd: self.worker.send_text(text))
            cmd_grid.addWidget(btn, idx // 4, idx % 4)

        command_layout.addLayout(cmd_grid)
        command_layout.addStretch(1)

        bottom_row.addWidget(command_card, 1)
        layout.addLayout(bottom_row, 3)

        return center

    def _build_right_panel(self) -> QtWidgets.QFrame:
        panel = QtWidgets.QFrame()
        panel.setObjectName("Panel")
        layout = QtWidgets.QVBoxLayout(panel)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        layout.addWidget(self._title_label("TELEMETRY"))

        self._add_telemetry_card(layout, "BATTERY", "3.98 V", "battery")
        self._add_telemetry_card(layout, "RSSI", "-- dBm", "rssi")
        self._add_telemetry_card(layout, "TEMP", "32.6 C", "temp")
        self._add_telemetry_card(layout, "RFID (UID)", "04 AA BB CC DD 18", "rfid")
        self._add_telemetry_card(layout, "PRINTER", "READY", "printer")
        self._add_telemetry_card(layout, "CAMERA", "FPS 27.3", "camera")

        layout.addStretch(1)
        return panel

    def _title_label(self, text: str) -> QtWidgets.QLabel:
        lb = QtWidgets.QLabel(text)
        lb.setObjectName("SectionTitle")
        return lb

    def _open_knob_card(
        self,
        title: str,
        knob_key: str,
        dial_value: int,
    ) -> QtWidgets.QFrame:
        card = QtWidgets.QFrame()
        card.setObjectName("KnobCard")
        layout = QtWidgets.QVBoxLayout(card)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(4)

        title_lb = QtWidgets.QLabel(title)
        title_lb.setAlignment(QtCore.Qt.AlignCenter)
        title_lb.setObjectName("KnobTitle")
        layout.addWidget(title_lb)

        dial = QtWidgets.QDial()
        dial.setRange(0, 100)
        dial.setValue(dial_value)
        dial.setNotchesVisible(True)
        dial.setWrapping(False)
        dial.setCursor(QtCore.Qt.PointingHandCursor)
        dial.setObjectName("ControlDial")
        dial.valueChanged.connect(
            lambda value, key=knob_key: self._on_knob_changed(key, value)
        )
        layout.addWidget(dial, 1)

        value_lb = QtWidgets.QLabel(self._format_knob_value(knob_key, dial_value))
        value_lb.setAlignment(QtCore.Qt.AlignCenter)
        value_lb.setObjectName("KnobValue")
        layout.addWidget(value_lb)
        self._knob_value_labels[knob_key] = value_lb
        self._knob_dials[knob_key] = dial
        return card

    def _format_knob_value(self, knob_key: str, value: int) -> str:
        if knob_key == "battery":
            return f"{3.2 + value * 0.0104:.2f} V"
        if knob_key == "temp":
            return f"{20.0 + value * 0.229:.1f} C"
        if knob_key == "rf_power":
            return f"{value * 0.3235:.1f} dBm"
        if knob_key in {"lcd", "spk", "pwm1", "pwm2"}:
            return f"{value} %"
        if knob_key == "printer":
            gray = max(0, min(10, round(value * 5 / 12)))
            return f"{gray} Gray"
        if knob_key == "camera":
            return f"{round(value * 120 / 72)} Exp"
        if knob_key == "system":
            return f"{round(value * 2.5)} Hz"
        return str(value)

    def _on_knob_changed(self, knob_key: str, value: int) -> None:
        text = self._format_knob_value(knob_key, value)
        label = self._knob_value_labels.get(knob_key)
        if label is not None:
            label.setText(text)

        if knob_key == "battery" and "battery" in self._telemetry_labels:
            self._telemetry_labels["battery"].setText(text)
        elif knob_key == "temp" and "temp" in self._telemetry_labels:
            self._telemetry_labels["temp"].setText(text)
        elif knob_key == "camera" and "camera" in self._telemetry_labels:
            self._telemetry_labels["camera"].setText(f"FPS {value / 3:.1f}")

    def _add_telemetry_card(
        self,
        parent_layout: QtWidgets.QVBoxLayout,
        title: str,
        value: str,
        key: str,
    ) -> None:
        card = QtWidgets.QFrame()
        card.setObjectName("TelemetryCard")
        layout = QtWidgets.QVBoxLayout(card)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(2)

        title_lb = QtWidgets.QLabel(title)
        title_lb.setObjectName("TelemetryTitle")
        layout.addWidget(title_lb)

        value_lb = QtWidgets.QLabel(value)
        value_lb.setObjectName("TelemetryValue")
        layout.addWidget(value_lb)

        self._telemetry_labels[key] = value_lb
        parent_layout.addWidget(card)

    def _apply_theme(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow {
                background-color: #050b14;
                color: #a6bfd7;
            }
            QWidget {
                font-family: Bahnschrift;
                font-size: 10pt;
                color: #a6bfd7;
            }
            #Root {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 #040914, stop:0.55 #071225, stop:1 #03070f);
            }
            #HeaderBar {
                border: 1px solid #113353;
                border-radius: 10px;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #091528, stop:1 #071120);
            }
            #HeaderTitle {
                color: #44e4ff;
                font-size: 13pt;
                font-weight: 700;
                letter-spacing: 1px;
            }
            #StatusDot {
                color: #ff5b5b;
                font-size: 14pt;
                font-weight: 700;
                min-width: 14px;
            }
            #HeaderValue {
                color: #9ab1c9;
                font-weight: 600;
            }
            #HeaderValueStrong {
                color: #45e4ff;
                font-weight: 700;
            }
            #HeaderClock {
                color: #c5d6e9;
                font-size: 14pt;
                font-weight: 600;
            }
            #HeaderSep {
                background-color: #13314a;
                max-width: 1px;
                min-width: 1px;
            }
            #Panel, #Card {
                border: 1px solid #113353;
                border-radius: 10px;
                background-color: #071223;
            }
            #SectionTitle {
                color: #5ddfff;
                font-size: 10pt;
                font-weight: 700;
                letter-spacing: 1px;
            }
            #MutedLabel {
                color: #7f95ab;
            }
            QPushButton {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #0a1729;
                color: #96bad7;
                min-height: 28px;
                padding: 2px 10px;
            }
            QPushButton:hover {
                border-color: #2f86bc;
                background-color: #0d1f34;
            }
            QPushButton:pressed {
                background-color: #122a44;
            }
            QPushButton:disabled {
                color: #4e6478;
                border-color: #1c2b3a;
                background-color: #070f1b;
            }
            QPushButton[variant="accent"] {
                border-color: #2c8ec3;
                color: #58e9ff;
                background-color: #0f2f47;
            }
            QPushButton[variant="primary"] {
                border-color: #29b7bf;
                color: #7ef8ff;
                background-color: #0f3440;
            }
            QPushButton[variant="danger"] {
                border-color: #a14560;
                color: #ffa8bf;
                background-color: #29111d;
            }
            QLineEdit, QComboBox {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #061020;
                color: #b6d4ef;
                padding: 4px 8px;
                min-height: 24px;
            }
            QLineEdit:focus, QComboBox:focus {
                border-color: #41caf4;
            }
            QListWidget {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #061020;
                color: #b6d4ef;
                padding: 4px;
            }
            QListWidget::item {
                padding: 4px 6px;
            }
            QListWidget::item:selected {
                background-color: #143250;
                border: 1px solid #2e7fb4;
            }
            QCheckBox {
                spacing: 5px;
            }
            QCheckBox::indicator {
                width: 13px;
                height: 13px;
                border: 1px solid #2e6b96;
                border-radius: 3px;
                background-color: #05111f;
            }
            QCheckBox::indicator:checked {
                background-color: #0ecae8;
                border-color: #40e5ff;
            }
            #KnobCard {
                border: 1px solid #173955;
                border-radius: 8px;
                background-color: #091425;
            }
            #KnobTitle {
                color: #66dfff;
                font-weight: 700;
            }
            #KnobValue {
                color: #cbe9ff;
                font-weight: 600;
            }
            QDial#ControlDial {
                background-color: #061221;
            }
            #TelemetryCard {
                border: 1px solid #1a486b;
                border-radius: 8px;
                background-color: #09192d;
            }
            #TelemetryTitle {
                color: #76dfff;
                font-size: 9pt;
                text-transform: uppercase;
            }
            #TelemetryValue {
                color: #39e3ff;
                font-size: 15pt;
                font-weight: 700;
            }
            #LogView {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #030a14;
                color: #69ff9a;
                selection-background-color: #1a4f74;
            }
            #MiniActionButton {
                min-height: 24px;
                font-size: 9pt;
            }
            """
        )

    def _bind_signals(self) -> None:
        self.scan_button.clicked.connect(self._on_scan_clicked)
        self.connect_button.clicked.connect(self._on_connect_clicked)
        self.disconnect_button.clicked.connect(self.worker.disconnect_device)

        self.send_button.clicked.connect(self._on_send_clicked)
        self.ping_button.clicked.connect(lambda: self.worker.send_text("PING"))
        self.tx_edit.returnPressed.connect(self._on_send_clicked)

        self.pause_wave_button.clicked.connect(self._on_toggle_wave_pause)
        self.clear_wave_button.clicked.connect(self.wave_widget.clear_data)

        for idx, checkbox in enumerate(self.ch_checks):
            checkbox.toggled.connect(
                lambda checked, channel=idx: self.wave_widget.set_channel_enabled(
                    channel, checked
                )
            )

        self.worker.log_signal.connect(self._append_log)
        self.worker.scan_item_signal.connect(self._on_scan_item)
        self.worker.scan_done_signal.connect(self._on_scan_done)
        self.worker.connected_signal.connect(self._on_connected_changed)
        self.worker.rx_signal.connect(self._append_rx)

    def _start_timers(self) -> None:
        self._clock_timer = QtCore.QTimer(self)
        self._clock_timer.timeout.connect(self._on_clock_tick)
        self._clock_timer.start(1000)
        self._on_clock_tick()

    def _on_clock_tick(self) -> None:
        self._uptime_secs += 1
        hours = self._uptime_secs // 3600
        minutes = (self._uptime_secs % 3600) // 60
        seconds = self._uptime_secs % 60
        self.header_uptime_value.setText(f"{hours:02d}:{minutes:02d}:{seconds:02d}")
        self.header_clock_value.setText(
            QtCore.QDateTime.currentDateTime().toString("HH:mm:ss")
        )

    def _clear_log_views(self) -> None:
        self.log_view.clear()
        self.rx_view.clear()

    def _append_log(self, text: str) -> None:
        t = QtCore.QTime.currentTime().toString("HH:mm:ss.zzz")
        level = "INFO"
        lower = text.lower()
        if "failed" in lower:
            level = "ERROR"
        elif "warning" in lower:
            level = "WARN"
        elif lower.startswith("rx <"):
            level = "DATA"
        self.log_view.appendPlainText(f"[{t}] [{level}] {text}")

    def _append_rx(self, text: str) -> None:
        self.rx_view.appendPlainText(text)

    def _on_scan_clicked(self) -> None:
        self.device_list.clear()
        self.scan_button.setEnabled(False)
        self.worker.scan(self.name_filter_edit.text())

    def _on_scan_item(self, name: str, address: str) -> None:
        item = QtWidgets.QListWidgetItem(f"{name}  [{address}]")
        item.setData(QtCore.Qt.UserRole, address)
        self.device_list.addItem(item)

        if self.header_device_value.text() == "N/A":
            self.header_device_value.setText(name)

    def _on_scan_done(self) -> None:
        self.scan_button.setEnabled(True)

    def _on_connect_clicked(self) -> None:
        item = self.device_list.currentItem()
        if item is None:
            self._append_log("Please select a scanned device first.")
            return

        address = item.data(QtCore.Qt.UserRole)
        name = item.text().split("[")[0].strip()
        self.header_device_value.setText(name)

        self.worker.connect_device(
            address,
            self.rx_uuid_edit.text(),
            self.tx_uuid_edit.text(),
        )

    def _on_send_clicked(self) -> None:
        text = self.tx_edit.text().strip()
        if not text:
            return
        self.worker.send_text(text)
        self.tx_edit.clear()

    def _on_toggle_wave_pause(self) -> None:
        self._wave_paused = not self._wave_paused
        self.wave_widget.set_paused(self._wave_paused)
        self.pause_wave_button.setText("RESUME" if self._wave_paused else "PAUSE")

    def _on_connected_changed(self, connected: bool) -> None:
        if connected:
            self.conn_status_label.setText("Status: Connected")
            self.connect_button.setEnabled(False)
            self.disconnect_button.setEnabled(True)
            self.send_button.setEnabled(True)
            self.ping_button.setEnabled(True)
            self.header_ble_status.setText("BLE Connected")
            self.header_ble_dot.setStyleSheet("color: #49df7b;")
            self.header_rssi_value.setText("-42 dBm")
            self._telemetry_labels["rssi"].setText("-42 dBm")
        else:
            self.conn_status_label.setText("Status: Disconnected")
            self.connect_button.setEnabled(True)
            self.disconnect_button.setEnabled(False)
            self.send_button.setEnabled(False)
            self.ping_button.setEnabled(False)
            self.header_ble_status.setText("BLE Disconnected")
            self.header_ble_dot.setStyleSheet("color: #ff5b5b;")
            self.header_rssi_value.setText("-- dBm")
            self._telemetry_labels["rssi"].setText("-- dBm")

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self.worker.shutdown()
        super().closeEvent(event)
