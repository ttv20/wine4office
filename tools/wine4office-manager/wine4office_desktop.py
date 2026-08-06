#!/usr/bin/env python3
"""Desktop integration helpers for the standalone Wine4Office Manager."""

from __future__ import annotations

import atexit
import os
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Mapping


_KDE_PLUGIN_ROOTS = (
    Path("/usr/lib/qt6/plugins"),
    Path("/usr/lib64/qt6/plugins"),
    Path("/usr/lib/x86_64-linux-gnu/qt6/plugins"),
    Path("/usr/lib/aarch64-linux-gnu/qt6/plugins"),
)
_plugin_bridge: Path | None = None


def is_kde_desktop(environment: Mapping[str, str] | None = None) -> bool:
    """Return whether the active desktop session identifies as KDE Plasma."""
    values = environment if environment is not None else os.environ
    desktop = ":".join(
        values.get(name, "")
        for name in ("XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION")
    ).casefold()
    return any(token in desktop for token in ("kde", "plasma"))


def _first_plugin(directory: Path, patterns: tuple[str, ...]) -> Path | None:
    for pattern in patterns:
        for candidate in sorted(directory.glob(pattern)):
            if candidate.is_file():
                return candidate
    return None


def prepare_kde_plugin_bridge(
    environment: dict[str, str] | None = None,
    plugin_roots: tuple[Path, ...] = _KDE_PLUGIN_ROOTS,
    *,
    frozen: bool | None = None,
) -> Path | None:
    """Expose only the host KDE theme plugins to a frozen Qt application.

    PyInstaller deliberately redirects Qt's library paths to its private bundle.
    A minimal temporary plugin tree lets Plasma's theme and Breeze style load
    without exposing host platform plugins, which could replace the bundled
    Wayland/X11 integration. Incompatible or absent plugins simply return None.
    """
    global _plugin_bridge
    values = environment if environment is not None else os.environ
    if frozen is None:
        frozen = bool(getattr(sys, "frozen", False))
    if not frozen or not is_kde_desktop(values):
        return None
    if values.get("QT_STYLE_OVERRIDE") or values.get("QT_QPA_PLATFORMTHEME"):
        return None
    if _plugin_bridge is not None:
        return _plugin_bridge

    selected: tuple[Path, Path] | None = None
    for root in plugin_roots:
        platform_theme = _first_plugin(
            root / "platformthemes", ("KDEPlasmaPlatformTheme*.so", "*KDE*PlatformTheme*.so")
        )
        breeze = _first_plugin(root / "styles", ("breeze*.so", "Breeze*.so"))
        if platform_theme and breeze:
            selected = platform_theme, breeze
            break
    if selected is None:
        return None

    runtime_value = values.get("XDG_RUNTIME_DIR", "").strip()
    runtime = Path(runtime_value) if runtime_value else None
    parent = (
        runtime if runtime is not None and runtime.is_dir() and os.access(runtime, os.W_OK)
        else None
    )
    bridge = Path(tempfile.mkdtemp(prefix="wine4office-qt-", dir=parent))
    try:
        for category, source in zip(("platformthemes", "styles"), selected, strict=True):
            destination = bridge / category
            destination.mkdir()
            (destination / source.name).symlink_to(source)
    except Exception:
        shutil.rmtree(bridge, ignore_errors=True)
        return None

    values["QT_QPA_PLATFORMTHEME"] = "kde"
    _plugin_bridge = bridge
    atexit.register(shutil.rmtree, bridge, ignore_errors=True)
    return bridge
