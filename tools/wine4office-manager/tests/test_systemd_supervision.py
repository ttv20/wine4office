#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


TESTS_DIR = Path(__file__).resolve().parent
FIXTURE = TESTS_DIR / "systemd_process_tree_fixture.py"


class SystemdProcessTreeTests(unittest.TestCase):
    """Exercise the same cgroup stop contract as the installed preload unit."""

    @classmethod
    def setUpClass(cls):
        cls.systemctl = shutil.which("systemctl")
        cls.systemd_run = shutil.which("systemd-run")
        if not cls.systemctl or not cls.systemd_run:
            raise unittest.SkipTest("systemd user tools are unavailable")
        try:
            probe = subprocess.run(
                [cls.systemctl, "--user", "show-environment"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=8,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise unittest.SkipTest(f"systemd user manager is unavailable: {error}")
        if probe.returncode:
            detail = (probe.stderr or probe.stdout).strip()
            raise unittest.SkipTest(
                f"systemd user manager is unavailable: {detail or probe.returncode}"
            )

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wine4office-systemd-")
        self.root = Path(self.temp.name)
        self.units: list[str] = []
        self.launchers: list[subprocess.Popen[str]] = []

    def tearDown(self):
        for unit in reversed(self.units):
            try:
                subprocess.run(
                    [self.systemctl, "--user", "stop", unit],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=10,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired):
                pass
            try:
                subprocess.run(
                    [self.systemctl, "--user", "reset-failed", unit],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=8,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired):
                pass
        for process in self.launchers:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        self.temp.cleanup()

    def _run(self, command: list[str], timeout: float = 12) -> subprocess.CompletedProcess:
        return subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            check=False,
        )

    def _start_unit(self, unit: str, pid_file: Path, *, scope: bool = False) -> None:
        command = [
            self.systemd_run,
            "--user",
            f"--unit={unit}",
            "--no-block",
            "--property=KillMode=control-group",
            "--property=SendSIGKILL=yes",
            "--property=TimeoutStopSec=2s",
        ]
        if scope:
            command.append("--scope")
        command.extend([sys.executable, str(FIXTURE), "parent", str(pid_file)])
        if scope:
            process = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                text=True,
            )
            self.launchers.append(process)
        else:
            result = self._run(command)
            self.assertEqual(
                result.returncode,
                0,
                msg=result.stderr.strip() or result.stdout.strip(),
            )
        self.units.append(unit)

    @staticmethod
    def _pid_from(path: Path) -> int | None:
        try:
            value = path.read_text(encoding="ascii").strip()
            pid = int(value)
        except (FileNotFoundError, OSError, ValueError):
            return None
        return pid if pid > 1 else None

    @classmethod
    def _pid_exists(cls, pid: int | None) -> bool:
        if pid is None:
            return False
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return Path("/proc", str(pid)).exists()

    def _wait_for_pids(self, pid_file: Path) -> tuple[int, int]:
        deadline = time.monotonic() + 10
        child_file = pid_file.with_name(f"{pid_file.name}.child")
        while time.monotonic() < deadline:
            parent = self._pid_from(pid_file)
            child = self._pid_from(child_file)
            if parent is not None and child is not None:
                return parent, child
            time.sleep(0.05)
        self.fail(f"process-tree fixture did not publish PIDs: {pid_file}")

    def test_control_group_stop_reaps_children_without_touching_foreign_scope(self):
        managed_file = self.root / "managed.pids"
        foreign_file = self.root / "foreign.pids"
        managed = f"wine4office-test-managed-{os.getpid()}.service"
        foreign = f"wine4office-test-foreign-{os.getpid()}.scope"
        self._start_unit(managed, managed_file)
        self._start_unit(foreign, foreign_file, scope=True)
        managed_pids = self._wait_for_pids(managed_file)
        foreign_pids = self._wait_for_pids(foreign_file)

        stopped = self._run([self.systemctl, "--user", "stop", managed], timeout=12)
        self.assertEqual(
            stopped.returncode,
            0,
            msg=stopped.stderr.strip() or stopped.stdout.strip(),
        )

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and any(
            self._pid_exists(pid) for pid in managed_pids
        ):
            time.sleep(0.05)
        self.assertFalse(
            any(self._pid_exists(pid) for pid in managed_pids),
            "systemd stop left a Wine/App-V process in the managed cgroup",
        )
        self.assertTrue(
            all(self._pid_exists(pid) for pid in foreign_pids),
            "stopping the managed unit killed a process in a foreign scope",
        )
        state = self._run(
            [self.systemctl, "--user", "show", foreign, "--property=ActiveState", "--value"],
            timeout=8,
        )
        self.assertEqual(state.returncode, 0, msg=state.stderr.strip())
        self.assertEqual(state.stdout.strip(), "active")


if __name__ == "__main__":
    unittest.main()
