from __future__ import annotations

from typing import TypeVar

from PyQt6.QtCore import QDateTime, QTimer, Qt
from PyQt6.QtWidgets import (
    QGridLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPlainTextEdit,
    QPushButton,
    QWidget,
)

from ble_manager import BleManager
from user_config import UserConfigStore


TWidget = TypeVar("TWidget", bound=QWidget)


class BleTabController:
    def __init__(self, ble_api: BleManager, tab_root: QWidget) -> None:
        self._ble = ble_api
        self._root = tab_root
        self._is_connected = False
        self._is_scanning = False
        self._config = UserConfigStore()
        self._auto_connect_pending = False
        self._auto_scan_requested = False
        self._auto_target_name = ""
        self._auto_target_address = ""
        self._last_connect_name = ""
        self._last_connect_address = ""

        self.device_filter_edit: QLineEdit
        self.scan_button: QPushButton
        self.connect_button: QPushButton
        self.disconnect_button: QPushButton

        self.service_uuid_edit: QLineEdit
        self.rx_uuid_edit: QLineEdit
        self.tx_uuid_edit: QLineEdit
        self.apply_config_button: QPushButton

        self.conn_value: QLabel
        self.device_value: QLabel
        self.service_value: QLabel
        self.rx_value: QLabel
        self.tx_value: QLabel
        self.config_summary: QLabel

        self.device_list: QListWidget
        self.rx_view: QPlainTextEdit
        self.tx_view: QPlainTextEdit
        self.tx_input: QLineEdit
        self.send_button: QPushButton
        self.clear_button: QPushButton

        self.ping_button: QPushButton
        self.get_status_button: QPushButton
        self.reboot_button: QPushButton

        self._collect_widgets()
        self._load_saved_device_preference()
        self._apply_layout_tuning()
        self._apply_button_variants()
        self._apply_theme()
        self._bind_signals()

        self.rx_view.document().setMaximumBlockCount(2000)
        self.tx_view.document().setMaximumBlockCount(2000)

        self._is_connected = False
        self.disconnect_button.setEnabled(False)
        self.send_button.setEnabled(False)

        self._apply_ble_config(notify=False)
        self._schedule_auto_connect_if_enabled()

    def _collect_widgets(self) -> None:
        self.device_filter_edit = self._must_find(QLineEdit, "device_filter_edit")
        self.scan_button = self._must_find(QPushButton, "scan_button")
        self.connect_button = self._must_find(QPushButton, "connect_button")
        self.disconnect_button = self._must_find(QPushButton, "disconnect_button")

        self.service_uuid_edit = self._must_find(QLineEdit, "service_uuid_edit")
        self.rx_uuid_edit = self._must_find(QLineEdit, "rx_uuid_edit")
        self.tx_uuid_edit = self._must_find(QLineEdit, "tx_uuid_edit")
        self.apply_config_button = self._must_find(QPushButton, "apply_config_button")

        self.conn_value = self._must_find(QLabel, "conn_value")
        self.device_value = self._must_find(QLabel, "device_value")
        self.service_value = self._must_find(QLabel, "service_value")
        self.rx_value = self._must_find(QLabel, "rx_value")
        self.tx_value = self._must_find(QLabel, "tx_value")
        self.config_summary = self._must_find(QLabel, "config_summary")

        self.device_list = self._must_find(QListWidget, "device_list")
        self.rx_view = self._must_find(QPlainTextEdit, "rx_view")
        self.tx_view = self._must_find(QPlainTextEdit, "tx_view")
        self.tx_input = self._must_find(QLineEdit, "tx_input")
        self.send_button = self._must_find(QPushButton, "send_button")
        self.clear_button = self._must_find(QPushButton, "clear_button")

        self.ping_button = self._must_find(QPushButton, "ping_button")
        self.get_status_button = self._must_find(QPushButton, "get_status_button")
        self.reboot_button = self._must_find(QPushButton, "reboot_button")

    def _apply_layout_tuning(self) -> None:
        control_grid = self._root.findChild(QGridLayout, "control_grid")
        if control_grid is None:
            return

        control_grid.setColumnStretch(0, 0)
        control_grid.setColumnStretch(1, 2)
        control_grid.setColumnStretch(2, 2)
        control_grid.setColumnStretch(3, 0)
        control_grid.setColumnStretch(4, 3)
        control_grid.setColumnStretch(5, 0)
        control_grid.setColumnStretch(6, 3)

    def _must_find(self, widget_type: type[TWidget], name: str) -> TWidget:
        widget = self._root.findChild(widget_type, name)
        if widget is None:
            raise RuntimeError(f"Missing widget '{name}' in total.ui")
        return widget

    def _apply_button_variants(self) -> None:
        self.scan_button.setProperty("variant", "accent")
        self.connect_button.setProperty("variant", "primary")
        self.disconnect_button.setProperty("variant", "danger")
        self.apply_config_button.setProperty("variant", "ghost")

    def _load_saved_device_preference(self) -> None:
        self._auto_target_name = self._config.last_device_name
        self._auto_target_address = self._config.last_device_address

        if self._auto_target_name:
            self.device_filter_edit.setText(self._auto_target_name)
        elif self._auto_target_address:
            self.device_filter_edit.setText(self._auto_target_address)

    def _schedule_auto_connect_if_enabled(self) -> None:
        if not self._config.auto_connect_last_device:
            return

        if not self._auto_target_name and not self._auto_target_address:
            return

        self._auto_connect_pending = True
        QTimer.singleShot(700, self._try_auto_connect_last_device)

    def _try_auto_connect_last_device(self) -> None:
        if not self._auto_connect_pending or self._is_connected:
            return

        self._apply_ble_config(notify=False)

        if self._auto_target_address:
            name = self._auto_target_name or self._auto_target_address
            self._append_sys(
                f"Auto connect: try last device {name} [{self._auto_target_address}]"
            )
            self._connect_to_device(name=name, address=self._auto_target_address)
            QTimer.singleShot(2600, self._start_auto_scan_for_last_device)
            return

        self._start_auto_scan_for_last_device()

    def _start_auto_scan_for_last_device(self) -> None:
        if not self._auto_connect_pending or self._is_connected or self._is_scanning:
            return

        filter_text = self.device_filter_edit.text().strip()
        if not filter_text:
            filter_text = self._auto_target_name or self._auto_target_address
            self.device_filter_edit.setText(filter_text)

        self._append_sys(f"Auto connect: scan with filter '{filter_text or 'ALL'}'.")
        self.device_list.clear()
        self._auto_scan_requested = True
        self._ble.scan(name_filter=filter_text, timeout_sec=6.0)

    def _pick_auto_connect_item(self) -> QListWidgetItem | None:
        target_address = self._auto_target_address.strip().lower()
        target_name = self._normalize_device_name(self._auto_target_name)

        for index in range(self.device_list.count()):
            item = self.device_list.item(index)
            if item is None:
                continue

            info = item.data(Qt.ItemDataRole.UserRole)
            if not isinstance(info, dict):
                continue

            name = str(info.get("name", "")).strip()
            address = str(info.get("address", "")).strip().lower()

            if target_address and address == target_address:
                return item

            if target_name and self._normalize_device_name(name) == target_name:
                return item

        return None

    def _save_last_connected_device(self, connected_name: str) -> None:
        name = connected_name.strip() or self._last_connect_name.strip()
        address = self._last_connect_address.strip()

        if not address:
            item = self.device_list.currentItem()
            if item is not None:
                info = item.data(Qt.ItemDataRole.UserRole)
                if isinstance(info, dict):
                    address = str(info.get("address", "")).strip()
                    if not name:
                        name = str(info.get("name", "")).strip()

        if not name and not address:
            return

        self._config.update_last_device(name=name or "Unknown", address=address)
        self._auto_target_name = name or self._auto_target_name
        self._auto_target_address = address or self._auto_target_address

    @staticmethod
    def _normalize_device_name(value: str) -> str:
        return "".join(ch for ch in value.lower() if ch.isalnum())

    def _apply_theme(self) -> None:
        self._root.setStyleSheet(
            """
            QWidget#ble_tab {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 #040914, stop:0.52 #071225, stop:1 #03070f);
            }
            QWidget#ble_tab QLabel {
                color: #a6bfd7;
                font-family: Bahnschrift;
                font-size: 10pt;
            }
            QFrame#hero_card {
                border: 1px solid #0b2236;
                border-radius: 10px;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #091528, stop:1 #071120);
            }
            QLabel#hero_title {
                color: #44e4ff;
                font-size: 13pt;
                font-weight: 700;
                letter-spacing: 1px;
            }
            QLabel#hero_subtitle {
                color: #8aa9c6;
            }
            QFrame#control_card,
            QFrame#status_strip,
            QFrame#list_card,
            QFrame#io_card {
                border: 1px solid #0b2236;
                border-radius: 10px;
                background-color: #071223;
            }
            QFrame#rx_frame,
            QFrame#tx_frame {
                border: 1px solid #102d46;
                border-radius: 8px;
                background-color: #081729;
            }
            QLabel#section_title,
            QLabel#list_title,
            QLabel#io_title,
            QLabel#rx_title,
            QLabel#tx_title {
                color: #5ddfff;
                font-weight: 700;
                letter-spacing: 1px;
            }
            QLabel#list_hint {
                color: #7f95ab;
            }
            QFrame#chip_state,
            QFrame#chip_device,
            QFrame#chip_service,
            QFrame#chip_rx,
            QFrame#chip_tx {
                border: 1px solid #15354f;
                border-radius: 7px;
                background-color: #061020;
            }
            QLabel#chip_key_state,
            QLabel#chip_key_device,
            QLabel#chip_key_service,
            QLabel#chip_key_rx,
            QLabel#chip_key_tx {
                color: #8fb3d2;
            }
            QLabel#conn_value,
            QLabel#device_value,
            QLabel#service_value,
            QLabel#rx_value,
            QLabel#tx_value {
                color: #48e7ff;
                font-weight: 700;
            }
            QLabel#config_summary {
                color: #78a6ca;
            }
            QPushButton {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #0a1729;
                color: #d0e7ff;
                min-height: 28px;
                padding: 2px 10px;
                font-family: Bahnschrift;
                font-size: 10pt;
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
            QPushButton[variant="ghost"] {
                border-color: #316485;
                color: #9ec6e4;
                background-color: #0b1a2e;
            }
            QLineEdit {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #061020;
                color: #b6d4ef;
                padding: 4px 8px;
                min-height: 24px;
                font-family: Consolas;
                font-size: 10pt;
            }
            QLineEdit:focus {
                border-color: #41caf4;
            }
            QListWidget#device_list {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #061020;
                alternate-background-color: #0a1a2e;
                color: #d8ecff;
                padding: 4px;
                font-family: Consolas;
                font-size: 10pt;
            }
            QListWidget#device_list::item {
                background-color: #061020;
                color: #d8ecff;
                padding: 4px 6px;
            }
            QListWidget#device_list::item:alternate {
                background-color: #0a1a2e;
                color: #cde4fb;
            }
            QListWidget#device_list::item:hover {
                background-color: #143250;
                color: #f4fbff;
            }
            QListWidget#device_list::item:selected {
                background-color: #1b4d78;
                color: #ffffff;
                border: 1px solid #56c5ff;
            }
            QPlainTextEdit#rx_view,
            QPlainTextEdit#tx_view {
                border: 1px solid #1f4d73;
                border-radius: 6px;
                background-color: #030a14;
                selection-background-color: #1a4f74;
                font-family: Consolas;
                font-size: 10pt;
            }
            QPlainTextEdit#rx_view {
                color: #69ff9a;
            }
            QPlainTextEdit#tx_view {
                color: #ffd88a;
            }
            """
        )

    def _bind_signals(self) -> None:
        self.scan_button.clicked.connect(self._on_scan_clicked)
        self.connect_button.clicked.connect(self._on_connect_clicked)
        self.disconnect_button.clicked.connect(self._ble.disconnect_device)
        self.apply_config_button.clicked.connect(self._on_apply_config_clicked)

        self.send_button.clicked.connect(self._on_send_clicked)
        self.tx_input.returnPressed.connect(self._on_send_clicked)
        self.clear_button.clicked.connect(self._on_clear_clicked)

        self.ping_button.clicked.connect(lambda: self._send_quick("PING"))
        self.get_status_button.clicked.connect(lambda: self._send_quick("GET_STATUS"))
        self.reboot_button.clicked.connect(lambda: self._send_quick("REBOOT"))

        self._ble.scan_started_signal.connect(self._on_scan_started)
        self._ble.scan_item_signal.connect(self._on_scan_item)
        self._ble.scan_finished_signal.connect(self._on_scan_finished)
        self._ble.connected_signal.connect(self._on_connected_changed)
        self._ble.rx_signal.connect(self._on_ble_rx)
        self._ble.tx_signal.connect(self._on_ble_tx)
        self._ble.log_signal.connect(self._on_ble_log)

    def _on_apply_config_clicked(self) -> None:
        self._apply_ble_config(notify=True)

    def _on_scan_clicked(self) -> None:
        self._auto_scan_requested = False
        self._auto_connect_pending = False
        self._apply_ble_config(notify=False)
        self.device_list.clear()
        filter_text = self.device_filter_edit.text().strip()
        self._append_sys(f"Start scanning. Filter: '{filter_text or 'ALL'}'")
        self._ble.scan(name_filter=filter_text, timeout_sec=6.0)

    def _on_connect_clicked(self) -> None:
        item = self.device_list.currentItem()
        if item is None:
            self._append_sys("Please select a device before CONNECT.")
            return

        info = item.data(Qt.ItemDataRole.UserRole)
        if not isinstance(info, dict):
            self._append_sys("Selected item has invalid data.")
            return

        address = str(info.get("address", ""))
        name = str(info.get("name", "Unknown"))
        if not address:
            self._append_sys("Selected item has no BLE address.")
            return

        self._auto_connect_pending = False
        self._connect_to_device(name=name, address=address)

    def _connect_to_device(self, name: str, address: str) -> None:
        clean_name = name.strip() or "Unknown"
        clean_address = address.strip()
        if not clean_address:
            return

        self._last_connect_name = clean_name
        self._last_connect_address = clean_address
        self._apply_ble_config(notify=False)
        self.device_value.setText(clean_name)
        self._refresh_status_strip()
        self._ble.connect_device(address=clean_address, name=clean_name)

    def _on_send_clicked(self) -> None:
        text = self.tx_input.text().strip()
        if not text:
            return

        if not self._is_connected:
            self._append_sys("Not connected. Connect first, then send text.")
            return

        self._ble.send_text(text)
        self.tx_input.clear()

    def _on_clear_clicked(self) -> None:
        self.tx_view.clear()
        self.rx_view.clear()

    def _send_quick(self, cmd: str) -> None:
        self.tx_input.setText(cmd)
        self._on_send_clicked()

    def _on_scan_started(self) -> None:
        self._is_scanning = True
        self.scan_button.setEnabled(False)
        self.scan_button.setText("SCANNING...")

    def _on_scan_item(self, name: str, address: str, rssi: int) -> None:
        display_name = name or "Unknown"
        signal_text = f"{rssi} dBm" if rssi > -999 else "--"
        item = QListWidgetItem(f"{display_name}  [{address}]  RSSI:{signal_text}")
        item.setData(
            Qt.ItemDataRole.UserRole,
            {"name": display_name, "address": address, "rssi": rssi},
        )
        self.device_list.addItem(item)

        if self.device_list.count() == 1:
            self.device_list.setCurrentItem(item)

    def _on_scan_finished(self, count: int) -> None:
        self._is_scanning = False
        self.scan_button.setEnabled(True)
        self.scan_button.setText("SEARCH")
        self._append_sys(f"Scan finished: {count} device(s).")

        if not self._auto_scan_requested or self._is_connected:
            return

        self._auto_scan_requested = False
        match_item = self._pick_auto_connect_item()
        if match_item is None:
            self._auto_connect_pending = False
            self._append_sys("Auto connect: no matching device in scan results.")
            return

        self.device_list.setCurrentItem(match_item)
        info = match_item.data(Qt.ItemDataRole.UserRole)
        if not isinstance(info, dict):
            self._auto_connect_pending = False
            self._append_sys("Auto connect: invalid scan result item.")
            return

        name = str(info.get("name", "Unknown"))
        address = str(info.get("address", "")).strip()
        if not address:
            self._auto_connect_pending = False
            self._append_sys("Auto connect: matched device has empty address.")
            return

        self._append_sys(f"Auto connect: matched {name} [{address}].")
        self._connect_to_device(name=name, address=address)

    def _on_connected_changed(self, connected: bool, device_name: str) -> None:
        self._is_connected = connected
        self.connect_button.setEnabled(not connected)
        self.disconnect_button.setEnabled(connected)
        self.send_button.setEnabled(connected)

        if connected:
            shown_name = device_name or self._last_connect_name or "N/A"
            self.device_value.setText(shown_name)
            self._auto_connect_pending = False
            self._auto_scan_requested = False
            self._save_last_connected_device(shown_name)
            self._append_sys(f"Connected to {shown_name}.")
        else:
            self.device_value.setText("N/A")
            self._append_sys("Disconnected.")

        self._refresh_status_strip()

    def _on_ble_rx(self, text: str) -> None:
        self._append_rx(text)

    def _on_ble_tx(self, text: str) -> None:
        self._append_tx(text)

    def _on_ble_log(self, text: str) -> None:
        plain = text.strip()
        if plain.startswith("TX >") or plain.startswith("RX <"):
            return
        self._append_sys(text)

    def _append_tx(self, text: str) -> None:
        stamp = QDateTime.currentDateTime().toString("HH:mm:ss")
        self.tx_view.appendPlainText(f"[{stamp}] TX > {text}")

    def _append_rx(self, text: str) -> None:
        stamp = QDateTime.currentDateTime().toString("HH:mm:ss")
        self.rx_view.appendPlainText(f"[{stamp}] RX < {text}")

    def _append_sys(self, text: str) -> None:
        stamp = QDateTime.currentDateTime().toString("HH:mm:ss")
        self.rx_view.appendPlainText(f"[{stamp}] SYS < {text}")

    def _apply_ble_config(self, notify: bool) -> None:
        self._ble.configure_uuids(
            service_uuid=self.service_uuid_edit.text(),
            rx_uuid=self.rx_uuid_edit.text(),
            tx_uuid=self.tx_uuid_edit.text(),
        )
        self._refresh_status_strip()
        if notify:
            self._append_sys("BLE UUID config applied.")

    def _refresh_status_strip(self) -> None:
        state = "Connected" if self._is_connected else "Disconnected"
        self.conn_value.setText(state)
        if not self.device_value.text().strip() or self.device_value.text().strip() == "--":
            self.device_value.setText("N/A")

        self.service_value.setText(self.service_uuid_edit.text().strip() or "--")
        self.rx_value.setText(self.rx_uuid_edit.text().strip() or "--")
        self.tx_value.setText(self.tx_uuid_edit.text().strip() or "--")

        self.config_summary.setText(
            "Service={service} | RX={rx} | TX={tx}".format(
                service=self.service_value.text(),
                rx=self.rx_value.text(),
                tx=self.tx_value.text(),
            )
        )
