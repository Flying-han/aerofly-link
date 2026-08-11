# -*- coding: utf-8 -*-
"""
AsyncWorker
===========
后台线程运行 asyncio 事件循环，避免阻塞 UI 主线程。
从 main_window.py 中拆出，降低主窗口文件的复杂度。
"""
import asyncio
import threading
from typing import Optional

from PyQt6.QtCore import QThread, pyqtSignal


class AsyncWorker(QThread):
    """后台线程，运行 asyncio 事件循环，避免阻塞 UI 主线程。"""

    _instance: Optional["AsyncWorker"] = None
    task_ready = pyqtSignal(object)  # coroutine

    def __init__(self, parent=None):
        super().__init__(parent)
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._running = False
        self.daemon = True
        AsyncWorker._instance = self

    def run(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._running = True
        self.task_ready.connect(self._execute_task)
        self._loop.run_forever()

    def _execute_task(self, coro):
        if self._loop and self._loop.is_running():
            future = asyncio.run_coroutine_threadsafe(coro, self._loop)

            def _log_exc(f):
                try:
                    f.result()
                except Exception as e:
                    import traceback

                    traceback.print_exc()
                    print(f"[AsyncWorker] Unhandled task exception: {e}")

            future.add_done_callback(_log_exc)

    def stop(self):
        if self._loop:
            self._loop.call_soon_threadsafe(self._loop.stop)
        self._running = False

    @staticmethod
    def instance() -> Optional["AsyncWorker"]:
        return AsyncWorker._instance

    @staticmethod
    def run_async(coro):
        """提交协程到后台线程执行；若 worker 未启动则用临时线程兜底。"""
        worker = AsyncWorker._instance
        if worker and worker._running:
            worker.task_ready.emit(coro)
        else:
            def _run():
                try:
                    asyncio.run(coro)
                except Exception as e:
                    print(f"Async fallback error: {e}")

            threading.Thread(target=_run, daemon=True).start()
