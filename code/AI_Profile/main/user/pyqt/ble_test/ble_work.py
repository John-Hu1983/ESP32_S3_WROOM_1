import asyncio
import threading
from typing import Optional

from PyQt5 import QtCore
from bleak import BleakClient, BleakScanner

DEFAULT_DEVICE_NAME = "ESP32S3-BLE-TEST"
DEFAULT_SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
DEFAULT_RX_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
DEFAULT_TX_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"


class BleWorker(QtCore.QObject):
    log_signal = QtCore.pyqtSignal(str)
    scan_item_signal = QtCore.pyqtSignal(str, str)
    scan_done_signal = QtCore.pyqtSignal()
    connected_signal = QtCore.pyqtSignal(bool)
    rx_signal = QtCore.pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

        self._client: Optional[BleakClient] = None
        self._rx_uuid = DEFAULT_RX_UUID
        self._tx_uuid = DEFAULT_TX_UUID

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def _submit(self, coro: asyncio.Future) -> None:
        asyncio.run_coroutine_threadsafe(coro, self._loop)

    def scan(self, name_filter: str) -> None:
        self._submit(self._scan_async(name_filter))

    async def _scan_async(self, name_filter: str) -> None:
        self.log_signal.emit("Scanning for BLE devices (5s)...")
        try:
            devices = await BleakScanner.discover(timeout=5.0)
        except Exception as exc:
            self.log_signal.emit(f"Scan failed: {exc}")
            self.scan_done_signal.emit()
            return

        key = name_filter.strip().lower()
        count = 0
        for dev in devices:
            name = (dev.name or "Unknown").strip()
            addr = dev.address
            if key and (key not in name.lower()) and (key not in addr.lower()):
                continue
            self.scan_item_signal.emit(name, addr)
            count += 1

        self.log_signal.emit(f"Scan complete: {count} match(es)")
        self.scan_done_signal.emit()

    def connect_device(self, address: str, rx_uuid: str, tx_uuid: str) -> None:
        self._submit(self._connect_async(address, rx_uuid, tx_uuid))

    async def _connect_async(self, address: str, rx_uuid: str, tx_uuid: str) -> None:
        await self._disconnect_async(silent=True)

        self._rx_uuid = rx_uuid.strip().lower()
        self._tx_uuid = tx_uuid.strip().lower()

        self.log_signal.emit(f"Connecting to {address}...")
        try:
            client = BleakClient(address, disconnected_callback=self._on_disconnected)
            await client.connect(timeout=10.0)
            await client.start_notify(self._tx_uuid, self._on_notify)
        except Exception as exc:
            self.log_signal.emit(f"Connect failed: {exc}")
            self.connected_signal.emit(False)
            return

        self._client = client
        self.connected_signal.emit(True)
        self.log_signal.emit("Connected, TX notify subscribed.")
        await self._send_async("PING")

    def _on_disconnected(self, _client: BleakClient) -> None:
        self.log_signal.emit("Disconnected by device.")
        self.connected_signal.emit(False)

    def _on_notify(self, _sender, data: bytearray) -> None:
        text = bytes(data).decode("utf-8", errors="replace")
        self.rx_signal.emit(text)
        self.log_signal.emit(f"RX < {text}")

    def disconnect_device(self) -> None:
        self._submit(self._disconnect_async(silent=False))

    async def _disconnect_async(self, silent: bool) -> None:
        if self._client is None:
            if not silent:
                self.connected_signal.emit(False)
            return

        try:
            if self._client.is_connected:
                try:
                    await self._client.stop_notify(self._tx_uuid)
                except Exception:
                    pass
                await self._client.disconnect()
        except Exception as exc:
            self.log_signal.emit(f"Disconnect warning: {exc}")

        self._client = None
        self.connected_signal.emit(False)
        if not silent:
            self.log_signal.emit("Disconnected.")

    def send_text(self, text: str) -> None:
        self._submit(self._send_async(text))

    async def _send_async(self, text: str) -> None:
        if self._client is None or not self._client.is_connected:
            self.log_signal.emit("Not connected.")
            return

        payload = text.encode("utf-8")
        if not payload:
            return

        try:
            await self._client.write_gatt_char(self._rx_uuid, payload, response=True)
        except Exception:
            await self._client.write_gatt_char(self._rx_uuid, payload, response=False)

        self.log_signal.emit(f"TX > {text}")

    def shutdown(self) -> None:
        fut = asyncio.run_coroutine_threadsafe(
            self._disconnect_async(silent=True), self._loop
        )
        try:
            fut.result(timeout=5.0)
        except Exception:
            pass

        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(timeout=2.0)