# -*- coding: utf-8 -*-
"""
资源路径工具
处理 PyInstaller 打包前后的路径差异。

PyInstaller 布局：
  - 单文件(one-file) 打包：运行时 sys._MEIPASS 指向临时解压目录，datas 在其顶层。
  - 单文件夹(one-folder) 打包(PyInstaller 6.x)：exe 旁边有 _internal/ 目录，
    datas 位于 <exedir>/_internal/assets、<exedir>/_internal/config。
  - 开发环境：项目根目录。
"""
import sys
import os
from pathlib import Path


def _is_one_file() -> bool:
    """单文件打包时 PyInstaller 会设置 sys._MEIPASS。"""
    return getattr(sys, 'frozen', False) and bool(getattr(sys, '_MEIPASS', None))


def get_base_path() -> Path:
    """获取应用基础路径（优先返回 datas 所在目录）。"""
    if _is_one_file():
        return Path(sys._MEIPASS)
    if getattr(sys, 'frozen', False):
        # 单文件夹打包：以 exe 所在目录为基准，_internal 在同级
        return Path(sys.executable).parent
    return Path(__file__).parent.parent  # core/ -> aerofly_link/


def get_asset_path(relative_path: str) -> Path:
    """
    获取只读资源文件路径（map.html 等）。
    兼容单文件(_MEIPASS) 与单文件夹(_internal 子目录) 两种布局。
    """
    base = get_base_path()
    candidates = [base / relative_path]
    # 单文件夹(PyInstaller 6.x)：datas 实际在 _internal/ 下
    if getattr(sys, 'frozen', False) and not _is_one_file():
        exe_dir = Path(sys.executable).parent
        candidates.append(exe_dir / '_internal' / relative_path)
    for c in candidates:
        if c.exists():
            return c
    # 都不存在时返回首选（便于报错信息指向真实位置）
    return candidates[0]


def get_config_dir() -> Path:
    """
    获取可写入的配置目录
    - 开发环境：项目下的 config/
    - 打包环境：%APPDATA%/AeroflyLink/
    """
    if getattr(sys, 'frozen', False):
        config_dir = Path(os.environ.get("APPDATA", Path.home())) / "AeroflyLink"
    else:
        config_dir = Path(__file__).parent.parent / "config"
    config_dir.mkdir(parents=True, exist_ok=True)
    return config_dir


def get_config_path(filename: str = "settings.json") -> Path:
    """获取配置文件完整路径"""
    return get_config_dir() / filename
