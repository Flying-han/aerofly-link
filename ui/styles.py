# -*- coding: utf-8 -*-
"""
共享样式定义
============
集中管理所有 UI 组件的 QSS 样式常量，避免在多个文件中重复定义。
每个样式以常量形式导出，组件直接引用。
"""

# ── 颜色常量 ──────────────────────────────────────────────────
COLOR_BG_DARK = "#1a1a1a"
COLOR_BG_PANEL = "#1e1e1e"
COLOR_BG_INPUT = "#2d2d2d"
COLOR_BG_INPUT_FOCUS = "#333"
COLOR_BG_INPUT_DISABLED = "#222"
COLOR_BORDER = "#555"
COLOR_BORDER_FOCUS = "#4CAF50"
COLOR_TEXT = "#f0f0f0"
COLOR_TEXT_DIM = "#aaa"
COLOR_TEXT_DISABLED = "#555"
COLOR_ACCENT = "#4CAF50"
COLOR_DANGER = "#c0392b"
COLOR_WARN = "#ff9800"
COLOR_INFO = "#2196F3"

# ── 输入框样式 ────────────────────────────────────────────────
INPUT_CSS = f"""
    QLineEdit {{
        background-color: {COLOR_BG_INPUT};
        color: {COLOR_TEXT};
        border: 1px solid {COLOR_BORDER};
        border-radius: 4px;
        padding: 5px 8px;
        font-size: 11px;
        selection-background-color: {COLOR_ACCENT};
    }}
    QLineEdit:focus {{
        border: 1px solid {COLOR_BORDER_FOCUS};
        background-color: {COLOR_BG_INPUT_FOCUS};
    }}
    QLineEdit:disabled {{
        background-color: {COLOR_BG_INPUT_DISABLED};
        color: {COLOR_TEXT_DISABLED};
        border: 1px solid #333;
    }}
"""

# ── 大号输入框（连接页用）────────────────────────────────────
INPUT_CSS_LARGE = f"""
    QLineEdit {{
        background-color: {COLOR_BG_INPUT};
        color: {COLOR_TEXT};
        border: 1px solid {COLOR_BORDER};
        border-radius: 4px;
        padding: 6px 10px;
        font-size: 12px;
        selection-background-color: {COLOR_ACCENT};
    }}
    QLineEdit:focus {{
        border: 1px solid {COLOR_BORDER_FOCUS};
        background-color: {COLOR_BG_INPUT_FOCUS};
    }}
    QLineEdit:disabled {{
        background-color: {COLOR_BG_INPUT_DISABLED};
        color: {COLOR_TEXT_DISABLED};
        border: 1px solid #333;
    }}
"""

# ── 下拉框样式 ────────────────────────────────────────────────
COMBO_CSS = f"""
    QComboBox {{
        background-color: {COLOR_BG_INPUT};
        color: {COLOR_TEXT};
        border: 1px solid {COLOR_BORDER};
        border-radius: 4px;
        padding: 5px 24px 5px 8px;
        font-size: 11px;
        selection-background-color: {COLOR_ACCENT};
    }}
    QComboBox::drop-down {{
        subcontrol-origin: padding;
        subcontrol-position: center right;
        width: 20px;
        border: none;
    }}
    QComboBox::down-arrow {{
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #aaa;
        width: 0px;
        height: 0px;
    }}
    QComboBox QAbstractItemView {{
        background-color: {COLOR_BG_INPUT};
        color: {COLOR_TEXT};
        selection-background-color: {COLOR_ACCENT};
        font-size: 11px;
        padding: 4px;
        border: 1px solid {COLOR_BORDER};
    }}
    QComboBox:disabled {{
        background-color: {COLOR_BG_INPUT_DISABLED};
        color: {COLOR_TEXT_DISABLED};
        border: 1px solid #333;
    }}
"""

# ── 大号下拉框（连接页用）────────────────────────────────────
COMBO_CSS_LARGE = COMBO_CSS.replace("padding: 5px 24px 5px 8px;", "padding: 6px 32px 6px 10px;").replace(
    "font-size: 11px;", "font-size: 12px;", 1
)

