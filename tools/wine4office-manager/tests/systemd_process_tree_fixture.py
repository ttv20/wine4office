#!/usr/bin/env python3
"""Keep a parent and child alive for systemd cgroup supervision tests."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path


def _write_pid(path: Path, pid: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}")
    temporary.write_text(f"{pid}\n", encoding="ascii")
    os.replace(temporary, path)


def _child(path: Path) -> int:
    # The parent exits on SIGTERM, while this child ignores it. A cgroup stop
    # must therefore reach and reap the child rather than only the leader.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    _write_pid(path, os.getpid())
    while True:
        time.sleep(0.2)


def _parent(path: Path) -> int:
    stopping = False

    def stop(_signum, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    child_path = path.with_name(f"{path.name}.child")
    child = subprocess.Popen(
        [sys.executable, str(Path(__file__).resolve()), "child", str(child_path)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        close_fds=True,
    )
    _write_pid(path, os.getpid())
    while not stopping:
        time.sleep(0.2)
    # Do not wait for or terminate the child here. The unit's cgroup is the
    # owner responsible for terminating and reaping both processes.
    return 0


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] not in {"parent", "child"}:
        print(f"usage: {argv[0]} parent|child PID_FILE", file=sys.stderr)
        return 2
    path = Path(argv[2]).resolve()
    if argv[1] == "child":
        return _child(path)
    return _parent(path)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
