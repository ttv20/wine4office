#!/usr/bin/env python3
"""Native Qt entry point and task state for Wine 365 Manager."""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import wine365_backend as backend  # noqa: E402


def install_root() -> Path:
    configured = os.environ.get("WINE365_MANAGER_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    if HERE.name == "lib":
        return HERE.parent
    return HERE


INSTALL_ROOT = install_root()
LAUNCHER = INSTALL_ROOT / "bin/wine365-launcher" if INSTALL_ROOT != HERE else HERE / "wine365-launcher"
ICONS = INSTALL_ROOT / "icons" if INSTALL_ROOT != HERE else HERE / "icons"
FONT_HELPER = (INSTALL_ROOT / "lib/register-office-cloud-fonts.sh"
               if INSTALL_ROOT != HERE else HERE / "register-office-cloud-fonts.sh")


class ManagerState:
    """Thread-safe bridge between the Qt event loop and blocking backend work."""

    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.config = backend.load_config()
        self.task = {"running": False, "kind": "", "status": "idle", "log": ""}
        self.cancel_event = threading.Event()
        self.process: subprocess.Popen | None = None

    def update_config(self, payload: dict) -> dict:
        with self.lock:
            for key in ("prefix", "wine", "update_url"):
                if key in payload:
                    self.config[key] = str(payload[key]).strip()
            if "desktop_copy" in payload:
                self.config["desktop_copy"] = bool(payload["desktop_copy"])
            backend.save_config(self.config)
            return dict(self.config)

    def snapshot(self) -> dict:
        with self.lock:
            config = dict(self.config)
            task = dict(self.task)
        return {
            "config": config,
            "status": backend.environment_status(config["prefix"], config["wine"]),
            "version": backend.current_version(),
            "task": task,
        }

    def output(self, line: str) -> None:
        with self.lock:
            text = self.task["log"] + line + "\n"
            self.task["log"] = text[-1_000_000:]

    def set_process(self, process: subprocess.Popen | None) -> None:
        with self.lock:
            self.process = process

    def start_task(self, kind: str, operation) -> None:
        with self.lock:
            if self.task["running"]:
                raise RuntimeError("Another operation is already running.")
            self.task = {"running": True, "kind": kind, "status": "running", "log": ""}
            self.cancel_event.clear()

        def worker() -> None:
            try:
                result = operation()
                with self.lock:
                    self.task["status"] = "completed"
                    if result:
                        self.output(str(result))
            except Exception as error:  # surfaced verbatim in the native operation log
                with self.lock:
                    self.task["status"] = "failed"
                    self.output(f"ERROR: {error}")
            finally:
                with self.lock:
                    self.task["running"] = False
                    self.process = None

        threading.Thread(target=worker, name=f"wine365-{kind}", daemon=True).start()

    def cancel(self) -> None:
        self.cancel_event.set()
        with self.lock:
            process = self.process
        if process and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Wine 365 Manager")
    parser.add_argument("--install-shortcut", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--smoke-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--screenshot", metavar="PATH", help=argparse.SUPPRESS)
    parser.add_argument("--no-browser", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.install_shortcut:
        path = backend.install_manager_shortcut(LAUNCHER.with_name("wine365-manager"), ICONS)
        print(path)
        return 0

    try:
        from wine365_qt import run_manager
    except ImportError as error:
        if error.name and error.name.startswith("PySide6"):
            parser.error(
                "PySide6 is required for the native Qt interface. Install the packaged Wine 365 "
                "release or run: python3 -m pip install --user PySide6"
            )
        raise

    return run_manager(
        ManagerState(),
        launcher=LAUNCHER,
        icons=ICONS,
        font_helper=FONT_HELPER,
        smoke_test=args.smoke_test,
        screenshot=Path(args.screenshot).expanduser() if args.screenshot else None,
    )


if __name__ == "__main__":
    raise SystemExit(main())