# ── 标签样式 ────────────────────────────────────────────────
LABEL_CSS = f"color: {COLOR_TEXT_DIM}; font-size: 11px; font-weight: bold; padding-bottom: 2px;"

# ── 小按钮样式（+/- 按钮）────────────────────────────────────
BTN_SMALL_CSS = f"""
    QPushButton {{
        background-color: #3a3a3a;
        color: #e0e0e0;
        border: 1px solid {COLOR_BORDER};
        border-radius: 4px;
        padding: 3px 6px;
        font-size: 12px;
        font-weight: bold;
        min-width: 28px;
        max-height: 24px;
    }}
    QPushButton:hover {{
        background-color: #4a4a4a;
        border: 1px solid {COLOR_ACCENT};
    }}
    QPushButton:disabled {{
        background-color: {COLOR_BG_INPUT_DISABLED};
        color: #444;
        border: 1px solid #333;
    }}
"""

# ── 主按钮（连接）────────────────────────────────────────────
BTN_PRIMARY_CSS = f"""
    QPushButton {{
        background-color: {COLOR_ACCENT};
        color: white;
        padding: 6px;
        font-size: 12px;
        font-weight: bold;
        border: none;
        border-radius: 4px;
        letter-spacing: 1px;
    }}
    QPushButton:hover {{ background-color: #45a049; }}
    QPushButton:pressed {{ background-color: #388E3C; }}
    QPushButton:disabled {{ background-color: #333; color: #555; }}
"""

# ── 危险按钮（断开）───────────────────────────────────────────
BTN_DANGER_CSS = f"""
    QPushButton {{
        background-color: {COLOR_DANGER};
        color: white;
        padding: 6px;
        font-size: 12px;
        font-weight: bold;
        border: none;
        border-radius: 4px;
        letter-spacing: 1px;
    }}
    QPushButton:hover {{ background-color: #e74c3c; }}
    QPushButton:pressed {{ background-color: #a93226; }}
    QPushButton:disabled {{ background-color: #333; color: #555; }}
"""

# ── 信息按钮（提交飞行计划等）────────────────────────────────
BTN_INFO_CSS = f"""
    QPushButton {{
        background-color: {COLOR_INFO};
        color: white;
        padding: 5px;
        font-size: 11px;
        font-weight: bold;
        border: none;
        border-radius: 3px;
    }}
    QPushButton:hover {{ background-color: #1976D2; }}
    QPushButton:disabled {{ background-color: #333; color: #666; }}
"""

# ── 面板/分组框样式 ──────────────────────────────────────────
GROUPBOX_CSS = f"""
    QGroupBox {{
        border: 1px solid #333;
        border-radius: 4px;
        margin-top: 8px;
        padding-top: 10px;
        font-size: 11px;
        font-weight: bold;
        color: #ccc;
    }}
    QGroupBox::title {{
        subcontrol-origin: margin;
        left: 8px;
        padding: 0 4px;
        color: {COLOR_TEXT_DIM};
    }}
"""

# ── 全局应用样式 ──────────────────────────────────────────────
APP_QSS = f"""
    QMainWindow {{
        background-color: {COLOR_BG_DARK};
    }}
    QWidget {{
        background-color: {COLOR_BG_PANEL};
        color: #e0e0e0;
        font-size: 11px;
    }}
    {GROUPBOX_CSS}
    QLabel {{
        background-color: transparent;
        color: #ccc;
    }}
    QSplitter::handle {{
        background-color: #333;
        width: 2px;
    }}
    QSplitter::handle:hover {{
        background-color: {COLOR_ACCENT};
    }}
    QStatusBar {{
        background-color: {COLOR_BG_DARK};
        color: #e0e0e0;
        border-top: 1px solid #333;
    }}
    QScrollBar:vertical {{
        background: {COLOR_BG_PANEL};
        width: 10px;
        border-radius: 5px;
    }}
    QScrollBar::handle:vertical {{
        background: #555;
        border-radius: 5px;
        min-height: 30px;
    }}
    QScrollBar::add-line:vertical,
    QScrollBar::sub-line:vertical {{
        height: 0;
    }}
"""
