from __future__ import annotations

import socket
import threading
from typing import Callable

from ble_manager import BleManager

try:
    import serial  # type: ignore[import-not-found]
except ImportError:
    serial = None


AtLineCallback = Callable[[str], None]


class BleLineTransport:
    def __init__(self, ble_manager: BleManager) -> None:
        self._ble = ble_manager
        self._line_callback: AtLineCallback | None = None
        self._ble.rx_signal.connect(self._on_ble_rx)

    def set_line_callback(self, callback: AtLineCallback) -> None:
        self._line_callback = callback

    def send_text(self, text: str) -> None:
        self._ble.send_text(text)

    def close(self) -> None:
        self._line_callback = None

    def _on_ble_rx(self, text: str) -> None:
        if self._line_callback is None:
            return

        for line in text.splitlines():
            clean = line.strip()
            if clean:
                self._line_callback(clean)


class TcpLineTransport:
    def __init__(self, host: str, port: int, timeout_sec: float = 3.0) -> None:
        self._host = host
        self._port = port
        self._timeout_sec = timeout_sec
        self._sock: socket.socket | None = None
        self._line_callback: AtLineCallback | None = None
        self._rx_thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._lock = threading.Lock()

    def connect(self) -> None:
        if self._sock is not None:
            return

        sock = socket.create_connection((self._host, self._port), timeout=self._timeout_sec)
        sock.settimeout(0.5)
        self._sock = sock
        self._stop_event.clear()
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thread.start()

    def set_line_callback(self, callback: AtLineCallback) -> None:
        self._line_callback = callback

    def send_text(self, text: str) -> None:
        payload = (text + "\n").encode("utf-8")
        with self._lock:
            if self._sock is None:
                raise RuntimeError("TCP transport is not connected")
            self._sock.sendall(payload)

    def close(self) -> None:
        self._stop_event.set()
        with self._lock:
            sock = self._sock
            self._sock = None
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    def _rx_loop(self) -> None:
        buffer = ""

        while not self._stop_event.is_set():
            with self._lock:
                sock = self._sock
            if sock is None:
                return

            try:
                chunk = sock.recv(512)
            except TimeoutError:
                continue
            except OSError:
                return

            if not chunk:
                return

            buffer += chunk.decode("utf-8", errors="replace")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                clean = line.strip()
                if clean and self._line_callback is not None:
                    self._line_callback(clean)


class UartLineTransport:
    def __init__(self, port: str, baudrate: int = 115200, timeout_sec: float = 0.3) -> None:
        self._port = port
        self._baudrate = baudrate
        self._timeout_sec = timeout_sec
        self._ser = None
        self._line_callback: AtLineCallback | None = None
        self._rx_thread: threading.Thread | None = None
        self._stop_event = threading.Event()

    def open(self) -> None:
        if serial is None:
            raise RuntimeError("pyserial is not installed. Install with: pip install pyserial")

        if self._ser is not None:
            return

        self._ser = serial.Serial(
            port=self._port,
            baudrate=self._baudrate,
            timeout=self._timeout_sec,
        )
        self._stop_event.clear()
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thread.start()

    def set_line_callback(self, callback: AtLineCallback) -> None:
        self._line_callback = callback

    def send_text(self, text: str) -> None:
        if self._ser is None:
            raise RuntimeError("UART transport is not open")

        payload = (text + "\r\n").encode("utf-8")
        self._ser.write(payload)

    def close(self) -> None:
        self._stop_event.set()
        if self._ser is not None:
            try:
                self._ser.close()
            except OSError:
                pass
            self._ser = None

    def _rx_loop(self) -> None:
        while not self._stop_event.is_set():
            if self._ser is None:
                return

            try:
                raw = self._ser.readline()
            except OSError:
                return

            if not raw:
                continue

            text = raw.decode("utf-8", errors="replace").strip()
            if text and self._line_callback is not None:
                self._line_callback(text)
