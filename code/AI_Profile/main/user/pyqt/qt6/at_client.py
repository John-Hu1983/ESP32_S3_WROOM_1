from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Callable


@dataclass(frozen=True)
class AtPidGains:
    kp: float
    ki: float
    kd: float


AtPidCallback = Callable[[AtPidGains], None]
AtTextCallback = Callable[[str], None]


class AtClient:
    _NUM_PATTERN = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
    _PID_SIMPLE_RE = re.compile(
        rf"^AT\+PID\s*:\s*({_NUM_PATTERN})\s*,\s*({_NUM_PATTERN})\s*,\s*({_NUM_PATTERN})\s*$",
        re.IGNORECASE,
    )
    _PID_NAMED_RE = re.compile(
        rf"^AT\+PID\s*:\s*KP\s*=\s*({_NUM_PATTERN})\s*[, ]+"
        rf"KI\s*=\s*({_NUM_PATTERN})\s*[, ]+"
        rf"KD\s*=\s*({_NUM_PATTERN})\s*$",
        re.IGNORECASE,
    )

    def __init__(self, send_text: Callable[[str], None]) -> None:
        self._send_text = send_text
        self._pid_callbacks: list[AtPidCallback] = []
        self._ok_callbacks: list[AtTextCallback] = []
        self._error_callbacks: list[AtTextCallback] = []

    def on_pid(self, callback: AtPidCallback) -> None:
        if callback not in self._pid_callbacks:
            self._pid_callbacks.append(callback)

    def on_ok(self, callback: AtTextCallback) -> None:
        if callback not in self._ok_callbacks:
            self._ok_callbacks.append(callback)

    def on_error(self, callback: AtTextCallback) -> None:
        if callback not in self._error_callbacks:
            self._error_callbacks.append(callback)

    def send_reboot(self) -> None:
        self._send_text("AT+REBOOT:")

    def send_pid_get(self) -> None:
        self._send_text("AT+PID:")

    def send_pid_set(self, kp: float, ki: float, kd: float) -> None:
        self._send_text(f"AT+PID:{kp:.4f},{ki:.4f},{kd:.4f}")

    def handle_rx_text(self, text: str) -> bool:
        handled = False
        for raw_line in text.splitlines():
            if self.handle_rx_line(raw_line):
                handled = True
        return handled

    def handle_rx_line(self, line: str) -> bool:
        clean = line.strip()
        if not clean:
            return False

        pid = self._parse_pid_line(clean)
        if pid is not None:
            for callback in self._pid_callbacks:
                callback(pid)
            return True

        upper = clean.upper()
        if upper.startswith("AT+OK"):
            message = self._tail_message(clean)
            for callback in self._ok_callbacks:
                callback(message)
            return True

        if upper.startswith("AT+ERR"):
            message = self._tail_message(clean)
            for callback in self._error_callbacks:
                callback(message)
            return True

        return False

    @classmethod
    def _parse_pid_line(cls, line: str) -> AtPidGains | None:
        match = cls._PID_SIMPLE_RE.match(line)
        if match is None:
            match = cls._PID_NAMED_RE.match(line)
        if match is None:
            return None

        try:
            kp = float(match.group(1))
            ki = float(match.group(2))
            kd = float(match.group(3))
        except ValueError:
            return None

        return AtPidGains(kp=kp, ki=ki, kd=kd)

    @staticmethod
    def _tail_message(line: str) -> str:
        if ":" not in line:
            return ""
        return line.split(":", 1)[1].strip()
