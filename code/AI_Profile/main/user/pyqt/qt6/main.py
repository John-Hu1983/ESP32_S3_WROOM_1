import sys
from pathlib import Path

from PyQt6 import uic
from PyQt6.QtGui import QCloseEvent, QFont
from PyQt6.QtWidgets import QApplication, QMainWindow, QTabWidget, QWidget

from ble_manager import BleManager
from ble_tab import BleTabController
from steering_tab import SteeringTabController


def _set_windows_title_bar_dark_mode(window: QMainWindow) -> None:
    if sys.platform != "win32":
        return

    import ctypes

    try:
        dwm = ctypes.windll.dwmapi
    except (AttributeError, OSError):
        return

    hwnd = int(window.winId())
    enabled = ctypes.c_int(1)
    enabled_size = ctypes.c_uint(ctypes.sizeof(enabled))

    # 20 is used by recent Windows builds; 19 works on some older builds.
    for attr in (20, 19):
        result = dwm.DwmSetWindowAttribute(
            ctypes.c_void_p(hwnd),
            ctypes.c_uint(attr),
            ctypes.byref(enabled),
            enabled_size,
        )
        if result == 0:
            return


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.ble_manager = BleManager()
        self.ble_tab_controller: BleTabController | None = None
        self.steering_tab_controller: SteeringTabController | None = None
        ui_path = Path(__file__).with_name("total.ui")
        uic.loadUi(str(ui_path), self)
        self.setWindowTitle("BLE - author: John Hu")
        self._init_tabs()

    def _init_tabs(self) -> None:
        tab_widget = self.findChild(QTabWidget, "tabWidget")
        if tab_widget is None:
            return

        ble_tab_root = self.findChild(QWidget, "ble_tab")
        if ble_tab_root is None and tab_widget.count() > 0:
            ble_tab_root = tab_widget.widget(0)

        if ble_tab_root is not None:
            self.ble_tab_controller = BleTabController(self.ble_manager, ble_tab_root)

        steering_tab_root = self.findChild(QWidget, "steering_tab")
        if steering_tab_root is None and tab_widget.count() > 1:
            steering_tab_root = tab_widget.widget(1)

        if steering_tab_root is not None:
            self.steering_tab_controller = SteeringTabController(
                self.ble_manager, steering_tab_root
            )

        if tab_widget.count() > 0:
            tab_widget.setTabText(0, "BLE Config")
            tab_widget.setCurrentIndex(0)

        if tab_widget.count() > 1:
            tab_widget.setTabText(1, "Steering Debug")

    def closeEvent(self, event: QCloseEvent) -> None:
        self.ble_manager.shutdown()
        super().closeEvent(event)


def main() -> None:
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setFont(QFont("Bahnschrift", 10))
    window = MainWindow()
    window.show()
    _set_windows_title_bar_dark_mode(window)
    exit_code = 0
    try:
        exit_code = app.exec()
    except KeyboardInterrupt:
        # Running from terminal may receive Ctrl+C; exit quietly.
        try:
            window.close()
        except RuntimeError:
            pass
        exit_code = 0
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
