# -*- coding: utf-8 -*-
"""
Aerofly Link 安装器 — 傻瓜式安装向导 (PyQt6 版)
打包后生成单个 setup.exe，双击即可安装 Aerofly Link 应用和 AF4 DLL
支持 --uninstall 参数作为卸载器使用
"""
import os
import sys
import json
import shutil
import zipfile
import subprocess
import winreg
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit,
    QPushButton, QProgressBar, QCheckBox, QFileDialog, QMessageBox,
    QStackedWidget, QSizePolicy
)
from PyQt6.QtCore import Qt, QTimer, QSize
from PyQt6.QtGui import QFont, QPalette, QColor

# ── 常量 ──────────────────────────────────────────────
APP_NAME = "Aerofly Link"
APP_VERSION = "1.0.0"
PUBLISHER = "Aerofly Link"

REG_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\AeroflyLink"

DEFAULT_INSTALL_DIR = os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), APP_NAME)

DOCUMENTS_DIR = os.path.join(os.environ.get("USERPROFILE", ""), "Documents")
AF4_GAME_DIR = os.path.join(DOCUMENTS_DIR, "Aerofly FS 4")
AF4_DLL_DIR = os.path.join(AF4_GAME_DIR, "external_dll")

# 尝试自动探测 AF4 游戏目录（Steam / 默认 Documents）
def _detect_af4_dir():
    candidates = [
        AF4_GAME_DIR,
        os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "Steam", "steamapps", "common", "Aerofly FS 4"),
        os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Steam", "steamapps", "common", "Aerofly FS 4"),
    ]
    for c in candidates:
        if c and os.path.isdir(c):
            return c
    return AF4_GAME_DIR

DEFAULT_AF4_DIR = _detect_af4_dir()

STYLE_SHEET = """
QWidget {
    background-color: #1e1e1e;
    color: #e0e0e0;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 10pt;
}
QLabel#TitleLabel {
    color: #4CAF50;
    font-size: 22pt;
    font-weight: bold;
}
QLabel#SubtitleLabel {
    color: #888888;
    font-size: 10pt;
}
QLabel#BigLabel {
    font-size: 12pt;
}
QLineEdit {
    background-color: #2d2d2d;
    border: 1px solid #444;
    border-radius: 4px;
    padding: 6px;
    color: #f0f0f0;
}
QPushButton {
    background-color: #4CAF50;
    color: white;
    border: none;
    border-radius: 4px;
    padding: 8px 20px;
    font-weight: bold;
}
QPushButton:hover {
    background-color: #45a049;
}
QPushButton:disabled {
    background-color: #333;
    color: #888;
}
QPushButton#Secondary {
    background-color: #3a3a3a;
    color: #e0e0e0;
}
QPushButton#Secondary:hover {
    background-color: #4a4a4a;
}
QPushButton#Danger {
    background-color: #c0392b;
}
QPushButton#Danger:hover {
    background-color: #e74c3c;
}
QProgressBar {
    border: none;
    border-radius: 4px;
    background-color: #2d2d2d;
    text-align: center;
    color: #e0e0e0;
}
QProgressBar::chunk {
    background-color: #4CAF50;
    border-radius: 4px;
}
QCheckBox {
    spacing: 6px;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
}
"""


def get_resource_dir():
    """获取打包后的资源目录（PyInstaller 的 _MEIPASS 或开发目录）"""
    if hasattr(sys, "_MEIPASS"):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))


def create_shortcut(shortcut_path, target_path, working_dir="", description=""):
    """通过 PowerShell 创建 .lnk 快捷方式"""
    ps_script = (
        f'$ws = New-Object -ComObject WScript.Shell; '
        f'$sc = $ws.CreateShortcut("{shortcut_path}"); '
        f'$sc.TargetPath = "{target_path}"; '
    )
    if working_dir:
        ps_script += f'$sc.WorkingDirectory = "{working_dir}"; '
    if description:
        ps_script += f'$sc.Description = "{description}"; '
    ps_script += '$sc.Save()'
    subprocess.run(
        ["powershell", "-NoProfile", "-Command", ps_script],
        capture_output=True, check=False
    )


