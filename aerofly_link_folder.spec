# -*- mode: python ; coding: utf-8 -*-
"""
Aerofly Link PyInstaller 打包配置 — 文件夹版（更快启动，用于制作安装包）
"""

import sys
from pathlib import Path

project_root = Path(SPECPATH)

a = Analysis(
    [str(project_root / 'main.py')],
    pathex=[str(project_root)],
    binaries=[],
    datas=[
        (str(project_root / 'assets' / 'images'), 'assets/images'),
        (str(project_root / 'config' / 'settings.example.json'), 'config'),
    ],
    hiddenimports=[
        'asyncio',
        'concurrent.futures',
        'asyncio.windows_events',
        'PyQt6.QtCore',
        'PyQt6.QtGui',
        'PyQt6.QtWidgets',
        'PyQt6.QtNetwork',
        'PyQt6.sip',
        'main_window',
        'core.dll_bridge',
        'core.transponder_controller',
        'core.transponder',
        'core.fsd_client',
        'core.resource_utils',
        'core.mock_server',
        'core.mock_dll_server',
        'core.diag_logger',
        'core.async_worker',
        'core.fsd_protocol',
        'ui.connection_panel',
        'ui.connect_page',
        'ui.transponder_panel',
        'ui.flightplan_panel',
        'ui.log_panel',
        'ui.styles',
        'ui.workspace',
        'ui.status_bar',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'tkinter',
        'unittest',
        'email',
        'http',
        'xmlrpc',
        'pydoc',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=None,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=None)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='AeroflyLink',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,
)

# 文件夹版：使用 COLLECT 收集所有依赖到独立文件夹
coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='AeroflyLink',
)
