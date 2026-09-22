from __future__ import annotations

import asyncio
import threading
from typing import Any

from PyQt6.QtCore import QObject, pyqtSignal

try:
    from bleak import BleakClient, BleakScanner
except Exception:
    BleakClient = None  # type: ignore[assignment]
    BleakScanner = None  # type: ignore[assignment]


class BleManager(QObject):
    DEFAULT_SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
    DEFAULT_RX_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
    DEFAULT_TX_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
    CONNECT_TIMEOUT_SEC = 6.0

    log_signal = pyqtSignal(str)
    scan_started_signal = pyqtSignal()
    scan_item_signal = pyqtSignal(str, str, int)
    scan_finished_signal = pyqtSignal(int)
    connected_signal = pyqtSignal(bool, str)
    rx_signal = pyqtSignal(str)
    tx_signal = pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

        self._client: Any = None
        self._service_uuid = self.DEFAULT_SERVICE_UUID
        self._rx_uuid = self.DEFAULT_RX_UUID
        self._tx_uuid = self.DEFAULT_TX_UUID
        self._connected_name = "N/A"

    @property
    def connected_name(self) -> str:
        return self._connected_name

    def configure_uuids(self, service_uuid: str, rx_uuid: str, tx_uuid: str) -> None:
        self._service_uuid = service_uuid.strip().lower()
        self._rx_uuid = rx_uuid.strip().lower()
        self._tx_uuid = tx_uuid.strip().lower()

    def scan(self, name_filter: str = "", timeout_sec: float = 6.0) -> None:
        self._submit(self._scan_async(name_filter=name_filter, timeout_sec=timeout_sec))

    def connect_device(self, address: str, name: str = "N/A") -> None:
        self._submit(self._connect_async(address=address, name=name))

    def disconnect_device(self) -> None:
        self._submit(self._disconnect_async(silent=False))

    def send_text(self, text: str) -> None:
        self._submit(self._send_async(text))

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

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def _submit(self, coro: Any) -> None:
        if self._loop.is_closed():
            return
        asyncio.run_coroutine_threadsafe(coro, self._loop)

    async def _scan_async(self, name_filter: str, timeout_sec: float) -> None:
        if BleakScanner is None:
            self.log_signal.emit(
                "BLE package not available. Install with: pip install bleak"
            )
            self.scan_finished_signal.emit(0)
            return

        self.scan_started_signal.emit()
        self.log_signal.emit(f"Scanning BLE devices for {timeout_sec:.1f}s ...")

        try:
            devices = await self._discover_devices(timeout_sec)
        except Exception as exc:
            self.log_signal.emit(f"Scan failed: {exc}")
            self.scan_finished_signal.emit(0)
            return

        key = self._normalize(name_filter)
        count = 0

        for name, address, rssi in devices:
            if key and not self._is_match(key, name, address):
                continue
            self.scan_item_signal.emit(name, address, rssi)
            count += 1

        if count == 0:
            if key:
                self.log_signal.emit(
                    f"No match for filter '{name_filter}'. Try empty filter to list all devices."
                )
            else:
                self.log_signal.emit(
                    "No BLE devices found. Check adapter, device advertising, and permissions."
                )

        self.log_signal.emit(f"Scan complete: {count} device(s).")
        self.scan_finished_signal.emit(count)

    async def _discover_devices(self, timeout_sec: float) -> list[tuple[str, str, int]]:
        if BleakScanner is None:
            return []

        found: list[tuple[str, str, int]] = []

        try:
            adv_map = await BleakScanner.discover(timeout=timeout_sec, return_adv=True)
            for address, value in adv_map.items():
                device, adv_data = value

                adv_name = (getattr(adv_data, "local_name", "") or "").strip()
                dev_name = (getattr(device, "name", "") or "").strip()
                name = adv_name or dev_name or "Unknown"

                adv_rssi = getattr(adv_data, "rssi", None)
                dev_rssi = getattr(device, "rssi", None)
                if isinstance(adv_rssi, int):
                    rssi = adv_rssi
                elif isinstance(dev_rssi, int):
                    rssi = dev_rssi
                else:
                    rssi = -999

                dev_addr = str(getattr(device, "address", address))
                found.append((name, dev_addr, rssi))
            return self._dedupe(found)
        except TypeError:
            pass

        devices = await BleakScanner.discover(timeout=timeout_sec)
        for device in devices:
            name = (getattr(device, "name", "") or "").strip() or "Unknown"
            address = str(getattr(device, "address", ""))
            rssi_raw = getattr(device, "rssi", -999)
            rssi = rssi_raw if isinstance(rssi_raw, int) else -999
            found.append((name, address, rssi))

        return self._dedupe(found)

    async def _connect_async(self, address: str, name: str) -> None:
        if BleakClient is None:
            self.log_signal.emit(
                "BLE package not available. Install with: pip install bleak"
            )
            self.connected_signal.emit(False, "N/A")
            return

        await self._disconnect_async(silent=True)
        self.log_signal.emit(f"Connecting to {name} [{address}] ...")

        try:
            client = BleakClient(address, disconnected_callback=self._on_disconnected)
            await client.connect(timeout=self.CONNECT_TIMEOUT_SEC)
        except Exception as exc:
            self.log_signal.emit(f"Connect failed: {exc}")
            self.connected_signal.emit(False, "N/A")
            return

        self._client = client
        self._connected_name = name or address
        self.log_signal.emit(f"Connected: {self._connected_name}")

        if self._tx_uuid:
            try:
                await self._client.start_notify(self._tx_uuid, self._on_notify)
                self.log_signal.emit(f"Notify subscribed: {self._tx_uuid}")
            except Exception as exc:
                self.log_signal.emit(f"Notify subscribe failed: {exc}")

        self.connected_signal.emit(True, self._connected_name)

    async def _disconnect_async(self, silent: bool) -> None:
        if self._client is None:
            if not silent:
                self.connected_signal.emit(False, "N/A")
            return

        try:
            if self._client.is_connected:
                if self._tx_uuid:
                    try:
                        await self._client.stop_notify(self._tx_uuid)
                    except Exception:
                        pass
                await self._client.disconnect()
        except Exception as exc:
            self.log_signal.emit(f"Disconnect warning: {exc}")

        self._client = None
        self._connected_name = "N/A"
        self.connected_signal.emit(False, "N/A")
        if not silent:
            self.log_signal.emit("Disconnected.")

    async def _send_async(self, text: str) -> None:
        if self._client is None or not self._client.is_connected:
            self.log_signal.emit("Not connected.")
            return

        payload = text.encode("utf-8")
        if not payload:
            return

        if not self._rx_uuid:
            self.log_signal.emit("RX UUID is empty. Configure UUID before sending.")
            return

        try:
            await self._client.write_gatt_char(self._rx_uuid, payload, response=True)
        except Exception:
            await self._client.write_gatt_char(self._rx_uuid, payload, response=False)

        self.tx_signal.emit(text)
        self.log_signal.emit(f"TX > {text}")

    def _on_notify(self, _sender: Any, data: bytearray) -> None:
        text = bytes(data).decode("utf-8", errors="replace")
        self.rx_signal.emit(text)
        self.log_signal.emit(f"RX < {text}")

    def _on_disconnected(self, _client: Any) -> None:
        self._client = None
        self._connected_name = "N/A"
        self.connected_signal.emit(False, "N/A")
        self.log_signal.emit("Disconnected by device.")

    @staticmethod
    def _normalize(value: str) -> str:
        return "".join(ch for ch in value.lower() if ch.isalnum())

    def _is_match(self, key: str, name: str, address: str) -> bool:
        n_name = self._normalize(name)
        n_addr = self._normalize(address)
        return key in n_name or key in n_addr

    @staticmethod
    def _dedupe(items: list[tuple[str, str, int]]) -> list[tuple[str, str, int]]:
        by_addr: dict[str, tuple[str, int]] = {}

        for name, address, rssi in items:
            if not address:
                continue

            old = by_addr.get(address)
            if old is None:
                by_addr[address] = (name, rssi)
                continue

            old_name, old_rssi = old
            if old_name == "Unknown" and name != "Unknown":
                by_addr[address] = (name, rssi)
                continue

            if rssi > old_rssi:
                by_addr[address] = (old_name, rssi)

        merged = [(name, address, rssi) for address, (name, rssi) in by_addr.items()]
        merged.sort(key=lambda item: (item[0].lower(), item[1]))
        return merged
