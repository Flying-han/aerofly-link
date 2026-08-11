# -*- mode: python ; coding: utf-8 -*-
"""
Aerofly Link 安装器 PyInstaller 打包配置
输出单个 AeroflyLink_Setup.exe，内含应用 zip 和 DLL zip
"""

import os

block_cipher = None

a = Analysis(
    ['installer.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('data/aerofly_link.zip', '.'),
        ('data/dll.zip', '.'),
    ],
    hiddenimports=[
        'PyQt6',
        'PyQt6.QtCore',
        'PyQt6.QtGui',
        'PyQt6.QtWidgets',
        'PyQt6.sip',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'PyQt5', 'PySide6', 'PySide2',
        'numpy', 'pandas', 'matplotlib', 'scipy',
        'PIL', 'cv2', 'requests', 'urllib3',
        'asyncio', 'email', 'http', 'xmlrpc',
        'PyInstaller', 'setuptools', 'pip',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='AeroflyLink_Setup',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,
)
