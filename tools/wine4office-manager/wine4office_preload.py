#!/usr/bin/env python3
"""Lightweight entry point for the Wine4Office background-service worker."""

from __future__ import annotations

import sys
from pathlib import Path

import wine4office_backend as backend


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if len(arguments) != 2:
        print(
            "Usage: wine4office-preload-worker BINDING_PATH STATUS_PATH",
            file=sys.stderr,
        )
        return 2
    return backend.run_preload_worker(Path(arguments[0]), Path(arguments[1]))


if __name__ == "__main__":
    raise SystemExit(main())
