from __future__ import annotations

import json
import re
import time
from collections import deque
from typing import TypeVar

from PyQt6.QtCore import QDateTime, Qt
from PyQt6.QtGui import QColor, QPainter, QPen
from PyQt6.QtWidgets import (
    QComboBox,
    QDial,
    QDoubleSpinBox,
    QFrame,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from at_client import AtClient, AtPidGains
from ble_manager import BleManager


TWidget = TypeVar("TWidget", bound=QWidget)


class SteeringScopeWidget(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._samples: deque[tuple[float, float, float, float]] = deque(maxlen=4000)
        self._window_sec = 12.0
        self._paused = False
        self.setMinimumHeight(280)

    def set_window_seconds(self, seconds: float) -> None:
        self._window_sec = max(2.0, float(seconds))
        self.update()

    def set_paused(self, paused: bool) -> None:
        self._paused = paused

    def clear(self) -> None:
        self._samples.clear()
        self.update()

    def add_sample(self, setpoint: float, feedback: float, pwm: float) -> None:
        if self._paused:
            return

        now = time.monotonic()
        self._samples.append((now, setpoint, feedback, pwm))
        self._trim_old_samples(now)
        self.update()

    def _trim_old_samples(self, now: float) -> None:
        keep_after = now - max(self._window_sec + 2.0, self._window_sec * 1.5)
        while self._samples and self._samples[0][0] < keep_after:
            self._samples.popleft()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        rect = self.rect().adjusted(10, 10, -10, -10)
        if rect.width() < 80 or rect.height() < 60:
            return

        painter.fillRect(rect, QColor("#040b16"))
        painter.setPen(QPen(QColor("#17314a"), 1))

        for i in range(1, 10):
            x = rect.left() + int(rect.width() * i / 10)
            painter.drawLine(x, rect.top(), x, rect.bottom())

        for i in range(1, 6):
            y = rect.top() + int(rect.height() * i / 6)
            painter.drawLine(rect.left(), y, rect.right(), y)

        if len(self._samples) < 2:
            painter.setPen(QColor("#6f87a2"))
            painter.drawText(
                rect.adjusted(8, 8, -8, -8),
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
                "Waiting for steering telemetry...",
            )
            return

        latest_time = self._samples[-1][0]
        start_time = latest_time - self._window_sec
        visible_samples = [row for row in self._samples if row[0] >= start_time]
        if len(visible_samples) < 2:
            return

        values = [v for _, sp, fb, pwm in visible_samples for v in (sp, fb, pwm)]
        y_min = min(values)
        y_max = max(values)
        if abs(y_max - y_min) < 1.0:
            y_min -= 0.5
            y_max += 0.5

        margin = max(1.0, (y_max - y_min) * 0.12)
        y_min -= margin
        y_max += margin

        def map_x(sample_t: float) -> int:
            span_t = max(self._window_sec, 0.001)
            ratio = (sample_t - start_time) / span_t
            return rect.left() + int(max(0.0, min(1.0, ratio)) * rect.width())

        def map_y(sample_v: float) -> int:
            span_v = max(y_max - y_min, 0.001)
            ratio = (sample_v - y_min) / span_v
            return rect.bottom() - int(max(0.0, min(1.0, ratio)) * rect.height())

        self._draw_trace(
            painter,
            visible_samples,
            map_x,
            map_y,
            index=1,
            color=QColor("#4dd8ff"),
        )
        self._draw_trace(
            painter,
            visible_samples,
            map_x,
            map_y,
            index=2,
            color=QColor("#73ffa1"),
        )
        self._draw_trace(
            painter,
            visible_samples,
            map_x,
            map_y,
            index=3,
            color=QColor("#ffc766"),
        )

        painter.setPen(QColor("#8ba7c2"))
        painter.drawText(
            rect.adjusted(6, 6, -6, -6),
            Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
            f"{self._window_sec:.0f}s  y:[{y_min:.1f}, {y_max:.1f}]",
        )

        legend_top = rect.top() + 8
        self._draw_legend_item(painter, rect.left() + 14, legend_top, "Setpoint", "#4dd8ff")
        self._draw_legend_item(painter, rect.left() + 110, legend_top, "Feedback", "#73ffa1")
        self._draw_legend_item(painter, rect.left() + 206, legend_top, "PWM", "#ffc766")

    @staticmethod
    def _draw_trace(
        painter: QPainter,
        samples: list[tuple[float, float, float, float]],
        map_x,
        map_y,
        index: int,
        color: QColor,
    ) -> None:
        pen = QPen(color, 1.8)
        painter.setPen(pen)

        last_x = map_x(samples[0][0])
        last_y = map_y(samples[0][index])
        for row in samples[1:]:
            sample_t = row[0]
            val = row[index]
            x = map_x(sample_t)
            y = map_y(val)
            painter.drawLine(last_x, last_y, x, y)
            last_x = x
            last_y = y

    @staticmethod
    def _draw_legend_item(
        painter: QPainter, x: int, y: int, title: str, color: str
    ) -> None:
        painter.fillRect(x, y + 4, 10, 2, QColor(color))
        painter.setPen(QColor("#9db6cf"))
        painter.drawText(x + 14, y + 8, title)


class SteeringTabController:
    def __init__(self, ble_api: BleManager, tab_root: QWidget) -> None:
        self._ble = ble_api
        self._at = AtClient(self._ble.send_text)
        self._root = tab_root
        self._is_connected = False
        self._angle_syncing = False

        self.scope_host: QFrame
        self.scope_window_combo: QComboBox
        self.scope_pause_button: QPushButton
        self.scope_clear_button: QPushButton
        self.scope_live_badge: QLabel
        self.steering_conn_led: QLabel

        self.kp_spin: QDoubleSpinBox
        self.ki_spin: QDoubleSpinBox
        self.kd_spin: QDoubleSpinBox
        self.pid_out_limit_spin: QDoubleSpinBox
        self.pid_apply_button: QPushButton
        self.pid_reset_i_button: QPushButton

        self.angle_knob: QDial
        self.angle_set_spin: QDoubleSpinBox
        self.knob_value_lab: QLabel
        self.adc_set_spin: QLineEdit
        self.setpoint_send_button: QPushButton
        self.motor_enable_button: QPushButton
        self.motor_stop_button: QPushButton

        self.rt_conn_value: QLabel
        self.rt_set_value: QLabel
        self.rt_fb_value: QLabel
        self.rt_err_value: QLabel
        self.rt_pwm_value: QLabel
        self.rt_rpm_value: QLabel
        self.rt_vbus_value: QLabel

        self.telemetry_raw_view: QPlainTextEdit
        self.telemetry_cmd_edit: QLineEdit
        self.telemetry_send_button: QPushButton
        self.telemetry_clear_button: QPushButton

        self._last_setpoint = 0.0
        self._last_feedback = 0.0
        self._last_pwm = 0.0

        self._collect_widgets()
        self._scope = SteeringScopeWidget(self.scope_host)

        host_layout = self.scope_host.layout()
        if host_layout is None:
            host_layout = QVBoxLayout(self.scope_host)
            host_layout.setContentsMargins(0, 0, 0, 0)
            host_layout.setSpacing(0)
        host_layout.addWidget(self._scope)

        self.telemetry_raw_view.document().setMaximumBlockCount(1500)

        self._init_controls()
        self._bind_signals()
        self._on_connected_changed(False, "N/A")

    def _collect_widgets(self) -> None:
        self.scope_host = self._must_find(QFrame, "scope_host")
        self.scope_window_combo = self._must_find(QComboBox, "scope_window_combo")
        self.scope_pause_button = self._must_find(QPushButton, "scope_pause_button")
        self.scope_clear_button = self._must_find(QPushButton, "scope_clear_button")
        self.scope_live_badge = self._must_find(QLabel, "scope_live_badge")
        self.steering_conn_led = self._must_find(QLabel, "steering_conn_led")

        self.kp_spin = self._must_find(QDoubleSpinBox, "kp_spin")
        self.ki_spin = self._must_find(QDoubleSpinBox, "ki_spin")
        self.kd_spin = self._must_find(QDoubleSpinBox, "kd_spin")
        self.pid_out_limit_spin = self._must_find(QDoubleSpinBox, "pid_out_limit_spin")
        self.pid_apply_button = self._must_find(QPushButton, "pid_apply_button")
        self.pid_reset_i_button = self._must_find(QPushButton, "pid_reset_i_button")

        self.angle_knob = self._must_find(QDial, "angle_knob")
        self.angle_set_spin = self._must_find(QDoubleSpinBox, "angle_set_spin")
        self.knob_value_lab = self._must_find(QLabel, "knob_value_lab")
        self.adc_set_spin = self._must_find(QLineEdit, "adc_set_spin")
        self.setpoint_send_button = self._must_find(QPushButton, "setpoint_send_button")
        self.motor_enable_button = self._must_find(QPushButton, "motor_enable_button")
        self.motor_stop_button = self._must_find(QPushButton, "motor_stop_button")

        self.rt_conn_value = self._must_find(QLabel, "rt_conn_value")
        self.rt_set_value = self._must_find(QLabel, "rt_set_value")
        self.rt_fb_value = self._must_find(QLabel, "rt_fb_value")
        self.rt_err_value = self._must_find(QLabel, "rt_err_value")
        self.rt_pwm_value = self._must_find(QLabel, "rt_pwm_value")
        self.rt_rpm_value = self._must_find(QLabel, "rt_rpm_value")
        self.rt_vbus_value = self._must_find(QLabel, "rt_vbus_value")

        self.telemetry_raw_view = self._must_find(QPlainTextEdit, "telemetry_raw_view")
        self.telemetry_cmd_edit = self._must_find(QLineEdit, "telemetry_cmd_edit")
        self.telemetry_send_button = self._must_find(QPushButton, "telemetry_send_button")
        self.telemetry_clear_button = self._must_find(
            QPushButton, "telemetry_clear_button"
        )

    def _must_find(self, widget_type: type[TWidget], name: str) -> TWidget:
        widget = self._root.findChild(widget_type, name)
        if widget is None:
            raise RuntimeError(f"Missing widget '{name}' in total.ui")
        return widget

    def _init_controls(self) -> None:
        if self.scope_window_combo.count() == 0:
            self.scope_window_combo.addItem("8 s", 8.0)
            self.scope_window_combo.addItem("12 s", 12.0)
            self.scope_window_combo.addItem("20 s", 20.0)
            self.scope_window_combo.addItem("30 s", 30.0)

        self.scope_window_combo.setCurrentIndex(min(1, self.scope_window_combo.count() - 1))
        self._scope.set_window_seconds(self._current_scope_window_sec())

        self.angle_knob.setRange(0, 330)
        self.angle_set_spin.setRange(0.0, 330.0)
        self.angle_set_spin.setDecimals(0)
        self.angle_set_spin.setSingleStep(1.0)
        self.angle_set_spin.setValue(0.0)
        self.knob_value_lab.setAttribute(
            Qt.WidgetAttribute.WA_TransparentForMouseEvents, True
        )
        self.knob_value_lab.setText("0 deg")
        self.adc_set_spin.setReadOnly(True)
        adc_value = self._set_adc_for_degree(0)

        self.rt_set_value.setText(str(adc_value))
        self.rt_fb_value.setText("0.0 deg")
        self.rt_err_value.setText("0.0 deg")
        self.rt_pwm_value.setText("0.0 %")
        self.rt_rpm_value.setText("0.0 rpm")
        self.rt_vbus_value.setText("0.0 V")

        self.scope_live_badge.setText("LIVE")
        self.scope_pause_button.setText("Pause")

    def _set_adc_for_degree(self, degree: int) -> int:
        adc_value = self._degree_to_adc(degree)
        self.adc_set_spin.setText(str(adc_value))
        return adc_value

    @staticmethod
    def _degree_to_adc(degree: int) -> int:
        clamped = max(0, min(330, int(degree)))
        return int(round(clamped * 4095.0 / 330.0))

    def _setpoint_to_adc_text(self, setpoint: float) -> str:
        rounded = int(round(setpoint))
        if 0 <= rounded <= 330:
            return str(self._degree_to_adc(rounded))
        return str(rounded)

    def _bind_signals(self) -> None:
        self.scope_window_combo.currentIndexChanged.connect(self._on_scope_window_changed)
        self.scope_pause_button.clicked.connect(self._on_scope_pause_clicked)
        self.scope_clear_button.clicked.connect(self._on_scope_clear_clicked)

        self.angle_knob.valueChanged.connect(self._on_angle_knob_changed)
        self.angle_set_spin.valueChanged.connect(self._on_angle_spin_changed)

        self.setpoint_send_button.clicked.connect(self._on_send_setpoint_clicked)
        self.motor_enable_button.clicked.connect(self._on_motor_enable_clicked)
        self.motor_stop_button.clicked.connect(self._on_motor_stop_clicked)

        self.pid_apply_button.clicked.connect(self._on_apply_pid_clicked)
        self.pid_reset_i_button.clicked.connect(self._on_reset_i_clicked)

        self.telemetry_send_button.clicked.connect(self._on_manual_send_clicked)
        self.telemetry_clear_button.clicked.connect(self._on_clear_raw_telemetry_clicked)
        self.telemetry_cmd_edit.returnPressed.connect(self._on_manual_send_clicked)

        self._ble.connected_signal.connect(self._on_connected_changed)
        self._ble.rx_signal.connect(self._on_ble_rx)
        self._ble.tx_signal.connect(self._on_ble_tx)

        self._at.on_pid(self._on_at_pid)
        self._at.on_ok(self._on_at_ok)
        self._at.on_error(self._on_at_error)

    def _current_scope_window_sec(self) -> float:
        data = self.scope_window_combo.currentData()
        if isinstance(data, (int, float)):
            return float(data)

        text = self.scope_window_combo.currentText()
        match = re.search(r"(\d+(?:\.\d+)?)", text)
        if match is None:
            return 12.0
        return float(match.group(1))

    def _set_command_widgets_enabled(self, enabled: bool) -> None:
        self.setpoint_send_button.setEnabled(enabled)
        self.motor_enable_button.setEnabled(enabled)
        self.motor_stop_button.setEnabled(enabled)
        self.pid_apply_button.setEnabled(enabled)
        self.pid_reset_i_button.setEnabled(enabled)
        self.telemetry_send_button.setEnabled(enabled)

    def _on_connected_changed(self, connected: bool, _device_name: str) -> None:
        self._is_connected = connected
        self.rt_conn_value.setText("Connected" if connected else "Disconnected")
        self._update_ble_led()
        self._set_command_widgets_enabled(connected)
        if not connected:
            self._append_note("Steering tab ready. Connect BLE device first.")

    def _update_ble_led(self) -> None:
        color = "#ff2828" if self._is_connected else "#090909"
        self.steering_conn_led.setStyleSheet(
            "QLabel#steering_conn_led {"
            f"background-color: {color};"
            "border: 1px solid #2a3644;"
            "border-radius: 12px;"
            "}"
        )

    def _on_scope_window_changed(self) -> None:
        self._scope.set_window_seconds(self._current_scope_window_sec())

    def _on_scope_pause_clicked(self) -> None:
        paused = self.scope_pause_button.text().strip().lower() == "resume"
        if paused:
            self._scope.set_paused(False)
            self.scope_pause_button.setText("Pause")
            self.scope_live_badge.setText("LIVE")
        else:
            self._scope.set_paused(True)
            self.scope_pause_button.setText("Resume")
            self.scope_live_badge.setText("PAUSED")

    def _on_scope_clear_clicked(self) -> None:
        self._scope.clear()
        self._append_note("Scope buffer cleared.")

    def _on_angle_knob_changed(self, value: int) -> None:
        if self._angle_syncing:
            return

        angle = float(value)
        self._angle_syncing = True
        self.angle_set_spin.setValue(angle)
        self._angle_syncing = False

        angle_int = int(round(angle))
        self.knob_value_lab.setText(f"{angle_int} deg")
        adc_value = self._set_adc_for_degree(angle_int)
        self.rt_set_value.setText(str(adc_value))

    def _on_angle_spin_changed(self, value: float) -> None:
        if self._angle_syncing:
            return

        value_int = int(round(value))
        self._angle_syncing = True
        self.angle_knob.setValue(value_int)
        self._angle_syncing = False

        self.knob_value_lab.setText(f"{value_int} deg")
        adc_value = self._set_adc_for_degree(value_int)
        self.rt_set_value.setText(str(adc_value))

    def _on_send_setpoint_clicked(self) -> None:
        angle = int(round(self.angle_set_spin.value()))
        adc_value = self._set_adc_for_degree(angle)
        self._send_command(f"AT+SETPOINT:{angle},{adc_value}")

    def _on_motor_enable_clicked(self) -> None:
        self._send_command("STEER_ENABLE 1")

    def _on_motor_stop_clicked(self) -> None:
        self._send_command("STEER_STOP")

    def _on_apply_pid_clicked(self) -> None:
        kp = self.kp_spin.value()
        ki = self.ki_spin.value()
        kd = self.kd_spin.value()
        self._at.send_pid_set(kp=kp, ki=ki, kd=kd)

    def _on_reset_i_clicked(self) -> None:
        self._send_command("STEER_PID_RESET_I")

    def _on_manual_send_clicked(self) -> None:
        text = self.telemetry_cmd_edit.text().strip()
        if not text:
            return
        self._send_command(text)
        self.telemetry_cmd_edit.clear()

    def _on_clear_raw_telemetry_clicked(self) -> None:
        self.telemetry_raw_view.clear()

    def _send_command(self, text: str) -> None:
        if not self._is_connected:
            self._append_note("Command skipped: BLE is not connected.")
            return
        self._ble.send_text(text)

    def _on_ble_tx(self, text: str) -> None:
        self._append_log(f"TX > {text}")

    def _on_ble_rx(self, text: str) -> None:
        lines = [line.strip() for line in text.splitlines() if line.strip()]
        if not lines:
            return

        for line in lines:
            self._append_log(f"RX < {line}")
            if self._at.handle_rx_line(line):
                continue
            telemetry = self._parse_telemetry_line(line)
            if telemetry:
                self._apply_telemetry(telemetry)

    def _on_at_pid(self, pid: AtPidGains) -> None:
        if not self.kp_spin.hasFocus():
            self.kp_spin.setValue(pid.kp)
        if not self.ki_spin.hasFocus():
            self.ki_spin.setValue(pid.ki)
        if not self.kd_spin.hasFocus():
            self.kd_spin.setValue(pid.kd)
        self._append_note(
            f"AT PID synced: KP={pid.kp:.4f} KI={pid.ki:.4f} KD={pid.kd:.4f}"
        )

    def _on_at_ok(self, message: str) -> None:
        if message:
            self._append_note(f"AT OK: {message}")
        else:
            self._append_note("AT OK")

    def _on_at_error(self, message: str) -> None:
        if message:
            self._append_note(f"AT ERROR: {message}")
        else:
            self._append_note("AT ERROR")

    def _append_note(self, text: str) -> None:
        self._append_log(f"SYS < {text}")

    def _append_log(self, text: str) -> None:
        stamp = QDateTime.currentDateTime().toString("HH:mm:ss")
        self.telemetry_raw_view.appendPlainText(f"[{stamp}] {text}")

    def _parse_telemetry_line(self, text: str) -> dict[str, float]:
        numeric: dict[str, float] = {}

        if "{" in text and "}" in text:
            start_idx = text.find("{")
            end_idx = text.rfind("}")
            if start_idx >= 0 and end_idx > start_idx:
                json_part = text[start_idx : end_idx + 1]
                try:
                    obj = json.loads(json_part)
                    if isinstance(obj, dict):
                        for key, value in obj.items():
                            parsed = self._to_float(value)
                            if parsed is not None:
                                numeric[str(key).lower()] = parsed
                except (json.JSONDecodeError, TypeError, ValueError):
                    pass

        for key, value in re.findall(
            r"([A-Za-z_][A-Za-z0-9_]*)\s*[:=]\s*([-+]?\d+(?:\.\d+)?)", text
        ):
            parsed = self._to_float(value)
            if parsed is not None:
                numeric[key.lower()] = parsed

        if not numeric:
            return {}

        telemetry = {
            "setpoint": self._first_present(
                numeric,
                "setpoint",
                "target",
                "set",
                "sp",
                "cmd",
                "angle_cmd",
                "ref",
            ),
            "feedback": self._first_present(
                numeric,
                "motvr",
                "servo_vr",
                "vr",
                "feedback",
                "fb",
                "angle",
                "pos",
                "position",
                "meas",
            ),
            "error": self._first_present(numeric, "error", "err"),
            "pwm": self._first_present(
                numeric,
                "motpwm",
                "motor_pwm",
                "pwm",
                "out",
                "output",
                "duty",
            ),
            "rpm": self._first_present(numeric, "rpm", "speed"),
            "vbus": self._first_present(numeric, "vbus", "voltage", "batt", "vbatt"),
            "kp": self._first_present(numeric, "kp"),
            "ki": self._first_present(numeric, "ki"),
            "kd": self._first_present(numeric, "kd"),
        }

        return {k: v for k, v in telemetry.items() if v is not None}

    def _apply_telemetry(self, data: dict[str, float]) -> None:
        setpoint = data.get("setpoint")
        feedback = data.get("feedback")
        error = data.get("error")
        pwm = data.get("pwm")
        rpm = data.get("rpm")
        vbus = data.get("vbus")

        if setpoint is not None:
            self._last_setpoint = setpoint
            self.rt_set_value.setText(self._setpoint_to_adc_text(setpoint))
        if feedback is not None:
            self._last_feedback = feedback
            if abs(feedback) > 360:
                self.rt_fb_value.setText(str(int(round(feedback))))
            else:
                self.rt_fb_value.setText(f"{feedback:.1f} deg")
        if error is not None:
            self.rt_err_value.setText(f"{error:.2f} deg")
        elif (
            setpoint is not None
            and feedback is not None
            and abs(setpoint) <= 360
            and abs(feedback) <= 360
        ):
            self.rt_err_value.setText(f"{(setpoint - feedback):.2f} deg")

        if pwm is not None:
            self._last_pwm = pwm
            self.rt_pwm_value.setText(f"{pwm:+.1f} %")
        if rpm is not None:
            self.rt_rpm_value.setText(f"{rpm:.1f} rpm")
        if vbus is not None:
            self.rt_vbus_value.setText(f"{vbus:.2f} V")

        kp = data.get("kp")
        if kp is not None and not self.kp_spin.hasFocus():
            self.kp_spin.setValue(kp)
        ki = data.get("ki")
        if ki is not None and not self.ki_spin.hasFocus():
            self.ki_spin.setValue(ki)
        kd = data.get("kd")
        if kd is not None and not self.kd_spin.hasFocus():
            self.kd_spin.setValue(kd)

        self._scope.add_sample(self._last_setpoint, self._last_feedback, self._last_pwm)

    @staticmethod
    def _to_float(value: object) -> float | None:
        if isinstance(value, (int, float)):
            return float(value)
        if isinstance(value, str):
            try:
                return float(value.strip())
            except ValueError:
                return None
        return None

    @staticmethod
    def _first_present(source: dict[str, float], *keys: str) -> float | None:
        for key in keys:
            if key in source:
                return source[key]
        return None
