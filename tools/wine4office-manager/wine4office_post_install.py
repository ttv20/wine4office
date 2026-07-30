#!/usr/bin/env python3
"""Versioned post-install hooks executed by the newly installed manager."""

from __future__ import annotations

import json
import os
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import wine4office_backend as backend


POST_INSTALL_SCHEMA = 2
Output = Callable[[str], None]


@dataclass(frozen=True)
class PostInstallContext:
    config: dict
    font_helper: Path
    manager_command: list[str]
    icons: Path
    output: Output


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


def _migrate_managed_shortcuts(context: PostInstallContext) -> dict:
    result = backend.refresh_managed_app_shortcuts(
        context.config["prefix"], context.config["wine"],
        helper=context.font_helper,
    )
    updated = len(result["updated"])
    context.output(
        f"Post-install: refreshed {updated} existing managed shortcut file(s)."
    )
    for app, reason in result["skipped"].items():
        context.output(f"Post-install: skipped {app}: {reason}")
    return result


def _refresh_manager_shortcut(context: PostInstallContext) -> dict:
    result = backend.refresh_manager_shortcut(
        context.manager_command, context.icons,
    )
    status = "refreshed" if result["updated"] else "not installed; left unchanged"
    context.output(f"Post-install: manager application shortcut {status}.")
    return result


POST_INSTALL_HOOKS = (
    _migrate_managed_shortcuts,
    _refresh_manager_shortcut,
)


def run_post_install(config: dict, font_helper: Path, manager_command: list[str],
                     icons: Path, output: Output = print,
                     force: bool = False) -> dict:
    """Run every hook once per manager version; forced runs remain idempotent."""
    version = backend.current_version()
    if not force and _applied(version):
        return {"ran": False, "manager_version": version, "hooks": []}

    context = PostInstallContext(
        config=config,
        font_helper=font_helper,
        manager_command=manager_command,
        icons=icons,
        output=output,
    )
    hook_results = [hook(context) for hook in POST_INSTALL_HOOKS]
    _write_marker(version)
    output(f"Post-install: completed for Wine4Office Manager {version}.")
    return {
        "ran": True,
        "manager_version": version,
        "hooks": hook_results,
    }
