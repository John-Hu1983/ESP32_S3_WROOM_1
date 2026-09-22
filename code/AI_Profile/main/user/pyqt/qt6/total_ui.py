# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'total.ui'
##
## Created by: Qt User Interface Compiler version 6.5.2
##
## WARNING! All changes made in this file will be lost when recompiling UI file!
################################################################################

from PySide6.QtCore import (QCoreApplication, QDate, QDateTime, QLocale,
    QMetaObject, QObject, QPoint, QRect,
    QSize, QTime, QUrl, Qt)
from PySide6.QtGui import (QBrush, QColor, QConicalGradient, QCursor,
    QFont, QFontDatabase, QGradient, QIcon,
    QImage, QKeySequence, QLinearGradient, QPainter,
    QPalette, QPixmap, QRadialGradient, QTransform)
from PySide6.QtWidgets import (QApplication, QComboBox, QDial, QDoubleSpinBox,
    QFrame, QGridLayout, QHBoxLayout, QLabel,
    QLineEdit, QListWidget, QListWidgetItem, QMainWindow,
    QMenuBar, QPlainTextEdit, QPushButton, QSizePolicy,
    QSpacerItem, QSplitter, QStatusBar, QTabWidget,
    QVBoxLayout, QWidget)

class Ui_MainWindow(object):
    def setupUi(self, MainWindow):
        if not MainWindow.objectName():
            MainWindow.setObjectName(u"MainWindow")
        MainWindow.resize(1200, 860)
        MainWindow.setStyleSheet(u"QMainWindow {\n"
"    background-color: #050b14;\n"
"}\n"
"QTabWidget::pane {\n"
"    border: 1px solid #0b2236;\n"
"    top: -1px;\n"
"    background-color: #050b14;\n"
"}\n"
"QTabBar::tab {\n"
"    background-color: #0b1728;\n"
"    color: #9ab8d3;\n"
"    border: 1px solid #12314d;\n"
"    border-bottom-color: #0b2236;\n"
"    padding: 6px 14px;\n"
"    margin-right: 2px;\n"
"}\n"
"QTabBar::tab:selected {\n"
"    background-color: #0f2238;\n"
"    color: #e1f1ff;\n"
"    border-color: #1b4a72;\n"
"}\n"
"QTabBar::tab:hover:!selected {\n"
"    background-color: #112840;\n"
"    color: #cce6ff;\n"
"}\n"
"QWidget#steering_tab {\n"
"    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,\n"
"        stop:0 #050c17, stop:0.55 #071428, stop:1 #040912);\n"
"}\n"
"QWidget#steering_tab QLabel {\n"
"    color: #afc1d4;\n"
"    font-family: Bahnschrift;\n"
"    font-size: 10pt;\n"
"}\n"
"QWidget#steering_tab QFrame#steering_hero_card {\n"
"    border: 1px solid #10314b;\n"
"    border-radius: 12px;\n"
"    background: "
                        "qlineargradient(x1:0, y1:0, x2:1, y2:0,\n"
"        stop:0 #10263f, stop:1 #0a1728);\n"
"}\n"
"QWidget#steering_tab QLabel#steering_hero_title {\n"
"    color: #63e6ff;\n"
"    font-size: 14pt;\n"
"    font-weight: 700;\n"
"    letter-spacing: 1px;\n"
"}\n"
"QWidget#steering_tab QLabel#steering_hero_subtitle {\n"
"    color: #8fb1cf;\n"
"}\n"
"QWidget#steering_tab QFrame#scope_card,\n"
"QWidget#steering_tab QFrame#pid_card,\n"
"QWidget#steering_tab QFrame#setpoint_card,\n"
"QWidget#steering_tab QFrame#runtime_card {\n"
"    border: 1px solid #0f2b44;\n"
"    border-radius: 11px;\n"
"    background-color: #081427;\n"
"}\n"
"QWidget#steering_tab QLabel#scope_title,\n"
"QWidget#steering_tab QLabel#pid_title,\n"
"QWidget#steering_tab QLabel#setpoint_title,\n"
"QWidget#steering_tab QLabel#runtime_title,\n"
"QWidget#steering_tab QLabel#telemetry_raw_title {\n"
"    color: #57dbff;\n"
"    font-size: 11pt;\n"
"    font-weight: 700;\n"
"    letter-spacing: 1px;\n"
"}\n"
"QWidget#steering_tab QLabel#scope_hint,\n"
"QWi"
                        "dget#steering_tab QLabel#runtime_hint,\n"
"QWidget#steering_tab QLabel#setpoint_hint {\n"
"    color: #7d97af;\n"
"}\n"
"QWidget#steering_tab QLabel#scope_live_badge {\n"
"    color: #7fffcf;\n"
"    border: 1px solid #20543f;\n"
"    border-radius: 7px;\n"
"    background-color: #0a3025;\n"
"    padding: 2px 8px;\n"
"    font-weight: 700;\n"
"}\n"
"QWidget#steering_tab QFrame#scope_host {\n"
"    border: 1px solid #1a3a55;\n"
"    border-radius: 8px;\n"
"    background-color: #040b16;\n"
"}\n"
"QWidget#steering_tab QPlainTextEdit#telemetry_raw_view {\n"
"    border: 1px solid #1e425f;\n"
"    border-radius: 8px;\n"
"    background-color: #030913;\n"
"    color: #9fe7c0;\n"
"    selection-background-color: #184e73;\n"
"    font-family: Consolas;\n"
"    font-size: 10pt;\n"
"}\n"
"QWidget#steering_tab QPushButton {\n"
"    border: 1px solid #20506f;\n"
"    border-radius: 7px;\n"
"    background-color: #0d2136;\n"
"    color: #d2e6fb;\n"
"    min-height: 28px;\n"
"    padding: 2px 10px;\n"
"}\n"
"QWidget#steeri"
                        "ng_tab QPushButton:hover {\n"
"    border-color: #2f88be;\n"
"    background-color: #12304b;\n"
"}\n"
"QWidget#steering_tab QPushButton:pressed {\n"
"    background-color: #153a5d;\n"
"}\n"
"QWidget#steering_tab QPushButton:disabled {\n"
"    color: #5f7285;\n"
"    border-color: #213246;\n"
"    background-color: #0a1624;\n"
"}\n"
"QWidget#steering_tab QPushButton#setpoint_send_button,\n"
"QWidget#steering_tab QPushButton#pid_apply_button,\n"
"QWidget#steering_tab QPushButton#motor_enable_button,\n"
"QWidget#steering_tab QPushButton#telemetry_send_button {\n"
"    border-color: #2b8fbe;\n"
"    color: #73ebff;\n"
"    background-color: #10344f;\n"
"}\n"
"QWidget#steering_tab QPushButton#motor_stop_button {\n"
"    border-color: #a65262;\n"
"    color: #ffc0cb;\n"
"    background-color: #311723;\n"
"}\n"
"QWidget#steering_tab QDoubleSpinBox,\n"
"QWidget#steering_tab QComboBox,\n"
"QWidget#steering_tab QLineEdit {\n"
"    border: 1px solid #1f4f74;\n"
"    border-radius: 7px;\n"
"    background-color: #061021;\n"
""
                        "    color: #b9d6ef;\n"
"    min-height: 26px;\n"
"    padding: 2px 8px;\n"
"    font-family: Consolas;\n"
"}\n"
"QWidget#steering_tab QDoubleSpinBox:focus,\n"
"QWidget#steering_tab QComboBox:focus,\n"
"QWidget#steering_tab QLineEdit:focus {\n"
"    border-color: #46d4ff;\n"
"}\n"
"QWidget#steering_tab QDial#angle_knob {\n"
"    background-color: #07182d;\n"
"    border: 1px solid #1a4366;\n"
"    border-radius: 68px;\n"
"}\n"
"QWidget#steering_tab QLabel#knob_value_lab,\n"
"QWidget#steering_tab QLabel#rt_conn_value,\n"
"QWidget#steering_tab QLabel#rt_set_value,\n"
"QWidget#steering_tab QLabel#rt_fb_value,\n"
"QWidget#steering_tab QLabel#rt_err_value,\n"
"QWidget#steering_tab QLabel#rt_pwm_value,\n"
"QWidget#steering_tab QLabel#rt_rpm_value,\n"
"QWidget#steering_tab QLabel#rt_vbus_value {\n"
"    color: #7af0ff;\n"
"    font-weight: 700;\n"
"}")
        self.centralwidget = QWidget(MainWindow)
        self.centralwidget.setObjectName(u"centralwidget")
        self.verticalLayout = QVBoxLayout(self.centralwidget)
        self.verticalLayout.setObjectName(u"verticalLayout")
        self.tabWidget = QTabWidget(self.centralwidget)
        self.tabWidget.setObjectName(u"tabWidget")
        self.ble_tab = QWidget()
        self.ble_tab.setObjectName(u"ble_tab")
        self.ble_root_layout = QVBoxLayout(self.ble_tab)
        self.ble_root_layout.setSpacing(10)
        self.ble_root_layout.setObjectName(u"ble_root_layout")
        self.ble_root_layout.setContentsMargins(12, 12, 12, 12)
        self.hero_card = QFrame(self.ble_tab)
        self.hero_card.setObjectName(u"hero_card")
        self.hero_layout = QVBoxLayout(self.hero_card)
        self.hero_layout.setSpacing(2)
        self.hero_layout.setObjectName(u"hero_layout")
        self.hero_layout.setContentsMargins(14, 10, 14, 10)
        self.hero_title_row = QHBoxLayout()
        self.hero_title_row.setSpacing(8)
        self.hero_title_row.setObjectName(u"hero_title_row")
        self.hero_title = QLabel(self.hero_card)
        self.hero_title.setObjectName(u"hero_title")

        self.hero_title_row.addWidget(self.hero_title)

        self.hero_title_row_spacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.hero_title_row.addItem(self.hero_title_row_spacer)

        self.ble_conn_led = QLabel(self.hero_card)
        self.ble_conn_led.setObjectName(u"ble_conn_led")
        self.ble_conn_led.setMinimumSize(QSize(28, 28))
        self.ble_conn_led.setMaximumSize(QSize(28, 28))
        self.ble_conn_led.setStyleSheet(u"QLabel#ble_conn_led {\n"
"    background-color: #090909;\n"
"    border: 1px solid #2a3644;\n"
"    border-radius: 14px;\n"
"}")

        self.hero_title_row.addWidget(self.ble_conn_led)


        self.hero_layout.addLayout(self.hero_title_row)

        self.hero_subtitle = QLabel(self.hero_card)
        self.hero_subtitle.setObjectName(u"hero_subtitle")

        self.hero_layout.addWidget(self.hero_subtitle)


        self.ble_root_layout.addWidget(self.hero_card)

        self.control_card = QFrame(self.ble_tab)
        self.control_card.setObjectName(u"control_card")
        self.control_layout = QVBoxLayout(self.control_card)
        self.control_layout.setSpacing(8)
        self.control_layout.setObjectName(u"control_layout")
        self.control_layout.setContentsMargins(12, 10, 12, 10)
        self.section_title = QLabel(self.control_card)
        self.section_title.setObjectName(u"section_title")

        self.control_layout.addWidget(self.section_title)

        self.control_grid = QGridLayout()
        self.control_grid.setSpacing(8)
        self.control_grid.setObjectName(u"control_grid")
        self.device_filter_lab = QLabel(self.control_card)
        self.device_filter_lab.setObjectName(u"device_filter_lab")

        self.control_grid.addWidget(self.device_filter_lab, 0, 0, 1, 1)

        self.device_filter_edit = QLineEdit(self.control_card)
        self.device_filter_edit.setObjectName(u"device_filter_edit")

        self.control_grid.addWidget(self.device_filter_edit, 0, 1, 1, 3)

        self.action_row = QHBoxLayout()
        self.action_row.setSpacing(8)
        self.action_row.setObjectName(u"action_row")
        self.scan_button = QPushButton(self.control_card)
        self.scan_button.setObjectName(u"scan_button")
        self.scan_button.setMinimumSize(QSize(150, 0))
        self.scan_button.setMaximumSize(QSize(150, 16777215))

        self.action_row.addWidget(self.scan_button)

        self.connect_button = QPushButton(self.control_card)
        self.connect_button.setObjectName(u"connect_button")
        self.connect_button.setMinimumSize(QSize(120, 0))
        self.connect_button.setMaximumSize(QSize(120, 16777215))

        self.action_row.addWidget(self.connect_button)

        self.disconnect_button = QPushButton(self.control_card)
        self.disconnect_button.setObjectName(u"disconnect_button")
        self.disconnect_button.setMinimumSize(QSize(140, 0))
        self.disconnect_button.setMaximumSize(QSize(140, 16777215))

        self.action_row.addWidget(self.disconnect_button)

        self.apply_config_button = QPushButton(self.control_card)
        self.apply_config_button.setObjectName(u"apply_config_button")
        self.apply_config_button.setMinimumSize(QSize(170, 0))
        self.apply_config_button.setMaximumSize(QSize(170, 16777215))

        self.action_row.addWidget(self.apply_config_button)

        self.action_row_spacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.action_row.addItem(self.action_row_spacer)


        self.control_grid.addLayout(self.action_row, 0, 4, 1, 3)

        self.service_uuid_lab = QLabel(self.control_card)
        self.service_uuid_lab.setObjectName(u"service_uuid_lab")

        self.control_grid.addWidget(self.service_uuid_lab, 1, 0, 1, 1)

        self.service_uuid_edit = QLineEdit(self.control_card)
        self.service_uuid_edit.setObjectName(u"service_uuid_edit")

        self.control_grid.addWidget(self.service_uuid_edit, 1, 1, 1, 2)

        self.rx_uuid_lab = QLabel(self.control_card)
        self.rx_uuid_lab.setObjectName(u"rx_uuid_lab")

        self.control_grid.addWidget(self.rx_uuid_lab, 1, 3, 1, 1)

        self.rx_uuid_edit = QLineEdit(self.control_card)
        self.rx_uuid_edit.setObjectName(u"rx_uuid_edit")

        self.control_grid.addWidget(self.rx_uuid_edit, 1, 4, 1, 1)

        self.tx_uuid_lab = QLabel(self.control_card)
        self.tx_uuid_lab.setObjectName(u"tx_uuid_lab")

        self.control_grid.addWidget(self.tx_uuid_lab, 1, 5, 1, 1)

        self.tx_uuid_edit = QLineEdit(self.control_card)
        self.tx_uuid_edit.setObjectName(u"tx_uuid_edit")

        self.control_grid.addWidget(self.tx_uuid_edit, 1, 6, 1, 1)

        self.config_row = QHBoxLayout()
        self.config_row.setObjectName(u"config_row")

        self.control_grid.addLayout(self.config_row, 2, 4, 1, 3)


        self.control_layout.addLayout(self.control_grid)


        self.ble_root_layout.addWidget(self.control_card)

        self.status_strip = QFrame(self.ble_tab)
        self.status_strip.setObjectName(u"status_strip")
        self.status_layout = QVBoxLayout(self.status_strip)
        self.status_layout.setSpacing(6)
        self.status_layout.setObjectName(u"status_layout")
        self.status_layout.setContentsMargins(12, 8, 12, 8)
        self.chips_row = QHBoxLayout()
        self.chips_row.setSpacing(8)
        self.chips_row.setObjectName(u"chips_row")
        self.chip_state = QFrame(self.status_strip)
        self.chip_state.setObjectName(u"chip_state")
        self.chip_state_layout = QHBoxLayout(self.chip_state)
        self.chip_state_layout.setSpacing(6)
        self.chip_state_layout.setObjectName(u"chip_state_layout")
        self.chip_state_layout.setContentsMargins(8, 4, 8, 4)
        self.chip_key_state = QLabel(self.chip_state)
        self.chip_key_state.setObjectName(u"chip_key_state")

        self.chip_state_layout.addWidget(self.chip_key_state)

        self.conn_value = QLabel(self.chip_state)
        self.conn_value.setObjectName(u"conn_value")

        self.chip_state_layout.addWidget(self.conn_value)


        self.chips_row.addWidget(self.chip_state)

        self.chip_device = QFrame(self.status_strip)
        self.chip_device.setObjectName(u"chip_device")
        self.chip_device_layout = QHBoxLayout(self.chip_device)
        self.chip_device_layout.setSpacing(6)
        self.chip_device_layout.setObjectName(u"chip_device_layout")
        self.chip_device_layout.setContentsMargins(8, 4, 8, 4)
        self.chip_key_device = QLabel(self.chip_device)
        self.chip_key_device.setObjectName(u"chip_key_device")

        self.chip_device_layout.addWidget(self.chip_key_device)

        self.device_value = QLabel(self.chip_device)
        self.device_value.setObjectName(u"device_value")

        self.chip_device_layout.addWidget(self.device_value)


        self.chips_row.addWidget(self.chip_device)

        self.chip_service = QFrame(self.status_strip)
        self.chip_service.setObjectName(u"chip_service")
        self.chip_service_layout = QHBoxLayout(self.chip_service)
        self.chip_service_layout.setSpacing(6)
        self.chip_service_layout.setObjectName(u"chip_service_layout")
        self.chip_service_layout.setContentsMargins(8, 4, 8, 4)
        self.chip_key_service = QLabel(self.chip_service)
        self.chip_key_service.setObjectName(u"chip_key_service")

        self.chip_service_layout.addWidget(self.chip_key_service)

        self.service_value = QLabel(self.chip_service)
        self.service_value.setObjectName(u"service_value")

        self.chip_service_layout.addWidget(self.service_value)


        self.chips_row.addWidget(self.chip_service)

        self.chip_rx = QFrame(self.status_strip)
        self.chip_rx.setObjectName(u"chip_rx")
        self.chip_rx_layout = QHBoxLayout(self.chip_rx)
        self.chip_rx_layout.setSpacing(6)
        self.chip_rx_layout.setObjectName(u"chip_rx_layout")
        self.chip_rx_layout.setContentsMargins(8, 4, 8, 4)
        self.chip_key_rx = QLabel(self.chip_rx)
        self.chip_key_rx.setObjectName(u"chip_key_rx")

        self.chip_rx_layout.addWidget(self.chip_key_rx)

        self.rx_value = QLabel(self.chip_rx)
        self.rx_value.setObjectName(u"rx_value")

        self.chip_rx_layout.addWidget(self.rx_value)


        self.chips_row.addWidget(self.chip_rx)

        self.chip_tx = QFrame(self.status_strip)
        self.chip_tx.setObjectName(u"chip_tx")
        self.chip_tx_layout = QHBoxLayout(self.chip_tx)
        self.chip_tx_layout.setSpacing(6)
        self.chip_tx_layout.setObjectName(u"chip_tx_layout")
        self.chip_tx_layout.setContentsMargins(8, 4, 8, 4)
        self.chip_key_tx = QLabel(self.chip_tx)
        self.chip_key_tx.setObjectName(u"chip_key_tx")

        self.chip_tx_layout.addWidget(self.chip_key_tx)

        self.tx_value = QLabel(self.chip_tx)
        self.tx_value.setObjectName(u"tx_value")

        self.chip_tx_layout.addWidget(self.tx_value)


        self.chips_row.addWidget(self.chip_tx)

        self.chips_spacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.chips_row.addItem(self.chips_spacer)


        self.status_layout.addLayout(self.chips_row)

        self.config_summary = QLabel(self.status_strip)
        self.config_summary.setObjectName(u"config_summary")

        self.status_layout.addWidget(self.config_summary)


        self.ble_root_layout.addWidget(self.status_strip)

        self.body_splitter = QSplitter(self.ble_tab)
        self.body_splitter.setObjectName(u"body_splitter")
        self.body_splitter.setOrientation(Qt.Horizontal)
        self.body_splitter.setChildrenCollapsible(False)
        self.list_card = QFrame(self.body_splitter)
        self.list_card.setObjectName(u"list_card")
        self.list_layout = QVBoxLayout(self.list_card)
        self.list_layout.setSpacing(8)
        self.list_layout.setObjectName(u"list_layout")
        self.list_layout.setContentsMargins(12, 10, 12, 10)
        self.list_title = QLabel(self.list_card)
        self.list_title.setObjectName(u"list_title")

        self.list_layout.addWidget(self.list_title)

        self.device_list = QListWidget(self.list_card)
        self.device_list.setObjectName(u"device_list")
        self.device_list.setMinimumSize(QSize(270, 0))
        self.device_list.setAlternatingRowColors(True)

        self.list_layout.addWidget(self.device_list)

        self.list_hint = QLabel(self.list_card)
        self.list_hint.setObjectName(u"list_hint")

        self.list_layout.addWidget(self.list_hint)

        self.body_splitter.addWidget(self.list_card)
        self.io_card = QFrame(self.body_splitter)
        self.io_card.setObjectName(u"io_card")
        self.io_layout = QVBoxLayout(self.io_card)
        self.io_layout.setSpacing(8)
        self.io_layout.setObjectName(u"io_layout")
        self.io_layout.setContentsMargins(12, 10, 12, 10)
        self.io_title = QLabel(self.io_card)
        self.io_title.setObjectName(u"io_title")

        self.io_layout.addWidget(self.io_title)

        self.rx_tx_splitter = QSplitter(self.io_card)
        self.rx_tx_splitter.setObjectName(u"rx_tx_splitter")
        self.rx_tx_splitter.setOrientation(Qt.Vertical)
        self.rx_tx_splitter.setChildrenCollapsible(False)
        self.rx_frame = QFrame(self.rx_tx_splitter)
        self.rx_frame.setObjectName(u"rx_frame")
        self.rx_layout = QVBoxLayout(self.rx_frame)
        self.rx_layout.setSpacing(6)
        self.rx_layout.setObjectName(u"rx_layout")
        self.rx_layout.setContentsMargins(8, 8, 8, 8)
        self.rx_title = QLabel(self.rx_frame)
        self.rx_title.setObjectName(u"rx_title")

        self.rx_layout.addWidget(self.rx_title)

        self.rx_view = QPlainTextEdit(self.rx_frame)
        self.rx_view.setObjectName(u"rx_view")
        self.rx_view.setReadOnly(True)

        self.rx_layout.addWidget(self.rx_view)

        self.rx_tx_splitter.addWidget(self.rx_frame)
        self.tx_frame = QFrame(self.rx_tx_splitter)
        self.tx_frame.setObjectName(u"tx_frame")
        self.tx_layout = QVBoxLayout(self.tx_frame)
        self.tx_layout.setSpacing(6)
        self.tx_layout.setObjectName(u"tx_layout")
        self.tx_layout.setContentsMargins(8, 8, 8, 8)
        self.tx_title = QLabel(self.tx_frame)
        self.tx_title.setObjectName(u"tx_title")

        self.tx_layout.addWidget(self.tx_title)

        self.tx_view = QPlainTextEdit(self.tx_frame)
        self.tx_view.setObjectName(u"tx_view")
        self.tx_view.setReadOnly(True)

        self.tx_layout.addWidget(self.tx_view)

        self.rx_tx_splitter.addWidget(self.tx_frame)

        self.io_layout.addWidget(self.rx_tx_splitter)

        self.send_row = QHBoxLayout()
        self.send_row.setSpacing(8)
        self.send_row.setObjectName(u"send_row")
        self.tx_input = QLineEdit(self.io_card)
        self.tx_input.setObjectName(u"tx_input")

        self.send_row.addWidget(self.tx_input)

        self.send_button = QPushButton(self.io_card)
        self.send_button.setObjectName(u"send_button")

        self.send_row.addWidget(self.send_button)

        self.clear_button = QPushButton(self.io_card)
        self.clear_button.setObjectName(u"clear_button")

        self.send_row.addWidget(self.clear_button)


        self.io_layout.addLayout(self.send_row)

        self.quick_row = QHBoxLayout()
        self.quick_row.setSpacing(8)
        self.quick_row.setObjectName(u"quick_row")
        self.quick_cmd_lab = QLabel(self.io_card)
        self.quick_cmd_lab.setObjectName(u"quick_cmd_lab")

        self.quick_row.addWidget(self.quick_cmd_lab)

        self.ping_button = QPushButton(self.io_card)
        self.ping_button.setObjectName(u"ping_button")

        self.quick_row.addWidget(self.ping_button)

        self.get_status_button = QPushButton(self.io_card)
        self.get_status_button.setObjectName(u"get_status_button")

        self.quick_row.addWidget(self.get_status_button)

        self.reboot_button = QPushButton(self.io_card)
        self.reboot_button.setObjectName(u"reboot_button")

        self.quick_row.addWidget(self.reboot_button)

        self.quick_row_spacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.quick_row.addItem(self.quick_row_spacer)


        self.io_layout.addLayout(self.quick_row)

        self.body_splitter.addWidget(self.io_card)

        self.ble_root_layout.addWidget(self.body_splitter)

        self.tabWidget.addTab(self.ble_tab, "")
        self.steering_tab = QWidget()
        self.steering_tab.setObjectName(u"steering_tab")
        self.steering_root_layout = QVBoxLayout(self.steering_tab)
        self.steering_root_layout.setSpacing(10)
        self.steering_root_layout.setObjectName(u"steering_root_layout")
        self.steering_root_layout.setContentsMargins(12, 12, 12, 12)
        self.steering_hero_card = QFrame(self.steering_tab)
        self.steering_hero_card.setObjectName(u"steering_hero_card")
        self.steering_hero_layout = QVBoxLayout(self.steering_hero_card)
        self.steering_hero_layout.setSpacing(2)
        self.steering_hero_layout.setObjectName(u"steering_hero_layout")
        self.steering_hero_layout.setContentsMargins(14, 10, 14, 10)
        self.steering_hero_title = QLabel(self.steering_hero_card)
        self.steering_hero_title.setObjectName(u"steering_hero_title")

        self.steering_hero_layout.addWidget(self.steering_hero_title)

        self.steering_hero_subtitle = QLabel(self.steering_hero_card)
        self.steering_hero_subtitle.setObjectName(u"steering_hero_subtitle")

        self.steering_hero_layout.addWidget(self.steering_hero_subtitle)


        self.steering_root_layout.addWidget(self.steering_hero_card)

        self.steering_vertical_splitter = QSplitter(self.steering_tab)
        self.steering_vertical_splitter.setObjectName(u"steering_vertical_splitter")
        self.steering_vertical_splitter.setOrientation(Qt.Vertical)
        self.steering_vertical_splitter.setChildrenCollapsible(False)
        self.scope_card = QFrame(self.steering_vertical_splitter)
        self.scope_card.setObjectName(u"scope_card")
        self.scope_card_layout = QVBoxLayout(self.scope_card)
        self.scope_card_layout.setSpacing(8)
        self.scope_card_layout.setObjectName(u"scope_card_layout")
        self.scope_card_layout.setContentsMargins(12, 10, 12, 10)
        self.scope_header_row = QHBoxLayout()
        self.scope_header_row.setSpacing(8)
        self.scope_header_row.setObjectName(u"scope_header_row")
        self.scope_title = QLabel(self.scope_card)
        self.scope_title.setObjectName(u"scope_title")

        self.scope_header_row.addWidget(self.scope_title)

        self.scope_hint = QLabel(self.scope_card)
        self.scope_hint.setObjectName(u"scope_hint")

        self.scope_header_row.addWidget(self.scope_hint)

        self.scope_header_spacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.scope_header_row.addItem(self.scope_header_spacer)

        self.scope_window_lab = QLabel(self.scope_card)
        self.scope_window_lab.setObjectName(u"scope_window_lab")

        self.scope_header_row.addWidget(self.scope_window_lab)

        self.scope_window_combo = QComboBox(self.scope_card)
        self.scope_window_combo.addItem("")
        self.scope_window_combo.addItem("")
        self.scope_window_combo.addItem("")
        self.scope_window_combo.addItem("")
        self.scope_window_combo.setObjectName(u"scope_window_combo")

        self.scope_header_row.addWidget(self.scope_window_combo)

        self.scope_pause_button = QPushButton(self.scope_card)
        self.scope_pause_button.setObjectName(u"scope_pause_button")

        self.scope_header_row.addWidget(self.scope_pause_button)

        self.scope_clear_button = QPushButton(self.scope_card)
        self.scope_clear_button.setObjectName(u"scope_clear_button")

        self.scope_header_row.addWidget(self.scope_clear_button)

        self.scope_live_badge = QLabel(self.scope_card)
        self.scope_live_badge.setObjectName(u"scope_live_badge")

        self.scope_header_row.addWidget(self.scope_live_badge)


        self.scope_card_layout.addLayout(self.scope_header_row)

        self.scope_host = QFrame(self.scope_card)
        self.scope_host.setObjectName(u"scope_host")
        self.scope_host_layout = QVBoxLayout(self.scope_host)
        self.scope_host_layout.setSpacing(0)
        self.scope_host_layout.setObjectName(u"scope_host_layout")
        self.scope_host_layout.setContentsMargins(0, 0, 0, 0)

        self.scope_card_layout.addWidget(self.scope_host)

        self.steering_vertical_splitter.addWidget(self.scope_card)
        self.steering_bottom_splitter = QSplitter(self.steering_vertical_splitter)
        self.steering_bottom_splitter.setObjectName(u"steering_bottom_splitter")
        self.steering_bottom_splitter.setOrientation(Qt.Horizontal)
        self.steering_bottom_splitter.setChildrenCollapsible(False)
        self.control_column = QWidget(self.steering_bottom_splitter)
        self.control_column.setObjectName(u"control_column")
        self.control_column_layout = QVBoxLayout(self.control_column)
        self.control_column_layout.setSpacing(10)
        self.control_column_layout.setObjectName(u"control_column_layout")
        self.control_column_layout.setContentsMargins(0, 0, 0, 0)
        self.pid_card = QFrame(self.control_column)
        self.pid_card.setObjectName(u"pid_card")
        self.pid_card_layout = QVBoxLayout(self.pid_card)
        self.pid_card_layout.setSpacing(8)
        self.pid_card_layout.setObjectName(u"pid_card_layout")
        self.pid_card_layout.setContentsMargins(12, 10, 12, 10)
        self.pid_title = QLabel(self.pid_card)
        self.pid_title.setObjectName(u"pid_title")

        self.pid_card_layout.addWidget(self.pid_title)

        self.pid_grid = QGridLayout()
        self.pid_grid.setObjectName(u"pid_grid")
        self.pid_grid.setHorizontalSpacing(8)
        self.pid_grid.setVerticalSpacing(8)
        self.kp_lab = QLabel(self.pid_card)
        self.kp_lab.setObjectName(u"kp_lab")

        self.pid_grid.addWidget(self.kp_lab, 0, 0, 1, 1)

        self.kp_spin = QDoubleSpinBox(self.pid_card)
        self.kp_spin.setObjectName(u"kp_spin")
        self.kp_spin.setDecimals(4)
        self.kp_spin.setMaximum(200.000000000000000)
        self.kp_spin.setValue(1.200000000000000)

        self.pid_grid.addWidget(self.kp_spin, 0, 1, 1, 1)

        self.ki_lab = QLabel(self.pid_card)
        self.ki_lab.setObjectName(u"ki_lab")

        self.pid_grid.addWidget(self.ki_lab, 0, 2, 1, 1)

        self.ki_spin = QDoubleSpinBox(self.pid_card)
        self.ki_spin.setObjectName(u"ki_spin")
        self.ki_spin.setDecimals(4)
        self.ki_spin.setMaximum(200.000000000000000)
        self.ki_spin.setValue(0.080000000000000)

        self.pid_grid.addWidget(self.ki_spin, 0, 3, 1, 1)

        self.kd_lab = QLabel(self.pid_card)
        self.kd_lab.setObjectName(u"kd_lab")

        self.pid_grid.addWidget(self.kd_lab, 1, 0, 1, 1)

        self.kd_spin = QDoubleSpinBox(self.pid_card)
        self.kd_spin.setObjectName(u"kd_spin")
        self.kd_spin.setDecimals(4)
        self.kd_spin.setMaximum(200.000000000000000)
        self.kd_spin.setValue(0.010000000000000)

        self.pid_grid.addWidget(self.kd_spin, 1, 1, 1, 1)

        self.pid_out_limit_lab = QLabel(self.pid_card)
        self.pid_out_limit_lab.setObjectName(u"pid_out_limit_lab")

        self.pid_grid.addWidget(self.pid_out_limit_lab, 1, 2, 1, 1)

        self.pid_out_limit_spin = QDoubleSpinBox(self.pid_card)
        self.pid_out_limit_spin.setObjectName(u"pid_out_limit_spin")
        self.pid_out_limit_spin.setDecimals(1)
        self.pid_out_limit_spin.setMaximum(100.000000000000000)
        self.pid_out_limit_spin.setValue(70.000000000000000)

        self.pid_grid.addWidget(self.pid_out_limit_spin, 1, 3, 1, 1)


        self.pid_card_layout.addLayout(self.pid_grid)

        self.pid_button_row = QHBoxLayout()
        self.pid_button_row.setSpacing(8)
        self.pid_button_row.setObjectName(u"pid_button_row")
        self.pid_apply_button = QPushButton(self.pid_card)
        self.pid_apply_button.setObjectName(u"pid_apply_button")

        self.pid_button_row.addWidget(self.pid_apply_button)

        self.pid_reset_i_button = QPushButton(self.pid_card)
        self.pid_reset_i_button.setObjectName(u"pid_reset_i_button")

        self.pid_button_row.addWidget(self.pid_reset_i_button)

        self.pid_button_spacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.pid_button_row.addItem(self.pid_button_spacer)


        self.pid_card_layout.addLayout(self.pid_button_row)


        self.control_column_layout.addWidget(self.pid_card)

        self.setpoint_card = QFrame(self.control_column)
        self.setpoint_card.setObjectName(u"setpoint_card")
        self.setpoint_card_layout = QVBoxLayout(self.setpoint_card)
        self.setpoint_card_layout.setSpacing(8)
        self.setpoint_card_layout.setObjectName(u"setpoint_card_layout")
        self.setpoint_card_layout.setContentsMargins(12, 10, 12, 10)
        self.setpoint_title = QLabel(self.setpoint_card)
        self.setpoint_title.setObjectName(u"setpoint_title")

        self.setpoint_card_layout.addWidget(self.setpoint_title)

        self.setpoint_body_row = QHBoxLayout()
        self.setpoint_body_row.setSpacing(10)
        self.setpoint_body_row.setObjectName(u"setpoint_body_row")
        self.knob_frame = QFrame(self.setpoint_card)
        self.knob_frame.setObjectName(u"knob_frame")
        self.knob_frame_layout = QVBoxLayout(self.knob_frame)
        self.knob_frame_layout.setSpacing(6)
        self.knob_frame_layout.setObjectName(u"knob_frame_layout")
        self.knob_frame_layout.setContentsMargins(10, 10, 10, 10)
        self.knob_overlay = QWidget(self.knob_frame)
        self.knob_overlay.setObjectName(u"knob_overlay")
        self.knob_overlay.setMinimumSize(QSize(136, 136))
        self.knob_overlay.setMaximumSize(QSize(136, 136))
        self.knob_overlay_layout = QGridLayout(self.knob_overlay)
        self.knob_overlay_layout.setObjectName(u"knob_overlay_layout")
        self.knob_overlay_layout.setHorizontalSpacing(0)
        self.knob_overlay_layout.setVerticalSpacing(0)
        self.knob_overlay_layout.setContentsMargins(0, 0, 0, 0)
        self.angle_knob = QDial(self.knob_overlay)
        self.angle_knob.setObjectName(u"angle_knob")
        self.angle_knob.setMinimumSize(QSize(136, 136))
        self.angle_knob.setMaximumSize(QSize(136, 136))
        self.angle_knob.setMinimum(0)
        self.angle_knob.setMaximum(330)
        self.angle_knob.setSingleStep(1)
        self.angle_knob.setPageStep(10)
        self.angle_knob.setNotchesVisible(True)

        self.knob_overlay_layout.addWidget(self.angle_knob, 0, 0, 1, 1)

        self.knob_value_lab = QLabel(self.knob_overlay)
        self.knob_value_lab.setObjectName(u"knob_value_lab")
        self.knob_value_lab.setAlignment(Qt.AlignCenter)

        self.knob_overlay_layout.addWidget(self.knob_value_lab, 0, 0, 1, 1)


        self.knob_frame_layout.addWidget(self.knob_overlay)


        self.setpoint_body_row.addWidget(self.knob_frame)

        self.setpoint_right_layout = QVBoxLayout()
        self.setpoint_right_layout.setSpacing(8)
        self.setpoint_right_layout.setObjectName(u"setpoint_right_layout")
        self.setpoint_grid = QGridLayout()
        self.setpoint_grid.setObjectName(u"setpoint_grid")
        self.setpoint_grid.setHorizontalSpacing(8)
        self.setpoint_grid.setVerticalSpacing(8)
        self.angle_set_lab = QLabel(self.setpoint_card)
        self.angle_set_lab.setObjectName(u"angle_set_lab")

        self.setpoint_grid.addWidget(self.angle_set_lab, 0, 0, 1, 1)

        self.angle_set_spin = QDoubleSpinBox(self.setpoint_card)
        self.angle_set_spin.setObjectName(u"angle_set_spin")
        self.angle_set_spin.setDecimals(0)
        self.angle_set_spin.setMinimum(0.000000000000000)
        self.angle_set_spin.setMaximum(330.000000000000000)
        self.angle_set_spin.setSingleStep(1.000000000000000)
        self.angle_set_spin.setValue(0.000000000000000)

        self.setpoint_grid.addWidget(self.angle_set_spin, 0, 1, 1, 1)

        self.adc_set_lab = QLabel(self.setpoint_card)
        self.adc_set_lab.setObjectName(u"adc_set_lab")

        self.setpoint_grid.addWidget(self.adc_set_lab, 1, 0, 1, 1)

        self.adc_set_spin = QLineEdit(self.setpoint_card)
        self.adc_set_spin.setObjectName(u"adc_set_spin")
        self.adc_set_spin.setReadOnly(True)

        self.setpoint_grid.addWidget(self.adc_set_spin, 1, 1, 1, 1)


        self.setpoint_right_layout.addLayout(self.setpoint_grid)

        self.setpoint_button_row = QHBoxLayout()
        self.setpoint_button_row.setSpacing(8)
        self.setpoint_button_row.setObjectName(u"setpoint_button_row")
        self.setpoint_send_button = QPushButton(self.setpoint_card)
        self.setpoint_send_button.setObjectName(u"setpoint_send_button")

        self.setpoint_button_row.addWidget(self.setpoint_send_button)

        self.motor_enable_button = QPushButton(self.setpoint_card)
        self.motor_enable_button.setObjectName(u"motor_enable_button")

        self.setpoint_button_row.addWidget(self.motor_enable_button)

        self.motor_stop_button = QPushButton(self.setpoint_card)
        self.motor_stop_button.setObjectName(u"motor_stop_button")

        self.setpoint_button_row.addWidget(self.motor_stop_button)


        self.setpoint_right_layout.addLayout(self.setpoint_button_row)

        self.setpoint_hint = QLabel(self.setpoint_card)
        self.setpoint_hint.setObjectName(u"setpoint_hint")

        self.setpoint_right_layout.addWidget(self.setpoint_hint)

        self.setpoint_bottom_spacer = QSpacerItem(20, 20, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.setpoint_right_layout.addItem(self.setpoint_bottom_spacer)


        self.setpoint_body_row.addLayout(self.setpoint_right_layout)


        self.setpoint_card_layout.addLayout(self.setpoint_body_row)


        self.control_column_layout.addWidget(self.setpoint_card)

        self.steering_bottom_splitter.addWidget(self.control_column)
        self.runtime_card = QFrame(self.steering_bottom_splitter)
        self.runtime_card.setObjectName(u"runtime_card")
        self.runtime_card_layout = QVBoxLayout(self.runtime_card)
        self.runtime_card_layout.setSpacing(8)
        self.runtime_card_layout.setObjectName(u"runtime_card_layout")
        self.runtime_card_layout.setContentsMargins(12, 10, 12, 10)
        self.runtime_title = QLabel(self.runtime_card)
        self.runtime_title.setObjectName(u"runtime_title")

        self.runtime_card_layout.addWidget(self.runtime_title)

        self.runtime_grid = QGridLayout()
        self.runtime_grid.setObjectName(u"runtime_grid")
        self.runtime_grid.setHorizontalSpacing(8)
        self.runtime_grid.setVerticalSpacing(6)
        self.rt_conn_lab = QLabel(self.runtime_card)
        self.rt_conn_lab.setObjectName(u"rt_conn_lab")

        self.runtime_grid.addWidget(self.rt_conn_lab, 0, 0, 1, 1)

        self.rt_conn_value = QLabel(self.runtime_card)
        self.rt_conn_value.setObjectName(u"rt_conn_value")

        self.runtime_grid.addWidget(self.rt_conn_value, 0, 1, 1, 1)

        self.rt_set_lab = QLabel(self.runtime_card)
        self.rt_set_lab.setObjectName(u"rt_set_lab")

        self.runtime_grid.addWidget(self.rt_set_lab, 1, 0, 1, 1)

        self.rt_set_value = QLabel(self.runtime_card)
        self.rt_set_value.setObjectName(u"rt_set_value")

        self.runtime_grid.addWidget(self.rt_set_value, 1, 1, 1, 1)

        self.rt_fb_lab = QLabel(self.runtime_card)
        self.rt_fb_lab.setObjectName(u"rt_fb_lab")

        self.runtime_grid.addWidget(self.rt_fb_lab, 2, 0, 1, 1)

        self.rt_fb_value = QLabel(self.runtime_card)
        self.rt_fb_value.setObjectName(u"rt_fb_value")

        self.runtime_grid.addWidget(self.rt_fb_value, 2, 1, 1, 1)

        self.rt_err_lab = QLabel(self.runtime_card)
        self.rt_err_lab.setObjectName(u"rt_err_lab")

        self.runtime_grid.addWidget(self.rt_err_lab, 3, 0, 1, 1)

        self.rt_err_value = QLabel(self.runtime_card)
        self.rt_err_value.setObjectName(u"rt_err_value")

        self.runtime_grid.addWidget(self.rt_err_value, 3, 1, 1, 1)

        self.rt_pwm_lab = QLabel(self.runtime_card)
        self.rt_pwm_lab.setObjectName(u"rt_pwm_lab")

        self.runtime_grid.addWidget(self.rt_pwm_lab, 4, 0, 1, 1)

        self.rt_pwm_value = QLabel(self.runtime_card)
        self.rt_pwm_value.setObjectName(u"rt_pwm_value")

        self.runtime_grid.addWidget(self.rt_pwm_value, 4, 1, 1, 1)

        self.rt_rpm_lab = QLabel(self.runtime_card)
        self.rt_rpm_lab.setObjectName(u"rt_rpm_lab")

        self.runtime_grid.addWidget(self.rt_rpm_lab, 5, 0, 1, 1)

        self.rt_rpm_value = QLabel(self.runtime_card)
        self.rt_rpm_value.setObjectName(u"rt_rpm_value")

        self.runtime_grid.addWidget(self.rt_rpm_value, 5, 1, 1, 1)

        self.rt_vbus_lab = QLabel(self.runtime_card)
        self.rt_vbus_lab.setObjectName(u"rt_vbus_lab")

        self.runtime_grid.addWidget(self.rt_vbus_lab, 6, 0, 1, 1)

        self.rt_vbus_value = QLabel(self.runtime_card)
        self.rt_vbus_value.setObjectName(u"rt_vbus_value")

        self.runtime_grid.addWidget(self.rt_vbus_value, 6, 1, 1, 1)


        self.runtime_card_layout.addLayout(self.runtime_grid)

        self.runtime_hint = QLabel(self.runtime_card)
        self.runtime_hint.setObjectName(u"runtime_hint")

        self.runtime_card_layout.addWidget(self.runtime_hint)

        self.telemetry_raw_title = QLabel(self.runtime_card)
        self.telemetry_raw_title.setObjectName(u"telemetry_raw_title")

        self.runtime_card_layout.addWidget(self.telemetry_raw_title)

        self.telemetry_raw_view = QPlainTextEdit(self.runtime_card)
        self.telemetry_raw_view.setObjectName(u"telemetry_raw_view")
        self.telemetry_raw_view.setReadOnly(True)

        self.runtime_card_layout.addWidget(self.telemetry_raw_view)

        self.telemetry_cmd_row = QHBoxLayout()
        self.telemetry_cmd_row.setSpacing(8)
        self.telemetry_cmd_row.setObjectName(u"telemetry_cmd_row")
        self.telemetry_cmd_edit = QLineEdit(self.runtime_card)
        self.telemetry_cmd_edit.setObjectName(u"telemetry_cmd_edit")

        self.telemetry_cmd_row.addWidget(self.telemetry_cmd_edit)

        self.telemetry_send_button = QPushButton(self.runtime_card)
        self.telemetry_send_button.setObjectName(u"telemetry_send_button")

        self.telemetry_cmd_row.addWidget(self.telemetry_send_button)

        self.telemetry_clear_button = QPushButton(self.runtime_card)
        self.telemetry_clear_button.setObjectName(u"telemetry_clear_button")

        self.telemetry_cmd_row.addWidget(self.telemetry_clear_button)


        self.runtime_card_layout.addLayout(self.telemetry_cmd_row)

        self.steering_bottom_splitter.addWidget(self.runtime_card)
        self.steering_vertical_splitter.addWidget(self.steering_bottom_splitter)

        self.steering_root_layout.addWidget(self.steering_vertical_splitter)

        self.tabWidget.addTab(self.steering_tab, "")

        self.verticalLayout.addWidget(self.tabWidget)

        MainWindow.setCentralWidget(self.centralwidget)
        self.menubar = QMenuBar(MainWindow)
        self.menubar.setObjectName(u"menubar")
        self.menubar.setGeometry(QRect(0, 0, 1200, 22))
        MainWindow.setMenuBar(self.menubar)
        self.statusbar = QStatusBar(MainWindow)
        self.statusbar.setObjectName(u"statusbar")
        MainWindow.setStatusBar(self.statusbar)

        self.retranslateUi(MainWindow)

        self.tabWidget.setCurrentIndex(0)


        QMetaObject.connectSlotsByName(MainWindow)
    # setupUi

    def retranslateUi(self, MainWindow):
        MainWindow.setWindowTitle(QCoreApplication.translate("MainWindow", u"MainWindow", None))
        self.hero_title.setText(QCoreApplication.translate("MainWindow", u"BLE DEVICE CONFIG", None))
#if QT_CONFIG(tooltip)
        self.ble_conn_led.setToolTip(QCoreApplication.translate("MainWindow", u"BLE Link LED", None))
#endif // QT_CONFIG(tooltip)
        self.ble_conn_led.setText("")
        self.hero_subtitle.setText(QCoreApplication.translate("MainWindow", u"Search, connect, configure UUID, send and receive text", None))
        self.section_title.setText(QCoreApplication.translate("MainWindow", u"DEVICE / CONNECTION", None))
        self.device_filter_lab.setText(QCoreApplication.translate("MainWindow", u"Device Filter", None))
        self.device_filter_edit.setPlaceholderText(QCoreApplication.translate("MainWindow", u"Leave empty to list all devices", None))
        self.scan_button.setText(QCoreApplication.translate("MainWindow", u"SEARCH", None))
        self.connect_button.setText(QCoreApplication.translate("MainWindow", u"CONNECT", None))
        self.disconnect_button.setText(QCoreApplication.translate("MainWindow", u"DISCONNECT", None))
        self.apply_config_button.setText(QCoreApplication.translate("MainWindow", u"APPLY CONFIG", None))
        self.service_uuid_lab.setText(QCoreApplication.translate("MainWindow", u"Service UUID", None))
        self.service_uuid_edit.setText(QCoreApplication.translate("MainWindow", u"0000fff0-0000-1000-8000-00805f9b34fb", None))
        self.rx_uuid_lab.setText(QCoreApplication.translate("MainWindow", u"RX UUID", None))
        self.rx_uuid_edit.setText(QCoreApplication.translate("MainWindow", u"0000fff1-0000-1000-8000-00805f9b34fb", None))
        self.tx_uuid_lab.setText(QCoreApplication.translate("MainWindow", u"TX UUID", None))
        self.tx_uuid_edit.setText(QCoreApplication.translate("MainWindow", u"0000fff2-0000-1000-8000-00805f9b34fb", None))
        self.chip_key_state.setText(QCoreApplication.translate("MainWindow", u"State:", None))
        self.conn_value.setText(QCoreApplication.translate("MainWindow", u"--", None))
        self.chip_key_device.setText(QCoreApplication.translate("MainWindow", u"Device:", None))
        self.device_value.setText(QCoreApplication.translate("MainWindow", u"--", None))
        self.chip_key_service.setText(QCoreApplication.translate("MainWindow", u"Service:", None))
        self.service_value.setText(QCoreApplication.translate("MainWindow", u"--", None))
        self.chip_key_rx.setText(QCoreApplication.translate("MainWindow", u"RX:", None))
        self.rx_value.setText(QCoreApplication.translate("MainWindow", u"--", None))
        self.chip_key_tx.setText(QCoreApplication.translate("MainWindow", u"TX:", None))
        self.tx_value.setText(QCoreApplication.translate("MainWindow", u"--", None))
        self.config_summary.setText("")
        self.list_title.setText(QCoreApplication.translate("MainWindow", u"SCAN RESULTS", None))
        self.list_hint.setText(QCoreApplication.translate("MainWindow", u"Tip: select a device then click CONNECT", None))
        self.io_title.setText(QCoreApplication.translate("MainWindow", u"TEXT TX / RX", None))
        self.rx_title.setText(QCoreApplication.translate("MainWindow", u"RECEIVE", None))
        self.rx_view.setPlaceholderText(QCoreApplication.translate("MainWindow", u"Received text will appear here", None))
        self.tx_title.setText(QCoreApplication.translate("MainWindow", u"SEND HISTORY", None))
        self.tx_view.setPlaceholderText(QCoreApplication.translate("MainWindow", u"Sent text history", None))
        self.tx_input.setPlaceholderText(QCoreApplication.translate("MainWindow", u"Input command or text", None))
        self.send_button.setText(QCoreApplication.translate("MainWindow", u"SEND", None))
        self.clear_button.setText(QCoreApplication.translate("MainWindow", u"CLEAR", None))
        self.quick_cmd_lab.setText(QCoreApplication.translate("MainWindow", u"Quick Command", None))
        self.ping_button.setText(QCoreApplication.translate("MainWindow", u"PING", None))
        self.get_status_button.setText(QCoreApplication.translate("MainWindow", u"GET_STATUS", None))
        self.reboot_button.setText(QCoreApplication.translate("MainWindow", u"REBOOT", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.ble_tab), QCoreApplication.translate("MainWindow", u"BLE Config", None))
        self.steering_hero_title.setText(QCoreApplication.translate("MainWindow", u"STEERING ENGINE DEBUG CONSOLE", None))
        self.steering_hero_subtitle.setText(QCoreApplication.translate("MainWindow", u"DC Motor + Variable Resistor closed-loop tuning and live telemetry", None))
        self.scope_title.setText(QCoreApplication.translate("MainWindow", u"SCOPE WAVEFORM", None))
        self.scope_hint.setText(QCoreApplication.translate("MainWindow", u"Setpoint / Feedback / PWM", None))
        self.scope_window_lab.setText(QCoreApplication.translate("MainWindow", u"Window", None))
        self.scope_window_combo.setItemText(0, QCoreApplication.translate("MainWindow", u"8 s", None))
        self.scope_window_combo.setItemText(1, QCoreApplication.translate("MainWindow", u"12 s", None))
        self.scope_window_combo.setItemText(2, QCoreApplication.translate("MainWindow", u"20 s", None))
        self.scope_window_combo.setItemText(3, QCoreApplication.translate("MainWindow", u"30 s", None))

        self.scope_pause_button.setText(QCoreApplication.translate("MainWindow", u"Pause", None))
        self.scope_clear_button.setText(QCoreApplication.translate("MainWindow", u"Clear", None))
        self.scope_live_badge.setText(QCoreApplication.translate("MainWindow", u"LIVE", None))
        self.pid_title.setText(QCoreApplication.translate("MainWindow", u"PID TUNING", None))
        self.kp_lab.setText(QCoreApplication.translate("MainWindow", u"Kp", None))
        self.ki_lab.setText(QCoreApplication.translate("MainWindow", u"Ki", None))
        self.kd_lab.setText(QCoreApplication.translate("MainWindow", u"Kd", None))
        self.pid_out_limit_lab.setText(QCoreApplication.translate("MainWindow", u"Output Limit", None))
        self.pid_apply_button.setText(QCoreApplication.translate("MainWindow", u"Apply PID", None))
        self.pid_reset_i_button.setText(QCoreApplication.translate("MainWindow", u"Reset Integral", None))
        self.setpoint_title.setText(QCoreApplication.translate("MainWindow", u"STEERING COMMAND", None))
        self.knob_value_lab.setText(QCoreApplication.translate("MainWindow", u"0 deg", None))
        self.angle_set_lab.setText(QCoreApplication.translate("MainWindow", u"Target Angle (deg)", None))
        self.adc_set_lab.setText(QCoreApplication.translate("MainWindow", u"Target ADC (0~4095)", None))
        self.adc_set_spin.setText(QCoreApplication.translate("MainWindow", u"0", None))
        self.setpoint_send_button.setText(QCoreApplication.translate("MainWindow", u"Send Setpoint", None))
        self.motor_enable_button.setText(QCoreApplication.translate("MainWindow", u"Enable Motor", None))
        self.motor_stop_button.setText(QCoreApplication.translate("MainWindow", u"Emergency Stop", None))
        self.setpoint_hint.setText(QCoreApplication.translate("MainWindow", u"Knob range: 0 to 330 deg, step: 1 deg. Use Emergency Stop when needed.", None))
        self.runtime_title.setText(QCoreApplication.translate("MainWindow", u"REAL-TIME PARAMETERS", None))
        self.rt_conn_lab.setText(QCoreApplication.translate("MainWindow", u"Connection", None))
        self.rt_conn_value.setText(QCoreApplication.translate("MainWindow", u"Disconnected", None))
        self.rt_set_lab.setText(QCoreApplication.translate("MainWindow", u"Setpoint", None))
        self.rt_set_value.setText(QCoreApplication.translate("MainWindow", u"0.0 deg", None))
        self.rt_fb_lab.setText(QCoreApplication.translate("MainWindow", u"Feedback", None))
        self.rt_fb_value.setText(QCoreApplication.translate("MainWindow", u"0.0 deg", None))
        self.rt_err_lab.setText(QCoreApplication.translate("MainWindow", u"Error", None))
        self.rt_err_value.setText(QCoreApplication.translate("MainWindow", u"0.0 deg", None))
        self.rt_pwm_lab.setText(QCoreApplication.translate("MainWindow", u"PWM Output", None))
        self.rt_pwm_value.setText(QCoreApplication.translate("MainWindow", u"0.0 %", None))
        self.rt_rpm_lab.setText(QCoreApplication.translate("MainWindow", u"Motor Speed", None))
        self.rt_rpm_value.setText(QCoreApplication.translate("MainWindow", u"0.0 rpm", None))
        self.rt_vbus_lab.setText(QCoreApplication.translate("MainWindow", u"Bus Voltage", None))
        self.rt_vbus_value.setText(QCoreApplication.translate("MainWindow", u"0.0 V", None))
        self.runtime_hint.setText(QCoreApplication.translate("MainWindow", u"Tip: supports set=12.5, fb=11.9, pwm=28, rpm=320, AT+MOTVR: 2073, AT+MOTPWM: -5", None))
        self.telemetry_raw_title.setText(QCoreApplication.translate("MainWindow", u"RAW TELEMETRY", None))
        self.telemetry_raw_view.setPlaceholderText(QCoreApplication.translate("MainWindow", u"RX/TX log and parsed steering telemetry", None))
        self.telemetry_cmd_edit.setPlaceholderText(QCoreApplication.translate("MainWindow", u"Manual command, e.g. STEER_PID kp=1.3 ki=0.05 kd=0.01", None))
        self.telemetry_send_button.setText(QCoreApplication.translate("MainWindow", u"Send Manual CMD", None))
        self.telemetry_clear_button.setText(QCoreApplication.translate("MainWindow", u"Clear Raw Telemetry", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.steering_tab), QCoreApplication.translate("MainWindow", u"Steering Debug", None))
    # retranslateUi

