"""Supervise jobsvc + worker + scheduler in one process.

`heisenberg run` is supposed to feel like a normal application — one
command, browser opens, Ctrl-C cleanly shuts everything down. This
module is the supervisor.

The default mode is **in-process**: uvicorn runs the FastAPI app on
the main thread, and we spawn the worker loop and the calibration
scheduler as asyncio tasks on the same loop. ``--separate-processes``
splits them out for production deployments where you want SIGSEGV
isolation between the API and the worker.

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import asyncio
import logging
import os
import signal
import sys
import threading
import webbrowser
from pathlib import Path

from . import paths


def _start_worker_thread(stop_event: asyncio.Event) -> threading.Thread:
    """Run the jobsvc worker loop on its own asyncio loop in a thread."""

    def _run() -> None:
        from jobsvc.config import get_settings
        from jobsvc.db import session_scope
        from jobsvc.worker import process_one

        settings = get_settings()

        async def _loop() -> None:
            while not stop_event.is_set():
                try:
                    async with session_scope() as session:
                        did = await process_one(session)
                except Exception as exc:  # noqa: BLE001
                    logging.getLogger("heisenberg.worker").exception(
                        "worker iteration failed: %s", exc
                    )
                    did = False
                if not did:
                    try:
                        await asyncio.wait_for(
                            stop_event.wait(),
                            timeout=settings.worker_poll_interval_seconds,
                        )
                    except asyncio.TimeoutError:
                        pass

        asyncio.run(_loop())

    t = threading.Thread(target=_run, name="heisenberg-worker", daemon=True)
    t.start()
    return t


def _start_scheduler_thread() -> threading.Thread:
    """Run the calibration APScheduler in a thread (BlockingScheduler)."""

    def _run() -> None:
        from apscheduler.schedulers.background import BackgroundScheduler
        from apscheduler.triggers.cron import CronTrigger
        from calibration.main import run_once

        registry = (
            Path(os.environ["SPINOR_REGISTRY_ROOT"])
            if "SPINOR_REGISTRY_ROOT" in os.environ
            else None
        )
        if registry is None:
            return

        sched = BackgroundScheduler(timezone="UTC")
        sched.add_job(
            run_once,
            CronTrigger(hour=2, minute=0),
            args=[registry],
            id="nightly_refresh",
            replace_existing=True,
        )
        sched.start()
        threading.Event().wait()

    t = threading.Thread(target=_run, name="heisenberg-scheduler", daemon=True)
    t.start()
    return t


def _open_browser_when_ready(url: str, delay: float = 1.5) -> threading.Thread:
    def _open() -> None:
        import time

        time.sleep(delay)
        try:
            webbrowser.open(url, new=2)
        except Exception:  # noqa: BLE001
            pass

    t = threading.Thread(target=_open, name="heisenberg-browser", daemon=True)
    t.start()
    return t


def run_in_process(
    *,
    host: str,
    port: int,
    open_browser: bool,
    log_level: str,
    enable_worker: bool = True,
    enable_scheduler: bool = True,
) -> None:
    """Boot uvicorn, the worker, and the scheduler in one process."""
    import uvicorn

    from jobsvc.main import app  # noqa: F401  -- ensure app is created

    stop_event = asyncio.Event()

    if enable_worker:
        _start_worker_thread(stop_event)
    if enable_scheduler:
        _start_scheduler_thread()
    if open_browser:
        _open_browser_when_ready(f"http://{host}:{port}/")

    config = uvicorn.Config(
        "jobsvc.main:app",
        host=host,
        port=port,
        log_level=log_level.lower(),
        access_log=False,
    )
    server = uvicorn.Server(config)

    def _stop(*_: object) -> None:
        stop_event.set()
        server.should_exit = True

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(sig, _stop)
        except ValueError:
            pass

    server.run()
    stop_event.set()


def write_pid() -> None:
    paths.pid_file().write_text(str(os.getpid()), encoding="utf-8")


def clear_pid() -> None:
    pf = paths.pid_file()
    try:
        pf.unlink()
    except FileNotFoundError:
        pass


__all__ = ["run_in_process", "write_pid", "clear_pid"]