def get_desktop_dir():
    """获取桌面目录"""
    return os.path.join(os.environ.get("USERPROFILE", ""), "Desktop")


def get_startmenu_dir():
    """获取当前用户的开始菜单目录（不需要管理员权限）"""
    base = os.path.join(
        os.environ.get("APPDATA", os.environ.get("USERPROFILE", "")),
        "Microsoft", "Windows", "Start Menu", "Programs"
    )
    return os.path.join(base, APP_NAME)


def is_admin():
    try:
        return bool(__import__("ctypes").windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def restart_as_admin(parent):
    ctypes = __import__("ctypes")
    params = " ".join(f'"{a}"' for a in sys.argv)
    ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, params, None, 1)
    QApplication.quit()


def show_error(parent, text):
    msg = QMessageBox(parent)
    msg.setIcon(QMessageBox.Icon.Critical)
    msg.setWindowTitle("错误")
    msg.setText(text)
    msg.exec()


def show_info(parent, text):
    msg = QMessageBox(parent)
    msg.setIcon(QMessageBox.Icon.Information)
    msg.setWindowTitle("提示")
    msg.setText(text)
    msg.exec()


# ═══════════════════════════════════════════════════════════
#  安装向导
# ═══════════════════════════════════════════════════════════

class InstallerWizard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_NAME} 安装程序")
        self.setMinimumSize(560, 440)
        self.resize(600, 480)

        self.install_dir = DEFAULT_INSTALL_DIR
        self.af4_dir = DEFAULT_AF4_DIR
        self.create_desktop_shortcut = True
        self.create_startmenu_shortcut = True
        self.launch_after_finish = True

        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self.stack = QStackedWidget()
        layout.addWidget(self.stack, 1)

        # 底部按钮栏（各页自行控制是否显示，这里先创建占位）
        self.btn_bar = QWidget()
        self.btn_bar_layout = QHBoxLayout(self.btn_bar)
        self.btn_bar_layout.setContentsMargins(24, 12, 24, 18)
        self.btn_bar_layout.addStretch()
        layout.addWidget(self.btn_bar)

        # 构建各页
        self.page_welcome = self._build_welcome_page()
        self.page_location = self._build_location_page()
        self.page_progress = self._build_progress_page()
        self.page_complete = self._build_complete_page()

        self.stack.addWidget(self.page_welcome)
        self.stack.addWidget(self.page_location)
        self.stack.addWidget(self.page_progress)
        self.stack.addWidget(self.page_complete)

        self._show_page(0)

    def _clear_btn_bar(self):
        while self.btn_bar_layout.count():
            item = self.btn_bar_layout.takeAt(0)
            w = item.widget()
            if w:
                w.setParent(None)

    def _add_buttons(self, *buttons):
        self._clear_btn_bar()
        self.btn_bar_layout.addStretch()
        for btn in buttons:
            self.btn_bar_layout.addWidget(btn)

    def _make_btn(self, text, primary=True, object_name=""):
        btn = QPushButton(text)
        if not primary:
            btn.setObjectName("Secondary")
        if object_name:
            btn.setObjectName(object_name)
        return btn

    def _build_welcome_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(36, 36, 36, 24)

        title = QLabel(APP_NAME)
        title.setObjectName("TitleLabel")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        version = QLabel(f"版本 {APP_VERSION}")
        version.setObjectName("SubtitleLabel")
        version.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(version)

        desc = QLabel("Aerofly FS 4 联机客户端")
        desc.setObjectName("BigLabel")
        desc.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addSpacing(20)
        layout.addWidget(desc)

        info = QLabel(
            "本程序将安装 Aerofly Link 客户端及 AF4 桥接 DLL\n\n"
            "安装内容包括：\n"
            "  • Aerofly Link 客户端程序\n"
            "  • AeroflyBridge.dll（自动放置到 AF4 external_dll 目录）\n"
            "  • 桌面和开始菜单快捷方式"
        )
        info.setWordWrap(True)
        layout.addWidget(info)
        layout.addStretch()

        next_btn = self._make_btn("下一步 →")
        next_btn.clicked.connect(lambda: self._show_page(1))
        cancel_btn = self._make_btn("取消", primary=False)
        cancel_btn.clicked.connect(self.close)
        page.buttons = [cancel_btn, next_btn]
        return page

    def _build_location_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(36, 24, 36, 24)

        lbl = QLabel("选择安装位置")
        lbl.setObjectName("BigLabel")
        layout.addWidget(lbl)
        layout.addSpacing(12)

        dir_layout = QHBoxLayout()
        dir_layout.addWidget(QLabel("安装路径："))
        self.dir_edit = QLineEdit(self.install_dir)
        dir_layout.addWidget(self.dir_edit, 1)
        browse_btn = self._make_btn("浏览...", primary=False)
        browse_btn.clicked.connect(self._browse_dir)
        dir_layout.addWidget(browse_btn)
        layout.addLayout(dir_layout)
        layout.addSpacing(10)

        af4_layout = QHBoxLayout()
        af4_layout.addWidget(QLabel("AF4 游戏目录："))
        self.af4_edit = QLineEdit(self.af4_dir)
        af4_layout.addWidget(self.af4_edit, 1)
        af4_browse_btn = self._make_btn("浏览...", primary=False)
        af4_browse_btn.clicked.connect(self._browse_af4_dir)
        af4_layout.addWidget(af4_browse_btn)
        layout.addLayout(af4_layout)
        layout.addSpacing(4)

        dll_tip = QLabel("桥接 DLL 将安装到该目录下的 external_dll 子文件夹")
        dll_tip.setObjectName("SubtitleLabel")
        dll_tip.setWordWrap(True)
        layout.addWidget(dll_tip)
        layout.addSpacing(10)

        self.desktop_cb = QCheckBox("创建桌面快捷方式")
        self.desktop_cb.setChecked(True)
        layout.addWidget(self.desktop_cb)

        self.startmenu_cb = QCheckBox("创建开始菜单快捷方式")
        self.startmenu_cb.setChecked(True)
        layout.addWidget(self.startmenu_cb)
        layout.addStretch()

        back_btn = self._make_btn("← 上一步", primary=False)
        back_btn.clicked.connect(lambda: self._show_page(0))
        cancel_btn = self._make_btn("取消", primary=False)
        cancel_btn.clicked.connect(self.close)
        install_btn = self._make_btn("安装")
        install_btn.clicked.connect(self._start_install)
        page.buttons = [back_btn, cancel_btn, install_btn]
        return page

    def _build_progress_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(36, 36, 36, 24)

        layout.addStretch()
        title = QLabel("正在安装...")
        title.setObjectName("BigLabel")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setTextVisible(True)
        self.progress_bar.setFixedHeight(24)
        layout.addWidget(self.progress_bar)

        self.status_label = QLabel("准备中...")
        self.status_label.setObjectName("SubtitleLabel")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)
        layout.addStretch()

        page.buttons = []
        return page

    def _build_complete_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(36, 36, 36, 24)

        layout.addStretch()
        self.complete_title = QLabel("安装完成！")
        self.complete_title.setObjectName("TitleLabel")
        self.complete_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.complete_title)

        complete_info = QLabel(
            "Aerofly Link 已成功安装到您的计算机\n\n"
            "请确保 Aerofly FS 4 已正确安装\n"
            "DLL 已放置到 external_dll 目录"
        )
        complete_info.setAlignment(Qt.AlignmentFlag.AlignCenter)
        complete_info.setWordWrap(True)
        layout.addWidget(complete_info)

        self.launch_cb = QCheckBox("立即启动 Aerofly Link")
        self.launch_cb.setChecked(True)
        layout.addWidget(self.launch_cb, alignment=Qt.AlignmentFlag.AlignCenter)
        layout.addStretch()

        finish_btn = self._make_btn("完成")
        finish_btn.clicked.connect(self._finish)
        page.buttons = [finish_btn]
        return page

    def _show_page(self, index):
        self.stack.setCurrentIndex(index)
        page = self.stack.widget(index)
        buttons = getattr(page, "buttons", [])
        self._add_buttons(*buttons)

    def _browse_dir(self):
        d = QFileDialog.getExistingDirectory(
            self, "选择安装位置", self.dir_edit.text()
        )
        if d:
            self.dir_edit.setText(d)

    def _browse_af4_dir(self):
        d = QFileDialog.getExistingDirectory(
            self, "选择 Aerofly FS 4 游戏目录", self.af4_edit.text()
        )
        if d:
            self.af4_edit.setText(d)

    def _start_install(self):
        install_dir = self.dir_edit.text().strip()
        if not install_dir:
            show_error(self, "请选择安装路径")
            return

        af4_dir = self.af4_edit.text().strip()
        if not af4_dir:
            show_error(self, "请选择 Aerofly FS 4 游戏目录")
            return

        self.install_dir = install_dir
        self.af4_dir = af4_dir
        self.create_desktop_shortcut = self.desktop_cb.isChecked()
        self.create_startmenu_shortcut = self.startmenu_cb.isChecked()

        if install_dir.startswith((r"C:\Program Files", r"C:\Program Files (x86)")):
            if not is_admin():
                reply = QMessageBox.question(
                    self, "需要管理员权限",
                    "安装到 Program Files 需要管理员权限\n\n"
                    "是否重新以管理员身份运行安装程序？",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
                )
                if reply == QMessageBox.StandardButton.Yes:
                    restart_as_admin(self)
                return

        self._show_page(2)
        QTimer.singleShot(100, self._do_install)

    def _update_status(self, label, value):
        self.status_label.setText(label)
        self.progress_bar.setValue(int(value))
        QApplication.processEvents()

    def _do_install(self):
        steps = [
            ("创建安装目录...", self._step_create_dir),
            ("解压 Aerofly Link 程序...", self._step_extract_app),
            ("安装 AF4 桥接 DLL...", self._step_install_dll),
            ("创建快捷方式...", self._step_create_shortcuts),
            ("写入注册表...", self._step_write_registry),
            ("创建卸载程序...", self._step_create_uninstaller),
        ]
        total = len(steps)
        for i, (label, func) in enumerate(steps):
            self._update_status(label, i / total * 100)
            try:
                func()
            except Exception as e:
                self.complete_title.setText("安装失败")
                self._show_page(3)
                show_error(self, str(e))
                return
            self._update_status(label, (i + 1) / total * 100)

        self._update_status("完成", 100)
        self._show_page(3)

    def _step_create_dir(self):
        os.makedirs(self.install_dir, exist_ok=True)

    def _step_extract_app(self):
        res = get_resource_dir()
        zip_path = os.path.join(res, "aerofly_link.zip")
        if not os.path.exists(zip_path):
            raise FileNotFoundError("找不到应用程序包 aerofly_link.zip")
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(self.install_dir)

    def _step_install_dll(self):
        res = get_resource_dir()
        zip_path = os.path.join(res, "dll.zip")
        if not os.path.exists(zip_path):
            raise FileNotFoundError("找不到 DLL 包 dll.zip")

        # 目标 1: 游戏目录 external_dll（用户选择的 AF4 安装目录）
        dll_dir1 = os.path.join(self.af4_dir, "external_dll")
        os.makedirs(dll_dir1, exist_ok=True)
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(dll_dir1)
        self.progress_signal.emit(f"  ✓ DLL 已安装到游戏目录: {dll_dir1}")

        # 目标 2: Documents\Aerofly FS 4\external_dll（正版默认路径，双保险）
        docs = os.path.join(os.environ.get("USERPROFILE", ""), "Documents")
        dll_dir2 = os.path.join(docs, "Aerofly FS 4", "external_dll")
        if dll_dir2 != dll_dir1:
            os.makedirs(dll_dir2, exist_ok=True)
            with zipfile.ZipFile(zip_path, "r") as zf:
                zf.extractall(dll_dir2)
            self.progress_signal.emit(f"  ✓ DLL 也已安装到 Documents: {dll_dir2}")

    def _step_create_shortcuts(self):
        exe_path = os.path.join(self.install_dir, "Aerofly Link.exe")
        failed = []

        if self.create_desktop_shortcut:
            try:
                desktop = get_desktop_dir()
                create_shortcut(
                    os.path.join(desktop, f"{APP_NAME}.lnk"),
                    exe_path, self.install_dir, APP_NAME
                )
            except Exception as e:
                failed.append(f"桌面快捷方式: {e}")

        if self.create_startmenu_shortcut:
            try:
                startmenu = get_startmenu_dir()
                os.makedirs(startmenu, exist_ok=True)
                create_shortcut(
                    os.path.join(startmenu, f"{APP_NAME}.lnk"),
                    exe_path, self.install_dir, APP_NAME
                )
            except Exception as e:
                failed.append(f"开始菜单快捷方式: {e}")

        if failed:
            # 仅记录，不中断安装
            self.status_label.setText(f"快捷方式部分失败: {'; '.join(failed)}")

    def _step_write_registry(self):
        uninstall_exe = os.path.join(self.install_dir, "uninstall.exe")
        try:
            with winreg.CreateKey(winreg.HKEY_LOCAL_MACHINE, REG_KEY) as key:
                self._write_reg_values(key, uninstall_exe)
        except PermissionError:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY) as key:
                self._write_reg_values(key, uninstall_exe)

    def _write_reg_values(self, key, uninstall_exe):
        winreg.SetValueEx(key, "DisplayName", 0, winreg.REG_SZ, APP_NAME)
        winreg.SetValueEx(key, "DisplayVersion", 0, winreg.REG_SZ, APP_VERSION)
        winreg.SetValueEx(key, "Publisher", 0, winreg.REG_SZ, PUBLISHER)
        winreg.SetValueEx(key, "DisplayIcon", 0, winreg.REG_SZ,
                          os.path.join(self.install_dir, "Aerofly Link.exe"))
        winreg.SetValueEx(key, "InstallLocation", 0, winreg.REG_SZ, self.install_dir)
        winreg.SetValueEx(key, "AF4Dir", 0, winreg.REG_SZ, self.af4_dir)
        winreg.SetValueEx(key, "UninstallString", 0, winreg.REG_SZ,
                          f'"{uninstall_exe}" --uninstall')
        winreg.SetValueEx(key, "NoModify", 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(key, "NoRepair", 0, winreg.REG_DWORD, 1)

    def _step_create_uninstaller(self):
        if getattr(sys, "frozen", False):
            src = sys.executable
            dst = os.path.join(self.install_dir, "uninstall.exe")
            shutil.copy2(src, dst)

    def _finish(self):
        if self.launch_cb.isChecked():
            exe = os.path.join(self.install_dir, "Aerofly Link.exe")
            if os.path.exists(exe):
                subprocess.Popen([exe])
        self.close()


# ═══════════════════════════════════════════════════════════
#  卸载器
# ═══════════════════════════════════════════════════════════

class UninstallerWizard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"卸载 {APP_NAME}")
        self.setMinimumSize(480, 320)
        self.resize(520, 360)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(36, 30, 36, 24)

        title = QLabel(f"卸载 {APP_NAME}")
        title.setObjectName("TitleLabel")
        title.setStyleSheet("color: #c0392b;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        info = QLabel(
            f"确定要完全卸载 {APP_NAME} 吗？\n\n"
            "将删除：\n"
            "  • Aerofly Link 程序文件\n"
            "  • AF4 external_dll 中的桥接 DLL\n"
            "  • 桌面和开始菜单快捷方式\n"
            "  • 注册表项"
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(True)
        self.progress.setFixedHeight(22)
        layout.addWidget(self.progress)

        self.status = QLabel("")
        self.status.setObjectName("SubtitleLabel")
        layout.addWidget(self.status)
        layout.addStretch()

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        uninstall_btn = self._make_btn("卸载", object_name="Danger")
        uninstall_btn.clicked.connect(self._do_uninstall)
        cancel_btn = self._make_btn("取消", primary=False)
        cancel_btn.clicked.connect(self.close)
        btn_layout.addWidget(cancel_btn)
        btn_layout.addWidget(uninstall_btn)
        layout.addLayout(btn_layout)

    def _make_btn(self, text, primary=True, object_name=""):
        btn = QPushButton(text)
        if not primary:
            btn.setObjectName("Secondary")
        if object_name:
            btn.setObjectName(object_name)
        return btn

    def _update(self, label, value):
        self.status.setText(label)
        self.progress.setValue(int(value))
        QApplication.processEvents()

    def _do_uninstall(self):
        steps = [
            ("删除快捷方式...", self._un_step_shortcuts),
            ("删除程序文件...", self._un_step_app),
            ("删除 AF4 DLL...", self._un_step_dll),
            ("清理注册表...", self._un_step_registry),
        ]
        total = len(steps)
        for i, (label, func) in enumerate(steps):
            self._update(label, i / total * 100)
            try:
                func()
            except Exception:
                pass
            self._update(label, (i + 1) / total * 100)

        self._update("卸载完成", 100)
        show_info(self, f"{APP_NAME} 已成功卸载")
        self.close()

    def _un_step_shortcuts(self):
        desktop = os.path.join(get_desktop_dir(), f"{APP_NAME}.lnk")
        if os.path.exists(desktop):
            os.remove(desktop)
        startmenu = get_startmenu_dir()
        if os.path.isdir(startmenu):
            shutil.rmtree(startmenu, ignore_errors=True)

    def _un_step_app(self):
        install_dir = self._get_install_dir()
        if install_dir and os.path.isdir(install_dir):
            shutil.rmtree(install_dir, ignore_errors=True)

    def _un_step_dll(self):
        # 清理两个可能的 DLL 安装位置
        dll_files = ["AeroflyBridge.dll", "AeroflyLinkDLL.dll",
                     "libgcc_s_seh-1.dll", "libstdc++-6.dll",
                     "libwinpthread-1.dll"]

        # 位置 1: 用户选择的 AF4 游戏目录
        af4_dir = self._get_af4_dir()
        if af4_dir:
            dll_dir1 = os.path.join(af4_dir, "external_dll")
            if os.path.isdir(dll_dir1):
                for f in dll_files:
                    p = os.path.join(dll_dir1, f)
                    if os.path.exists(p):
                        os.remove(p)

        # 位置 2: Documents\Aerofly FS 4\external_dll（默认路径）
        docs = os.path.join(os.environ.get("USERPROFILE", ""), "Documents")
        dll_dir2 = os.path.join(docs, "Aerofly FS 4", "external_dll")
        if os.path.isdir(dll_dir2):
            for f in dll_files:
                p = os.path.join(dll_dir2, f)
                if os.path.exists(p):
                    os.remove(p)

    def _un_step_registry(self):
        for hive in [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]:
            try:
                winreg.DeleteKey(hive, REG_KEY)
            except FileNotFoundError:
                pass

    def _get_install_dir(self):
        for hive in [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]:
            try:
                with winreg.OpenKey(hive, REG_KEY) as key:
                    val, _ = winreg.QueryValueEx(key, "InstallLocation")
                    return val
            except FileNotFoundError:
                continue
        return os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else None

    def _get_af4_dir(self):
        for hive in [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]:
            try:
                with winreg.OpenKey(hive, REG_KEY) as key:
                    val, _ = winreg.QueryValueEx(key, "AF4Dir")
                    return val
            except FileNotFoundError:
                continue
        return None


# ═══════════════════════════════════════════════════════════
#  入口
# ═══════════════════════════════════════════════════════════

def setup_dark_palette(app):
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor("#1e1e1e"))
    palette.setColor(QPalette.ColorRole.WindowText, QColor("#e0e0e0"))
    palette.setColor(QPalette.ColorRole.Base, QColor("#2d2d2d"))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor("#3a3a3a"))
    palette.setColor(QPalette.ColorRole.Text, QColor("#e0e0e0"))
    palette.setColor(QPalette.ColorRole.Button, QColor("#3a3a3a"))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor("#e0e0e0"))
    palette.setColor(QPalette.ColorRole.Highlight, QColor("#4CAF50"))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor("#ffffff"))
    app.setPalette(palette)


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet(STYLE_SHEET)
    setup_dark_palette(app)

    font = QFont("Microsoft YaHei UI", 10)
    if not QFont(font).exactMatch():
        font = QFont("Segoe UI", 10)
    app.setFont(font)

    if "--uninstall" in sys.argv:
        window = UninstallerWizard()
    else:
        window = InstallerWizard()

    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
