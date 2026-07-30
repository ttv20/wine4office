#!/usr/bin/env python3
"""Versioned post-install hooks executed by the newly installed manager."""

from __future__ import annotations

import json
import os
import time
from pathlib import Path
from typing import Callable

import wine4office_backend as backend


POST_INSTALL_SCHEMA = 1
Output = Callable[[str], None]


def marker_path() -> Path:
    return backend.data_home() / "wine4office/post-install.json"


def _applied(version: str) -> bool:
    try:
        marker = json.loads(marker_path().read_text())
    except (OSError, ValueError, json.JSONDecodeError):
        return False
    return (
        marker.get("schema") == POST_INSTALL_SCHEMA
        and marker.get("manager_version") == version
    )


def _write_marker(version: str) -> None:
    path = marker_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(
        f".{path.name}.{os.getpid()}.{time.time_ns()}.tmp"
    )
    try:
        temporary.write_text(json.dumps({
            "manager_version": version,
            "schema": POST_INSTALL_SCHEMA,
        }, sort_keys=True) + "\n")
        temporary.chmod(0o644)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _migrate_managed_shortcuts(config: dict, font_helper: Path,
                               output: Output) -> dict:
    result = backend.refresh_managed_app_shortcuts(
        config["prefix"], config["wine"], helper=font_helper,
    )
    updated = len(result["updated"])
    output(f"Post-install: refreshed {updated} existing managed shortcut file(s).")
    for app, reason in result["skipped"].items():
        output(f"Post-install: skipped {app}: {reason}")
    return result


POST_INSTALL_HOOKS = (_migrate_managed_shortcuts,)


def run_post_install(config: dict, font_helper: Path, output: Output = print,
                     force: bool = False) -> dict:
    """Run every hook once per manager version; forced runs remain idempotent."""
    version = backend.current_version()
    if not force and _applied(version):
        return {"ran": False, "manager_version": version, "hooks": []}

    hook_results = []
    for hook in POST_INSTALL_HOOKS:
        hook_results.append(hook(config, font_helper, output))
    _write_marker(version)
    output(f"Post-install: completed for Wine4OfficeManager {version}.")
    return {
        "ran": True,
        "manager_version": version,
        "hooks": hook_results,
    }
