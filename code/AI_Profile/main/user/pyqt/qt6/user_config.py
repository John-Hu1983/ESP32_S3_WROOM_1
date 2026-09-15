from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path
from typing import Any


class UserConfigStore:
    _DEFAULT_DATA: dict[str, Any] = {
        "ble": {
            "last_device_name": "",
            "last_device_address": "",
            "auto_connect_last_device": True,
        }
    }

    def __init__(self, file_path: Path | None = None) -> None:
        self._path = file_path or Path(__file__).with_name("user_config.json")
        self._data: dict[str, Any] = {}
        self._load()

    @property
    def last_device_name(self) -> str:
        ble = self._data.get("ble", {})
        if not isinstance(ble, dict):
            return ""
        return str(ble.get("last_device_name", "")).strip()

    @property
    def last_device_address(self) -> str:
        ble = self._data.get("ble", {})
        if not isinstance(ble, dict):
            return ""
        return str(ble.get("last_device_address", "")).strip()

    @property
    def auto_connect_last_device(self) -> bool:
        ble = self._data.get("ble", {})
        if not isinstance(ble, dict):
            return True

        value = ble.get("auto_connect_last_device", True)
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, float)):
            return bool(value)

        text = str(value).strip().lower()
        return text in {"1", "true", "yes", "on"}

    def update_last_device(self, name: str, address: str) -> None:
        ble = self._ensure_ble_section()
        ble["last_device_name"] = name.strip()
        ble["last_device_address"] = address.strip()
        self._save()

    def _ensure_ble_section(self) -> dict[str, Any]:
        ble = self._data.get("ble")
        if isinstance(ble, dict):
            return ble

        self._data["ble"] = {}
        return self._data["ble"]

    def _load(self) -> None:
        if not self._path.exists():
            self._data = deepcopy(self._DEFAULT_DATA)
            self._save()
            return

        parsed: dict[str, Any] = {}
        try:
            content = self._path.read_text(encoding="utf-8")
            raw = json.loads(content)
            if isinstance(raw, dict):
                parsed = raw
        except (OSError, json.JSONDecodeError):
            parsed = {}

        self._data = self._merge_defaults(parsed)
        self._save()

    def _merge_defaults(self, parsed: dict[str, Any]) -> dict[str, Any]:
        merged = deepcopy(self._DEFAULT_DATA)
        ble = parsed.get("ble")
        if isinstance(ble, dict):
            for key in (
                "last_device_name",
                "last_device_address",
                "auto_connect_last_device",
            ):
                if key in ble:
                    merged["ble"][key] = ble[key]
        return merged

    def _save(self) -> None:
        try:
            text = json.dumps(self._data, indent=2, ensure_ascii=True)
            self._path.write_text(text + "\n", encoding="utf-8")
        except OSError:
            return
