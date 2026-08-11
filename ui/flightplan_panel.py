# -*- coding: utf-8 -*-
"""
飞行计划提交面板
================
提交 FSD $FP 报文到服务器。
字段：
  Type / Callsign(只读) / Aircraft / Wake Turbulence / TAS /
  起降机场 / Departure Time(UTC) / 巡航高度 / Route /
  备降机场 / 备注 / Pilot(真实姓名)
"""
from datetime import datetime, timezone

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QLineEdit,
    QComboBox, QPushButton, QGroupBox, QLabel, QScrollArea,
    QTextEdit, QMessageBox
)
from PyQt6.QtCore import pyqtSignal, QTimer

from ui.styles import INPUT_CSS, COMBO_CSS, BTN_INFO_CSS

# ── 选项常量 ─────────────────────────────────────────────────
FLIGHT_TYPES = ["IFR", "VFR", "SVFR", "DVFR"]
WAKE_CATEGORIES = ["Light", "Medium", "Heavy", "Super"]


class FlightPlanPanel(QGroupBox):
    """飞行计划面板 —— 可滚动的表单 + 提交按钮"""

    flight_plan_submitted = pyqtSignal(dict)       # 用户点击提交
    flight_plan_status = pyqtSignal(str, str)       # status, message

    def __init__(self, parent=None):
        super().__init__("飞行计划", parent)
        self._init_ui()
        # 自动刷新 UTC 时间每秒一次
        self._time_timer = QTimer(self)
        self._time_timer.timeout.connect(self._refresh_utc)
        self._time_timer.start(1000)

    # ── 样式常量（使用共享样式）──────────────────────────────
    INPUT_STYLE = INPUT_CSS
    COMBO_STYLE = COMBO_CSS
    LABEL_STYLE = "color: #aaa; font-size: 12px;"

    def _init_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(2, 2, 2, 2)

        # 滚动区域 —— 适应侧边栏宽度
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        scroll_widget = QWidget()
        layout = QVBoxLayout(scroll_widget)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        form = QFormLayout()
        form.setVerticalSpacing(6)
        form.setLabelAlignment(
            form.labelAlignment().AlignLeft | form.labelAlignment().AlignVCenter
        )

        # ── Type ──
        self.combo_type = QComboBox()
        self.combo_type.addItems(FLIGHT_TYPES)
        self.combo_type.setCurrentText("IFR")
        self.combo_type.setStyleSheet(self.COMBO_STYLE)
        lbl_type = QLabel("Type:")
        lbl_type.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_type, self.combo_type)

        # ── Callsign（只读）──
        self.input_callsign = QLineEdit()
        self.input_callsign.setReadOnly(True)
        self.input_callsign.setStyleSheet(self.INPUT_STYLE)
        lbl_cs = QLabel("Callsign:")
        lbl_cs.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_cs, self.input_callsign)

        # ── Aircraft + Wake 同行 ──
        row_ac = QHBoxLayout()
        row_ac.setSpacing(6)
        self.input_aircraft = QLineEdit()
        self.input_aircraft.setPlaceholderText("B738")
        self.input_aircraft.setMaxLength(4)
        self.input_aircraft.setStyleSheet(self.INPUT_STYLE)
        row_ac.addWidget(self.input_aircraft, 1)

        self.combo_wake = QComboBox()
        self.combo_wake.addItems(WAKE_CATEGORIES)
        self.combo_wake.setCurrentText("Medium")
        self.combo_wake.setStyleSheet(self.COMBO_STYLE)
        row_ac.addWidget(self.combo_wake, 1)

        lbl_ac = QLabel("Aircraft:")
        lbl_ac.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_ac, row_ac)

        # ── TAS ──
        row_tas = QHBoxLayout()
        row_tas.setSpacing(4)
        self.input_tas = QLineEdit()
        self.input_tas.setPlaceholderText("450")
        self.input_tas.setMaxLength(4)
        self.input_tas.setStyleSheet(self.INPUT_STYLE)
        row_tas.addWidget(self.input_tas, 1)
        lbl_kts = QLabel("kts")
        lbl_kts.setStyleSheet("color: #888; font-size: 12px;")
        row_tas.addWidget(lbl_kts)
        lbl_tas = QLabel("TAS:")
        lbl_tas.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_tas, row_tas)

        # ── Departure Airport ──
        self.input_dep_airport = QLineEdit()
        self.input_dep_airport.setPlaceholderText("ZBAA")
        self.input_dep_airport.setMaxLength(4)
        self.input_dep_airport.setStyleSheet(self.INPUT_STYLE)
        lbl_dep = QLabel("Dep. Airport:")
        lbl_dep.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_dep, self.input_dep_airport)

        # ── Departure Time (UTC) ──
        self.input_dep_time = QLineEdit()
        self.input_dep_time.setPlaceholderText("HHMM UTC")
        self.input_dep_time.setMaxLength(4)
        self.input_dep_time.setStyleSheet(self.INPUT_STYLE)
        lbl_time = QLabel("Dep. Time:")
        lbl_time.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_time, self.input_dep_time)

        # ── Actual Departure Time (UTC) ──
        self.input_actual_dep_time = QLineEdit()
        self.input_actual_dep_time.setPlaceholderText("HHMM UTC（未起飞留空）")
        self.input_actual_dep_time.setMaxLength(4)
        self.input_actual_dep_time.setStyleSheet(self.INPUT_STYLE)
        lbl_actual_time = QLabel("Actual Dep.:")
        lbl_actual_time.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_actual_time, self.input_actual_dep_time)

        # ── EET (HH:MM) ──
        self.input_eet = QLineEdit()
        self.input_eet.setPlaceholderText("HH:MM")
        self.input_eet.setMaxLength(5)
        self.input_eet.setStyleSheet(self.INPUT_STYLE)
        lbl_eet = QLabel("EET:")
        lbl_eet.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_eet, self.input_eet)

        # ── Endurance (HH:MM) ──
        self.input_endurance = QLineEdit()
        self.input_endurance.setPlaceholderText("HH:MM")
        self.input_endurance.setMaxLength(5)
        self.input_endurance.setStyleSheet(self.INPUT_STYLE)
        lbl_endurance = QLabel("Endurance:")
        lbl_endurance.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_endurance, self.input_endurance)

        # ── Cruising Altitude ──
        self.input_cruise_alt = QLineEdit()
        self.input_cruise_alt.setPlaceholderText("F350 或 A050")
        self.input_cruise_alt.setMaxLength(8)
        self.input_cruise_alt.setStyleSheet(self.INPUT_STYLE)
        lbl_alt = QLabel("Cruise Alt:")
        lbl_alt.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_alt, self.input_cruise_alt)

        # ── Route ──
        self.input_route = QLineEdit()
        self.input_route.setPlaceholderText("如: CDY G212 KR B458 WXI")
        self.input_route.setStyleSheet(self.INPUT_STYLE)
        lbl_route = QLabel("Route:")
        lbl_route.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_route, self.input_route)

        # ── Destination Airport ──
        self.input_dest_airport = QLineEdit()
        self.input_dest_airport.setPlaceholderText("ZGGG")
        self.input_dest_airport.setMaxLength(4)
        self.input_dest_airport.setStyleSheet(self.INPUT_STYLE)
        lbl_dest = QLabel("Dest. Airport:")
        lbl_dest.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_dest, self.input_dest_airport)

        # ── Alternate Airport ──
        self.input_alt_airport = QLineEdit()
        self.input_alt_airport.setPlaceholderText("ZGSZ")
        self.input_alt_airport.setMaxLength(4)
        self.input_alt_airport.setStyleSheet(self.INPUT_STYLE)
        lbl_altn = QLabel("Alternate:")
        lbl_altn.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_altn, self.input_alt_airport)

        # ── Remarks ──
        self.input_remarks = QTextEdit()
        self.input_remarks.setPlaceholderText("备注（如 OPR/公司名 RMK/TCAS）")
        self.input_remarks.setMinimumHeight(36)
        self.input_remarks.setStyleSheet(self.INPUT_STYLE)
        lbl_rmk = QLabel("Remarks:")
        lbl_rmk.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_rmk, self.input_remarks)

        # ── Pilot（真实姓名）──
        self.input_pilot = QLineEdit()
        self.input_pilot.setPlaceholderText("真实姓名")
        self.input_pilot.setStyleSheet(self.INPUT_STYLE)
        lbl_pilot = QLabel("Pilot:")
        lbl_pilot.setStyleSheet(self.LABEL_STYLE)
        form.addRow(lbl_pilot, self.input_pilot)

        layout.addLayout(form)

        # ── 提交按钮 ──
        self.btn_submit = QPushButton("提交飞行计划")
        self.btn_submit.setStyleSheet(BTN_INFO_CSS)
        self.btn_submit.clicked.connect(self._on_submit)
        layout.addWidget(self.btn_submit)

        # ── 状态标签 ──
        self.lbl_status = QLabel("")
        self.lbl_status.setStyleSheet("color: gray; font-size: 11px; padding-top: 4px;")
        self.lbl_status.setWordWrap(True)
        layout.addWidget(self.lbl_status)

        layout.addStretch()

        scroll.setWidget(scroll_widget)
        outer.addWidget(scroll)

    # ── 公共接口 ──────────────────────────────────────────

    def set_callsign(self, callsign: str):
        """从连接面板同步 Callsign（只读）"""
        if callsign != self.input_callsign.text():
            self.input_callsign.setText(callsign)

    def set_pilot_name(self, name: str):
        """设置飞行员真实姓名"""
        if name != self.input_pilot.text():
            self.input_pilot.setText(name)

    def set_connected(self, connected: bool):
        """连接状态变化时更新 UI"""
        self.btn_submit.setEnabled(connected)
        if not connected:
            self.lbl_status.setText("请先连接到服务器")
            self.lbl_status.setStyleSheet("color: gray; font-size: 11px; padding-top: 4px;")

    def reset_fields(self):
        """清空所有飞行计划字段（保留 Callsign 和 Pilot 由连接面板同步）"""
        self.combo_type.setCurrentText("IFR")
        self.input_aircraft.clear()
        self.combo_wake.setCurrentText("Medium")
        self.input_tas.clear()
        self.input_dep_airport.clear()
        self.input_dep_time.clear()
        self.input_actual_dep_time.clear()
        self.input_eet.clear()
        self.input_endurance.clear()
        self.input_cruise_alt.clear()
        self.input_route.clear()
        self.input_dest_airport.clear()
        self.input_alt_airport.clear()
        self.input_remarks.clear()
        self.lbl_status.setText("请先连接到服务器")
        self.lbl_status.setStyleSheet("color: gray; font-size: 11px; padding-top: 4px;")

    def get_flight_plan(self) -> dict:
        """获取当前填写的飞行计划数据"""
        return {
            "type": self.combo_type.currentText(),
            "callsign": self.input_callsign.text().strip(),
            "aircraft": self.input_aircraft.text().strip().upper(),
            "wake_category": self.combo_wake.currentText(),
            "tas": self.input_tas.text().strip(),
            "dep_airport": self.input_dep_airport.text().strip().upper(),
            "dep_time": self.input_dep_time.text().strip(),
            "actual_dep_time": self.input_actual_dep_time.text().strip(),
            "cruise_alt": self.input_cruise_alt.text().strip(),
            "route": self.input_route.text().strip(),
            "dest_airport": self.input_dest_airport.text().strip().upper(),
            "alt_airport": self.input_alt_airport.text().strip().upper(),
            "remarks": self.input_remarks.toPlainText().strip(),
            "pilot": self.input_pilot.text().strip(),
            "eet": self.input_eet.text().strip(),
            "endurance": self.input_endurance.text().strip(),
        }

    def load_settings(self, settings: dict):
        """加载保存的飞行计划偏好"""
        self.input_aircraft.setText(settings.get("aircraft", ""))
        self.combo_wake.setCurrentText(settings.get("wake_category", "Medium"))
        self.input_pilot.setText(settings.get("pilot", ""))
        self.input_tas.setText(settings.get("tas", ""))
        self.input_dep_airport.setText(settings.get("dep_airport", ""))
        self.input_dest_airport.setText(settings.get("dest_airport", ""))
        self.input_alt_airport.setText(settings.get("alt_airport", ""))
        self.input_cruise_alt.setText(settings.get("cruise_alt", ""))
        self.input_route.setText(settings.get("route", ""))
        self.input_remarks.setPlainText(settings.get("remarks", ""))
        self.input_eet.setText(settings.get("eet", ""))
        self.input_endurance.setText(settings.get("endurance", ""))

    def _refresh_utc(self):
        """每秒刷新 UTC 时间到 departure time 字段"""
        now = datetime.now(timezone.utc)
        utc_str = now.strftime("%H%M")
        # 只在用户未手动修改时自动刷新
        current = self.input_dep_time.text().strip()
        if not current:
            self.input_dep_time.setText(utc_str)

    # ── 提交逻辑 ──────────────────────────────────────────

    def _on_submit(self):
        """验证表单并发出提交信号"""
        plan = self.get_flight_plan()

        # 必填校验
        errors = []
        if not plan["callsign"]:
            errors.append("Callsign 不能为空")
        if not plan["aircraft"]:
            errors.append("机型不能为空")
        if not plan["tas"]:
            errors.append("TAS 不能为空")
        elif not plan["tas"].isdigit():
            errors.append("TAS 必须是数字")
        if not plan["dep_airport"]:
            errors.append("起飞机场不能为空")
        elif len(plan["dep_airport"]) != 4:
            errors.append("起飞机场必须是 4 位 ICAO 码")
        if not plan["dest_airport"]:
            errors.append("降落机场不能为空")
        elif len(plan["dest_airport"]) != 4:
            errors.append("降落机场必须是 4 位 ICAO 码")

        if errors:
            QMessageBox.warning(self, "飞行计划不完整", "\n".join(errors))
            return

        self.lbl_status.setText("正在提交...")
        self.lbl_status.setStyleSheet("color: orange; font-size: 11px; padding-top: 4px;")
        self.btn_submit.setEnabled(False)
        self.flight_plan_submitted.emit(plan)

    def on_submit_result(self, success: bool, message: str = ""):
        """提交结果回调"""
        self.btn_submit.setEnabled(True)
        if success:
            self.lbl_status.setText("飞行计划已提交")
            self.lbl_status.setStyleSheet("color: green; font-size: 11px; padding-top: 4px;")
        else:
            self.lbl_status.setText(f"提交失败: {message}")
            self.lbl_status.setStyleSheet("color: red; font-size: 11px; padding-top: 4px;")
