#!/usr/bin/env python3
"""Backend operations for Wine4OfficeManager."""

from __future__ import annotations

import csv
import io
import fcntl
import hashlib
from html.parser import HTMLParser
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import stat
import struct
import ssl
import tarfile
import tempfile
import sys
import time
import uuid
import urllib.parse
import urllib.request
from contextlib import contextmanager
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable

PathValue = str | os.PathLike[str]
OFFICE_TELEMETRY_POLICY_KEY = (
    r"HKCU\Software\Policies\Microsoft\Office\Common\ClientTelemetry"
)
OFFICE_TELEMETRY_POLICY_VALUE = "SendTelemetry"
OFFICE_TELEMETRY_DISABLED = "3"


APP_META = {
    "word": {
        "name": "Microsoft Word (Wine4Office)",
        "exe": "WINWORD.EXE",
        "icon": "wine4office-word.svg",
        "categories": "Office;WordProcessor;",
        "mime": "application/msword;application/vnd.openxmlformats-officedocument.wordprocessingml.document;",
    },
    "excel": {
        "name": "Microsoft Excel (Wine4Office)",
        "exe": "EXCEL.EXE",
        "icon": "wine4office-excel.svg",
        "categories": "Office;Spreadsheet;",
        "mime": "application/vnd.ms-excel;application/vnd.openxmlformats-officedocument.spreadsheetml.sheet;",
    },
    "powerpoint": {
        "name": "Microsoft PowerPoint (Wine4Office)",
        "exe": "POWERPNT.EXE",
        "icon": "wine4office-powerpoint.svg",
        "categories": "Office;Presentation;",
        "mime": "application/vnd.ms-powerpoint;application/vnd.openxmlformats-officedocument.presentationml.presentation;",
    },
    "outlook": {
        "name": "Microsoft Outlook (Wine4Office)",
        "exe": "OUTLOOK.EXE",
        "icon": "wine4office-outlook.svg",
        "categories": "Office;Email;Network;",
        "mime": "x-scheme-handler/mailto;",
    },
    "setlang": {
        "name": "Microsoft Office Language Preferences (Wine4Office)",
        "exe": "SETLANG.EXE",
        "icon": "wine4office-setlang.svg",
        "categories": "Office;Settings;",
        "mime": "",
    },
}

OFFICE_CUSTOMIZATION_URL = "https://config.office.com/deploymentsettings"
OFFICE_PRODUCTS = (
    {
        "label": "Microsoft 365 Apps for enterprise",
        "product_id": "O365ProPlusRetail",
        "channel": "Current",
    },
    {
        "label": "Microsoft 365 Apps for business",
        "product_id": "O365BusinessRetail",
        "channel": "Current",
    },
    {
        "label": "Office LTSC Professional Plus 2024",
        "product_id": "ProPlus2024Volume",
        "channel": "PerpetualVL2024",
    },
    {
        "label": "Office LTSC Standard 2024",
        "product_id": "Standard2024Volume",
        "channel": "PerpetualVL2024",
    },
)

_OFFICE_PRODUCT_BY_ID = {record["product_id"]: record for record in OFFICE_PRODUCTS}
_OFFICE_LANGUAGE_IDS = frozenset({
    "ar-sa", "bg-bg", "zh-cn", "zh-tw", "hr-hr", "cs-cz", "da-dk", "nl-nl",
    "en-us", "et-ee", "fi-fi", "fr-fr", "de-de", "el-gr", "he-il", "hi-in",
    "hu-hu", "id-id", "it-it", "ja-jp", "kk-kz", "ko-kr", "lv-lv", "lt-lt",
    "ms-my", "nb-no", "pl-pl", "pt-br", "pt-pt", "ro-ro", "ru-ru",
    "sr-latn-rs", "sk-sk", "sl-si", "es-es", "sv-se", "th-th", "tr-tr",
    "uk-ua", "vi-vn",
})
_ODT_DOWNLOAD_PAGE = "https://www.microsoft.com/en-us/download/details.aspx?id=49117"
_ODT_PAGE_HOSTS = frozenset({"www.microsoft.com"})
_ODT_DOWNLOAD_HOSTS = frozenset({"download.microsoft.com"})
_ODT_LINK_PATTERN = re.compile(
    r"/officedeploymenttool_[0-9]{4,6}-[0-9]{4,6}\.exe$", re.IGNORECASE
)
MAX_OFFICE_XML_SIZE = 1024 * 1024
MAX_ODT_PAGE_SIZE = 4 * 1024 * 1024
MAX_ODT_DOWNLOAD_SIZE = 32 * 1024 * 1024
MAX_ODT_SETUP_SIZE = 64 * 1024 * 1024

TOOL_META = {
    "winecfg": ("winecfg", []),
    "regedit": ("regedit", []),
    "control": ("control", []),
    "explorer": ("winefile", []),
    "cmd": ("wine", ["cmd.exe"]),
    "taskmgr": ("wine", ["taskmgr.exe"]),
    "uninstaller": ("wine", ["uninstaller.exe"]),
}

Output = Callable[[str], None]
DEFAULT_METADATA_URL = (
    "https://github.com/ttv20/wine4office/releases/latest/download/release.json"
)

MAX_METADATA_SIZE = 1_048_576
MAX_MANAGER_SIZE = 1024**3
MAX_WINE_SIZE = 8 * 1024**3
MAX_WINE_ARCHIVE_MEMBERS = 100_000
MAX_WINE_FILE_SIZE = 4 * 1024**3
MAX_WINE_EXTRACTED_SIZE = 16 * 1024**3
DEFAULT_UPDATE_CHANNEL = "stable"
VERSION_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}")
_STANDALONE_VERSION_CACHE = None


def data_home() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")).expanduser()


def config_home() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")).expanduser()


def cache_home() -> Path:
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")).expanduser()


def config_path() -> Path:
    return config_home() / "wine4office/config.json"


def installed_root() -> Path | None:
    configured = os.environ.get("WINE4OFFICE_MANAGER_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    if getattr(sys, "frozen", False):
        executable = Path(sys.executable).expanduser().resolve()
        candidate = executable.parent.parent
        marker = candidate / "STANDALONE"
        if executable.name == "Wine4OfficeManager" and executable.parent.name == "bin":
            try:
                if marker.read_text(errors="replace").strip() == "Wine4OfficeManager":
                    return candidate
            except OSError:
                pass
    here = Path(__file__).resolve().parent
    return here.parent if here.name == "lib" else None

def standalone_manager_version_path(target: Path | None = None) -> Path | None:
    if target is None:
        if not getattr(sys, "frozen", False):
            return None
        target = Path(sys.executable).resolve()
    return target.with_name(f"{target.name}.version")


def _bound_standalone_version(target: Path) -> str | None:
    sidecar = standalone_manager_version_path(target)
    try:
        payload = json.loads(sidecar.read_text())
        version = payload["version"]
        expected_digest = payload["sha256"]
        if (not isinstance(version, str) or not VERSION_PATTERN.fullmatch(version)
                or not isinstance(expected_digest, str)
                or not re.fullmatch(r"[0-9a-f]{64}", expected_digest)):
            return None
        target_stat = target.stat()
        sidecar_stat = sidecar.stat()
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return None
    key = (
        target_stat.st_dev, target_stat.st_ino, target_stat.st_size, target_stat.st_mtime_ns,
        sidecar_stat.st_dev, sidecar_stat.st_ino, sidecar_stat.st_size, sidecar_stat.st_mtime_ns,
    )
    global _STANDALONE_VERSION_CACHE
    if _STANDALONE_VERSION_CACHE and _STANDALONE_VERSION_CACHE[0] == key:
        return _STANDALONE_VERSION_CACHE[1]
    digest = hashlib.sha256()
    try:
        with target.open("rb") as executable:
            for chunk in iter(lambda: executable.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError:
        return None
    result = version if digest.hexdigest() == expected_digest else None
    _STANDALONE_VERSION_CACHE = (key, result)
    return result


def current_version() -> str:
    """Return the independently installed Wine4OfficeManager version."""
    root = installed_root()
    candidates = [root / "VERSION"] if root else []
    for version_file in candidates:
        if version_file.is_file():
            value = version_file.read_text(errors="replace").strip()
            if value:
                return value
    if getattr(sys, "frozen", False):
        persisted = _bound_standalone_version(Path(sys.executable).resolve())
        if persisted:
            return persisted
        embedded = Path(__file__).resolve().parent / "VERSION"
        if embedded.is_file():
            value = embedded.read_text(errors="replace").strip()
            if value:
                return value
    return "development"


def current_wine_version() -> str:
    """Return the runner version, including legacy combined installations."""
    root = installed_root()
    candidates = [root / "WINE_VERSION", root / "VERSION"] if root else [
        data_home() / "wine4office/WINE_VERSION"
    ]
    for version in candidates:
        if version.is_file():
            value = version.read_text(errors="replace").strip()
            if value:
                return value
    return "development"


def configured_update_url() -> str:
    root = installed_root()
    address = root / "UPDATE_URL" if root else None
    if address and address.is_file():
        value = address.read_text(errors="replace").strip()
        if value:
            return value
    return DEFAULT_METADATA_URL


def configured_update_channel() -> str:
    root = installed_root()
    candidates = [root / "UPDATE_CHANNEL"] if root else []
    if getattr(sys, "frozen", False):
        candidates.append(Path(__file__).resolve().parent / "CHANNEL")
    for channel_path in candidates:
        if not channel_path.is_file():
            continue
        channel = channel_path.read_text(errors="replace").strip()
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", channel):
            raise ValueError("Configured update channel is invalid.")
        return channel
    return DEFAULT_UPDATE_CHANNEL


def default_config() -> dict:
    return {
        "prefix": str(Path.home() / ".wine4office"),
        "wine": detect_wine(),
        "desktop_copy": False,
        "use_x11": True,
        "office_telemetry_disabled": {},
        "update_url": configured_update_url(),
        "skipped_updates": {},
    }


def detect_wine() -> str:
    candidates: list[Path] = []
    if os.environ.get("WINE4OFFICE_WINE"):
        candidates.append(Path(os.environ["WINE4OFFICE_WINE"]).expanduser())
    candidates.append(data_home() / "wine4office/runner/bin/wine")

    bottles = data_home() / "bottles/bottles/Wine4Office/bottle.yml"
    if bottles.is_file():
        match = re.search(r"^Runner:\s*(\S+)\s*$", bottles.read_text(errors="replace"), re.MULTILINE)
        if match:
            candidates.append(data_home() / "bottles/runners" / match.group(1) / "bin/wine")

    system_wine = shutil.which("wine")
    if system_wine:
        candidates.append(Path(system_wine))
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate.resolve())
    return str(data_home() / "wine4office/runner/bin/wine")


def load_config() -> dict:
    result = default_config()
    path = config_path()
    if path.is_file():
        try:
            saved = json.loads(path.read_text())
            if isinstance(saved, dict):
                for key in result:
                    if key in saved:
                        if key == "update_url" and not str(saved[key]).strip():
                            continue
                        result[key] = saved[key]
        except (OSError, ValueError):
            pass
    return result


def save_config(config: dict) -> None:
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    safe = {key: config[key] for key in default_config() if key in config}
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(safe, indent=2) + "\n")
    os.replace(temporary, path)


def environment_config_key(prefix_value: PathValue) -> str:
    """Return the stable per-prefix key used by manager-owned settings."""
    return str(normalize_path(prefix_value))


def office_telemetry_disabled(config: dict, prefix_value: PathValue | None = None) -> bool:
    """Return whether Wine4Office owns the telemetry policy in one prefix."""
    policies = config.get("office_telemetry_disabled", {})
    if not isinstance(policies, dict):
        return False
    prefix = config.get("prefix", "") if prefix_value is None else prefix_value
    try:
        return policies.get(environment_config_key(prefix)) is True
    except (TypeError, ValueError):
        return False


def set_office_telemetry_disabled(config: dict, prefix_value: PathValue,
                                  disabled: bool) -> dict:
    """Copy config and update only Wine4Office's ownership record for one prefix."""
    result = dict(config)
    saved = result.get("office_telemetry_disabled", {})
    policies = dict(saved) if isinstance(saved, dict) else {}
    key = environment_config_key(prefix_value)
    if disabled:
        policies[key] = True
    else:
        policies.pop(key, None)
    result["office_telemetry_disabled"] = policies
    return result


def normalize_path(value: PathValue) -> Path:
    raw_value = value.strip() if isinstance(value, str) else os.fspath(value)
    return Path(os.path.expandvars(raw_value)).expanduser().resolve(strict=False)


def validate_prefix(value: PathValue) -> Path:
    raw_value = value.strip() if isinstance(value, str) else os.fspath(value)
    if not raw_value:
        raise ValueError("The Wine environment path is empty.")
    prefix = normalize_path(raw_value)
    home = Path.home().resolve()
    unsafe_roots = {
        Path("/"), home,
        *(Path("/") / name for name in (
            "bin", "boot", "dev", "etc", "home", "lib", "lib64", "media", "mnt",
            "opt", "proc", "root", "run", "sbin", "srv", "sys", "tmp", "usr", "var",
        )),
    }
    if prefix in unsafe_roots:
        raise ValueError(f"Refusing unsafe Wine environment path: {prefix}")
    return prefix

def paths_equivalent(first_value: str, second_value: str) -> bool:
    """Return whether two configured paths identify the same filesystem object."""
    if not first_value.strip() or not second_value.strip():
        return first_value.strip() == second_value.strip()
    first = Path(os.path.expandvars(first_value.strip())).expanduser()
    second = Path(os.path.expandvars(second_value.strip())).expanduser()
    try:
        if first.exists() and second.exists() and os.path.samefile(first, second):
            return True
    except OSError:
        pass
    return first.resolve(strict=False) == second.resolve(strict=False)


def has_wine_prefix_layout(prefix: Path) -> bool:
    """Return whether all filesystem markers required for a Wine prefix exist."""
    try:
        return (
            (prefix / "system.reg").is_file()
            and (prefix / "user.reg").is_file()
            and (prefix / "drive_c").is_dir()
            and (prefix / "dosdevices").is_dir()
        )
    except OSError:
        return False


def classify_prefix(value: str) -> str:
    """Classify an environment target without changing it."""
    prefix = validate_prefix(value)
    if not prefix.exists():
        return "missing"
    if not prefix.is_dir():
        return "unsafe"
    try:
        if not any(prefix.iterdir()):
            return "empty"
    except OSError:
        return "unsafe"
    return "valid" if has_wine_prefix_layout(prefix) else "unsafe"


def validate_environment_deletion(old_value: str, new_value: str) -> tuple[Path, Path]:
    """Validate that deleting old cannot affect the replacement environment."""
    old = validate_prefix(old_value)
    new = validate_prefix(new_value)
    if classify_prefix(str(old)) != "valid":
        raise ValueError(f"Refusing to delete a non-prefix environment: {old}")
    if paths_equivalent(str(old), str(new)) or old in new.parents or new in old.parents:
        raise ValueError(
            f"Refusing to delete overlapping Wine environments: {old} and {new}"
        )
    return old, new


def stage_environment_deletion(old_value: str, new_value: str, wine_value: str,
                               output: Output) -> Path:
    """Atomically move an approved old prefix aside so it can still be restored."""
    old, _ = validate_environment_deletion(old_value, new_value)
    stop_wine(str(old), wine_value)
    staged = old.with_name(
        f".{old.name}.wine4office-delete-{os.getpid()}-{time.time_ns()}"
    )
    if staged.exists():
        raise FileExistsError(f"Temporary deletion path already exists: {staged}")
    output(f"Staging the old Wine environment for deletion: {old}")
    old.rename(staged)
    return staged


def restore_staged_environment(staged: Path, old_value: str) -> None:
    old = validate_prefix(old_value)
    if old.exists():
        raise FileExistsError(f"Cannot restore the old environment over: {old}")
    staged.rename(old)


def finish_staged_environment_deletion(staged: Path, output: Output) -> None:
    approved = validate_prefix(str(staged))
    if classify_prefix(str(approved)) != "valid":
        raise ValueError(f"Refusing to delete staged data that is not a Wine prefix: {approved}")
    output(f"Deleting the approved old Wine environment: {approved}")
    shutil.rmtree(approved)

def discard_initialized_environment(prefix_value: str, output: Output) -> None:
    prefix = validate_prefix(prefix_value)
    if classify_prefix(str(prefix)) != "valid":
        raise ValueError(f"Refusing to discard a path that is not a Wine prefix: {prefix}")
    output(f"Discarding the uncommitted Wine environment: {prefix}")
    shutil.rmtree(prefix)


def require_wine(value: str) -> Path:
    wine = normalize_path(value)
    if not wine.is_file() or not os.access(wine, os.X_OK):
        raise FileNotFoundError(f"Wine executable is missing or not executable: {wine}")
    return wine


def wine_environment(prefix: str | Path, wine: str | Path,
                     use_x11: bool = True) -> dict[str, str]:
    env = os.environ.copy()
    env.update({
        "WINEPREFIX": str(prefix),
        "WINEARCH": "win64",
        "WINEDLLOVERRIDES": env.get("WINEDLLOVERRIDES", "riched20=n;mscoree=;mshtml=b"),
    })
    wine_bin = str(Path(wine).parent)
    env["PATH"] = wine_bin + os.pathsep + env.get("PATH", "")
    if use_x11:
        env.pop("WAYLAND_DISPLAY", None)
    elif env.get("WAYLAND_DISPLAY"):
        env.pop("DISPLAY", None)
    return env


def apply_office_telemetry_policy(prefix_value: str, wine_value: str, disabled: bool,
                                  *, remove_managed: bool = False,
                                  use_x11: bool = True) -> bool:
    """Set or remove only Office's official SendTelemetry user policy value."""
    prefix = validate_prefix(prefix_value)
    if classify_prefix(str(prefix)) != "valid":
        raise ValueError(f"Office telemetry policy requires a valid Wine prefix: {prefix}")
    wine = require_wine(wine_value)
    env = wine_environment(prefix, wine, use_x11)
    if disabled:
        subprocess.run(
            [
                str(wine), "reg", "add", OFFICE_TELEMETRY_POLICY_KEY,
                "/v", OFFICE_TELEMETRY_POLICY_VALUE, "/t", "REG_DWORD",
                "/d", OFFICE_TELEMETRY_DISABLED, "/f",
            ],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30, check=True,
        )
        return True
    if not remove_managed:
        return False

    query_command = [
        str(wine), "reg", "query", OFFICE_TELEMETRY_POLICY_KEY,
        "/v", OFFICE_TELEMETRY_POLICY_VALUE,
    ]
    query = subprocess.run(
        query_command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30, check=False,
    )
    if query.returncode == 1:
        return False
    if query.returncode:
        raise subprocess.CalledProcessError(
            query.returncode, query_command, output=query.stdout, stderr=query.stderr
        )
    if not re.search(r"\bREG_DWORD\s+(?:0x0*3|3)\b", query.stdout, re.IGNORECASE):
        return False
    subprocess.run(
        [
            str(wine), "reg", "delete", OFFICE_TELEMETRY_POLICY_KEY,
            "/v", OFFICE_TELEMETRY_POLICY_VALUE, "/f",
        ],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        timeout=30, check=True,
    )
    return True


def sibling_tool(wine: Path, name: str) -> Path | None:
    candidate = wine.parent / name
    return candidate if candidate.is_file() and os.access(candidate, os.X_OK) else None


def _stream_command(command: list[str], env: dict[str, str], output: Output, cwd: Path | None = None,
                    cancel_event=None, process_callback=None) -> None:
    output("$ " + " ".join(command))
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        start_new_session=True,
    )
    if process_callback:
        process_callback(process)
    assert process.stdout is not None
    try:
        while True:
            line = process.stdout.readline()
            if line:
                output(line.rstrip())
            if process.poll() is not None:
                for remaining in process.stdout:
                    output(remaining.rstrip())
                break
            if cancel_event is not None and cancel_event.is_set():
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                raise RuntimeError("Operation cancelled.")
    finally:
        if process.stdout:
            process.stdout.close()
        if process_callback:
            process_callback(None)
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, command)


def stop_wine(prefix_value: str, wine_value: str, use_x11: bool = True) -> None:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    env = wine_environment(prefix, wine, use_x11)

    subprocess.run(
        [str(wine), "wine4officeclose.exe"],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=20,
        check=True,
    )

    wineserver = sibling_tool(wine, "wineserver")
    if wineserver:
        subprocess.run([str(wineserver), "-k"], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=15, check=False)
        subprocess.run([str(wineserver), "-w"], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=15, check=True)


def create_environment(prefix_value: str, wine_value: str, recreate: bool, output: Output,
                       cancel_event=None, process_callback=None) -> str:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    kind = classify_prefix(str(prefix))
    if kind == "unsafe":
        raise FileExistsError(
            f"Refusing to initialize a nonempty directory that is not a Wine prefix: {prefix}"
        )
    if kind == "valid" and not recreate:
        raise FileExistsError(f"The environment already exists and is not empty: {prefix}")

    backup: Path | None = None
    if recreate and prefix.exists():
        stop_wine(str(prefix), str(wine))
        backup = prefix.with_name(f".{prefix.name}.wine4office-backup-{int(time.time())}")
        if backup.exists():
            raise FileExistsError(f"Backup path already exists: {backup}")
        output(f"Moving the current environment to {backup}")
        prefix.rename(backup)

    try:
        if cancel_event is not None and cancel_event.is_set():
            raise RuntimeError("Operation cancelled.")
        prefix.parent.mkdir(parents=True, exist_ok=True)
        wineboot = sibling_tool(wine, "wineboot")
        command = [str(wineboot), "-u"] if wineboot else [str(wine), "wineboot.exe", "-u"]
        _stream_command(
            command, wine_environment(prefix, wine), output,
            cancel_event=cancel_event, process_callback=process_callback,
        )
        _stream_command([
            str(wine), "reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Graphics",
            "/d", "x11,wayland", "/f",
        ], wine_environment(prefix, wine), output,
            cancel_event=cancel_event, process_callback=process_callback)
        if classify_prefix(str(prefix)) != "valid":
            raise RuntimeError(f"Wine initialization did not create a valid prefix at: {prefix}")
    except Exception:
        if prefix.exists():
            shutil.rmtree(prefix, ignore_errors=True)
        if backup and backup.exists():
            output("Initialization failed; restoring the previous environment.")
            backup.rename(prefix)
        raise

    if backup and backup.exists():
        output("Initialization succeeded; removing the temporary backup.")
        shutil.rmtree(backup)
    return f"Wine environment is ready at {prefix}"


def office_candidates(prefix: Path, executable: str) -> Iterable[Path]:
    roots = [
        prefix / "drive_c/Program Files/Microsoft Office/root/Office16",
        prefix / "drive_c/Program Files (x86)/Microsoft Office/root/Office16",
        prefix / "drive_c/Program Files/Microsoft Office/Office16",
        prefix / "drive_c/Program Files (x86)/Microsoft Office/Office16",
    ]
    for root in roots:
        yield root / executable


def find_office_app(prefix_value: str, app: str) -> Path | None:
    if app not in APP_META:
        raise ValueError(f"Unknown Office application: {app}")
    prefix = validate_prefix(prefix_value)
    executable = APP_META[app]["exe"]
    for candidate in office_candidates(prefix, executable):
        if candidate.is_file():
            return candidate
    return None


def environment_status(prefix_value: str, wine_value: str) -> dict:
    try:
        prefix = validate_prefix(prefix_value)
        wine = normalize_path(wine_value)
        apps = {app: bool(find_office_app(str(prefix), app)) for app in APP_META}
        return {
            "prefix_exists": (prefix / "system.reg").is_file(),
            "wine_exists": wine.is_file() and os.access(wine, os.X_OK),
            "apps": apps,
        }
    except (OSError, ValueError):
        return {"prefix_exists": False, "wine_exists": False, "apps": {app: False for app in APP_META}}


def prepare_office_building_blocks(prefix: Path) -> int:
    """Seed Word's per-user gallery when Wine does not expose the built-in template."""
    office_roots = [
        prefix / "drive_c/Program Files/Microsoft Office/root/Office16",
        prefix / "drive_c/Program Files (x86)/Microsoft Office/root/Office16",
        prefix / "drive_c/Program Files/Microsoft Office/Office16",
        prefix / "drive_c/Program Files (x86)/Microsoft Office/Office16",
    ]
    users_root = prefix / "drive_c/users"
    if not users_root.is_dir():
        return 0

    ignored_users = {"all users", "default", "default user", "public"}
    sources: list[tuple[str, Path]] = []
    for office_root in office_roots:
        document_parts = office_root / "Document Parts"
        if not document_parts.is_dir():
            continue
        for source in document_parts.glob("*/16/Built-In Building Blocks.dotx"):
            if source.is_file():
                sources.append((source.parent.parent.name, source))

    copied = 0
    for user in users_root.iterdir():
        if not user.is_dir() or user.name.lower() in ignored_users:
            continue
        for locale, source in sources:
            target = (user / "AppData/Roaming/Microsoft/Document Building Blocks" /
                      locale / "16/Building Blocks.dotx")
            # Never replace a user's custom Building Blocks gallery.
            if target.exists():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            copied += 1
    return copied


def register_cloud_fonts(prefix: Path, wine: Path, helper: Path | None = None,
                         use_x11: bool = True) -> None:
    candidates = [prefix / "register-office-cloud-fonts.sh"]
    if helper:
        candidates.append(helper)
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            subprocess.run([str(candidate)], env=wine_environment(prefix, wine, use_x11),
                           check=False, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=120)
            return


def _outlook_environment(env: dict[str, str]) -> dict[str, str]:
    result = env.copy()
    overrides = [item for item in result.get("WINEDLLOVERRIDES", "").split(";")
                 if item and not item.lower().startswith("mshtml=")]
    overrides.append("mshtml=")
    result["WINEDLLOVERRIDES"] = ";".join(overrides)
    return result


def prepare_outlook_first_run(prefix: Path, wine: Path, env: dict[str, str]) -> None:
    outlook_key = r"HKCU\Software\Microsoft\Office\16.0\Outlook"
    query = subprocess.run(
        [str(wine), "reg", "query", outlook_key, "/v", "LastUILanguage"],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        timeout=30, check=False,
    )
    if query.returncode == 0 and "LastUILanguage" in query.stdout:
        return

    locale_query = subprocess.run(
        [str(wine), "reg", "query", r"HKCU\Control Panel\International", "/v", "Locale"],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        timeout=30, check=False,
    )
    match = re.search(r"\b([0-9a-fA-F]{8})\b", locale_query.stdout)
    lcid = int(match.group(1), 16) if match else 0x0409
    subprocess.run(
        [str(wine), "reg", "add", outlook_key, "/v", "LastUILanguage",
         "/t", "REG_DWORD", "/d", str(lcid), "/f"],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        timeout=30, check=True,
    )


def _windows_document_path(document: str, wine: Path, env: dict[str, str]) -> str:
    winepath = sibling_tool(wine, "winepath")
    command = ([str(winepath), "-w", document] if winepath
               else [str(wine), "winepath.exe", "-w", document])
    result = subprocess.run(
        command, env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        text=True, timeout=30, check=True,
    )
    converted = result.stdout.rstrip("\r\n")
    if not converted:
        raise RuntimeError(f"Wine could not convert document path: {document}")
    return converted



def launch_app(prefix_value: str, wine_value: str, app: str, helper: Path | None = None,
               documents: Iterable[str] = (), use_x11: bool = True) -> int:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    executable = find_office_app(str(prefix), app)
    if not executable:
        raise FileNotFoundError(f"{APP_META[app]['exe']} is not installed in {prefix}")
    if app == "word":
        prepare_office_building_blocks(prefix)
    register_cloud_fonts(prefix, wine, helper, use_x11)
    env = wine_environment(prefix, wine, use_x11)
    arguments = [_windows_document_path(document, wine, env) for document in documents]
    if app == "outlook":
        prepare_outlook_first_run(prefix, wine, env)
        env = _outlook_environment(env)
    process = subprocess.Popen([str(wine), str(executable), *arguments], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               start_new_session=True)
    return process.pid


def launch_executable(prefix_value: str, wine_value: str, executable_value: str,
                      arguments: str | Iterable[str] = (),
                      working_directory: PathValue | None = None,
                      use_x11: bool = True) -> int:
    prefix = validate_prefix(prefix_value)
    if not (prefix / "system.reg").is_file():
        raise FileNotFoundError(f"Wine environment is not initialized: {prefix}")
    wine = require_wine(wine_value)
    executable = normalize_path(executable_value)
    if not executable.is_file():
        raise FileNotFoundError(f"Windows executable was not found: {executable}")
    if executable.suffix.lower() != ".exe":
        raise ValueError("Only .exe files can be launched from this control.")
    cwd = executable.parent
    if working_directory is not None:
        raw_directory = (working_directory.strip() if isinstance(working_directory, str)
                         else os.fspath(working_directory))
        if raw_directory:
            cwd = normalize_path(raw_directory)
            if not cwd.is_dir():
                raise NotADirectoryError(f"Working directory was not found: {cwd}")
    parsed_arguments = shlex.split(arguments) if isinstance(arguments, str) else list(arguments)
    process = subprocess.Popen([str(wine), str(executable), *parsed_arguments],
                               cwd=cwd, env=wine_environment(prefix, wine, use_x11),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               start_new_session=True)
    return process.pid


def host_terminal_command(command: list[str], env: dict[str, str]) -> list[str]:
    candidates: list[list[str]] = []
    configured = os.environ.get("TERMINAL", "").strip()
    if configured:
        candidates.append(shlex.split(configured))
    candidates.extend([[name] for name in (
        "konsole", "ptyxis", "kgx", "gnome-terminal", "xfce4-terminal",
        "kitty", "alacritty", "foot", "x-terminal-emulator", "xterm",
    )])

    seen: set[str] = set()
    for candidate in candidates:
        if not candidate or candidate[0] in seen:
            continue
        seen.add(candidate[0])
        executable = shutil.which(candidate[0], path=env.get("PATH"))
        if not executable:
            continue
        arguments = [executable, *candidate[1:]]
        name = Path(executable).name.lower()
        if name == "konsole":
            return [*arguments, "--hold", "-e", *command]
        if name in {"ptyxis", "kgx", "gnome-terminal"}:
            return [*arguments, "--", *command]
        if name == "xfce4-terminal":
            return [*arguments, "--hold", "--command", shlex.join(command)]
        return [*arguments, "-e", *command]
    raise FileNotFoundError(
        "No supported system terminal was found. Install Konsole, GNOME Terminal, or xterm."
    )


def launch_tool(prefix_value: str, wine_value: str, tool: str,
                use_x11: bool = True) -> int | None:
    if tool == "stop":
        stop_wine(prefix_value, wine_value, use_x11)
        return None
    if tool not in TOOL_META:
        raise ValueError(f"Unknown Wine tool: {tool}")
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    env = wine_environment(prefix, wine, use_x11)
    env.setdefault("WINEDEBUG", "-all")
    if tool == "cmd":
        terminal = host_terminal_command([str(wine), "cmd.exe"], env)
        process = subprocess.Popen(terminal, env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, start_new_session=True)
        return process.pid
    binary_name, arguments = TOOL_META[tool]
    binary = wine if binary_name == "wine" else sibling_tool(wine, binary_name)
    if binary is None:
        arguments = [f"{binary_name}.exe", *arguments]
        binary = wine
    process = subprocess.Popen([str(binary), *arguments], env=env, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, start_new_session=True)
    return process.pid


def desktop_exec_argument(value: str) -> str:
    if "\n" in value or "\r" in value:
        raise ValueError("Desktop shortcut arguments cannot contain line breaks.")
    if value in {"%f", "%F", "%u", "%U"}:
        return value
    escaped = value.replace("%", "%%").replace("\\", "\\\\")
    escaped = escaped.replace('"', '\\"').replace("`", "\\`").replace("$", "\\$")
    return f'"{escaped}"'


def desktop_directory() -> Path:
    override = os.environ.get("XDG_DESKTOP_DIR")
    if override:
        return normalize_path(override)
    user_dirs = config_home() / "user-dirs.dirs"
    if user_dirs.is_file():
        match = re.search(r'^XDG_DESKTOP_DIR="([^"]+)"', user_dirs.read_text(errors="replace"), re.MULTILINE)
        if match:
            value = match.group(1).replace("$HOME", str(Path.home()))
            return normalize_path(value)
    return Path.home() / "Desktop"


def _resource_data(pe, entry) -> bytes:
    language = entry.directory.entries[0]
    data = language.data.struct
    return pe.get_data(data.OffsetToData, data.Size)


def _build_ico(group_data: bytes, images: dict[int, bytes]) -> bytes:
    if len(group_data) < 6:
        raise ValueError("Icon group resource is truncated.")
    reserved, icon_type, count = struct.unpack_from("<HHH", group_data)
    if reserved != 0 or icon_type != 1 or count == 0:
        raise ValueError("Executable has an invalid icon group resource.")
    if len(group_data) < 6 + count * 14:
        raise ValueError("Icon group entries are truncated.")

    entries: list[bytes] = []
    payloads: list[bytes] = []
    offset = 6 + count * 16
    for index in range(count):
        width, height, colors, entry_reserved, planes, bit_count, _, resource_id = \
            struct.unpack_from("<BBBBHHIH", group_data, 6 + index * 14)
        image = images.get(resource_id)
        if not image:
            raise ValueError(f"Icon image resource {resource_id} is missing.")
        entries.append(struct.pack(
            "<BBBBHHII", width, height, colors, entry_reserved, planes, bit_count,
            len(image), offset,
        ))
        payloads.append(image)
        offset += len(image)
    return struct.pack("<HHH", 0, 1, count) + b"".join(entries) + b"".join(payloads)


def extract_office_icon(executable: Path, destination: Path) -> Path:
    executable = normalize_path(executable)
    destination = normalize_path(destination)
    if destination.is_file() and destination.stat().st_size > 6 \
            and destination.stat().st_mtime_ns >= executable.stat().st_mtime_ns:
        return destination
    try:
        import pefile
    except ImportError as error:
        raise RuntimeError(
            "Office icon extraction requires pefile. Install the Wine4Office GUI dependencies."
        ) from error

    pe = pefile.PE(str(executable), fast_load=True)
    try:
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]]
        )
        resources = getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None)
        if resources is None:
            raise ValueError("Executable has no resource directory.")
        resource_types = {entry.id: entry for entry in resources.entries if entry.id is not None}
        group_type = resource_types.get(14)
        image_type = resource_types.get(3)
        if group_type is None or image_type is None:
            raise ValueError("Executable has no Windows icon resources.")
        group_entry = group_type.directory.entries[0]
        group_data = _resource_data(pe, group_entry)
        images = {
            entry.id: _resource_data(pe, entry)
            for entry in image_type.directory.entries
            if entry.id is not None
        }
        icon_data = _build_ico(group_data, images)
    except (AttributeError, IndexError, KeyError, OSError, struct.error, ValueError) as error:
        raise RuntimeError(f"Could not extract the application icon from {executable}: {error}") from error
    finally:
        pe.close()

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".tmp")
    temporary.write_bytes(icon_data)
    os.replace(temporary, destination)
    return destination


def app_icon_path(app: str, executable: Path) -> Path:
    return extract_office_icon(executable, data_home() / "icons/wine4office" / f"{app}.ico")


def _owned_desktop_file(path: Path) -> bool:
    try:
        return "X-Wine4Office-Managed=true" in path.read_text(errors="replace")
    except OSError:
        return False


def write_desktop_file(path: Path, name: str, comment: str, command: list[str], icon: Path,
                       categories: str, mime: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "[Desktop Entry]",
        "Type=Application",
        f"Name={name}",
        f"Comment={comment}",
        "Exec=" + " ".join(desktop_exec_argument(part) for part in command),
        f"Icon={icon}",
        "Terminal=false",
        "StartupNotify=true",
        f"Categories={categories}",
    ]
    if mime:
        lines.append(f"MimeType={mime}")
    lines.append("X-Wine4Office-Managed=true")
    temporary = path.with_suffix(".tmp")
    temporary.write_text("\n".join(lines) + "\n")
    temporary.chmod(0o755)
    os.replace(temporary, path)


def installed_app_executable(prefix: Path, app: str) -> Path:
    executable = find_office_app(str(prefix), app)
    if executable is None:
        raise FileNotFoundError(f"{APP_META[app]['exe']} is not installed in {prefix}")
    return executable


def create_app_shortcuts(apps: Iterable[str], prefix_value: PathValue, wine_value: PathValue,
                         launcher: Path, copy_to_desktop: bool) -> list[str]:
    prefix = validate_prefix(prefix_value)
    wine = normalize_path(wine_value)
    if not launcher.is_file():
        raise FileNotFoundError(f"Wine4Office application launcher is missing: {launcher}")
    created: list[str] = []
    for app in apps:
        if app not in APP_META:
            raise ValueError(f"Unknown Office application: {app}")
        meta = APP_META[app]
        icon = app_icon_path(app, installed_app_executable(prefix, app))
        command = [str(launcher), "--prefix", str(prefix), "--wine", str(wine), app]
        if meta["mime"]:
            command.append("%F")
        filename = f"wine4office-{app}.desktop"
        menu_file = data_home() / "applications" / filename
        write_desktop_file(menu_file, meta["name"], f"Launch {meta['name']} in {prefix}", command,
                           icon, meta["categories"], meta["mime"])
        created.append(str(menu_file))
        desktop_file = desktop_directory() / filename
        if copy_to_desktop:
            write_desktop_file(desktop_file, meta["name"], f"Launch {meta['name']} in {prefix}", command,
                               icon, meta["categories"], meta["mime"])
            created.append(str(desktop_file))
        elif _owned_desktop_file(desktop_file):
            desktop_file.unlink()
    refresh_desktop_database()
    return created


def remove_app_shortcuts(apps: Iterable[str]) -> list[str]:
    removed: list[str] = []
    for app in apps:
        if app not in APP_META:
            raise ValueError(f"Unknown Office application: {app}")
        filename = f"wine4office-{app}.desktop"
        for path in (data_home() / "applications" / filename, desktop_directory() / filename):
            if path.is_file() and _owned_desktop_file(path):
                path.unlink()
                removed.append(str(path))
    refresh_desktop_database()
    return removed


def install_manager_shortcut(manager_launcher: Path, icons: Path) -> Path:
    source_icon = icons / "wine4office-manager.png"
    if not source_icon.is_file():
        raise FileNotFoundError(f"Wine4OfficeManager icon is missing: {source_icon}")
    installed_icon = data_home() / "icons/wine4office/wine4office-manager.png"
    installed_icon.parent.mkdir(parents=True, exist_ok=True)
    temporary_icon = installed_icon.with_name(
        f".{installed_icon.name}.{os.getpid()}.{time.time_ns()}.tmp"
    )
    try:
        shutil.copyfile(source_icon, temporary_icon)
        temporary_icon.chmod(0o644)
        os.replace(temporary_icon, installed_icon)
    finally:
        temporary_icon.unlink(missing_ok=True)
    path = data_home() / "applications/wine4office-manager.desktop"
    write_desktop_file(path, "Wine4OfficeManager", "Manage Wine4Office environments and shortcuts",
                       [str(manager_launcher)], installed_icon, "Utility;Settings;")
    refresh_desktop_database()
    return path


def refresh_desktop_database() -> None:
    utility = shutil.which("update-desktop-database")
    if utility:
        subprocess.run([utility, str(data_home() / "applications")], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _https_url(value: str, description: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{description} must be an HTTPS URL.")
    value = value.strip()
    parsed = urllib.parse.urlparse(value)
    if (parsed.scheme != "https" or not parsed.netloc or parsed.username is not None
            or parsed.password is not None or parsed.fragment):
        raise ValueError(f"{description} must be an HTTPS URL.")
    return value


def resolve_update_url(metadata_url: str, value: object, description: str) -> str:
    base = _https_url(metadata_url, "Metadata address")
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{description} is missing.")
    return _https_url(urllib.parse.urljoin(base, value.strip()), description)


def _parse_component(payload: object, name: str, metadata_url: str) -> dict:
    if not isinstance(payload, dict):
        raise ValueError(f"Release metadata {name} entry must be an object.")
    version = payload.get("version")
    if not isinstance(version, str) or not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"Release metadata has an invalid {name} version.")
    digest = payload.get("sha256")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
        raise ValueError(f"Release metadata has an invalid {name} SHA-256 digest.")
    size = payload.get("size")
    maximum = MAX_MANAGER_SIZE if name == "manager" else MAX_WINE_SIZE
    if type(size) is not int or size <= 0 or size > maximum:
        raise ValueError(f"Release metadata has an invalid {name} size.")
    if name == "wine" and payload.get("format") != "tar.zst":
        raise ValueError("Release metadata Wine format must be 'tar.zst'.")
    result = {
        "version": version,
        "url": resolve_update_url(metadata_url, payload.get("url"), f"{name.title()} artifact address"),
        "sha256": digest.lower(),
        "size": size,
    }
    if name == "wine":
        result["format"] = "tar.zst"
    return result


def _expected_update_channel(expected_channel: str | None) -> str:
    expected_channel = configured_update_channel() \
        if expected_channel is None else expected_channel
    if (not isinstance(expected_channel, str)
            or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", expected_channel)):
        raise ValueError("Expected update channel is invalid.")
    return expected_channel


def _validate_update_channel(metadata: dict, expected_channel: str | None) -> str:
    expected_channel = _expected_update_channel(expected_channel)
    channel = metadata.get("channel")
    if not isinstance(channel, str) or not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", channel):
        raise ValueError("Release metadata has an invalid channel.")
    if channel != expected_channel:
        raise ValueError(
            f"Release metadata channel {channel!r} does not match expected "
            f"channel {expected_channel!r}."
        )
    return channel


def parse_release_metadata(payload: bytes | str | dict, source_url: str,
                           expected_channel: str | None = None) -> dict:
    """Validate and normalize provider-neutral schema-v1 release metadata."""
    source_url = _https_url(source_url, "Metadata address")
    if isinstance(payload, (bytes, str)):
        try:
            payload = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError("Release metadata is not valid JSON.") from error
    if not isinstance(payload, dict):
        raise ValueError("Release metadata must be a JSON object.")
    if type(payload.get("schema_version")) is not int or payload["schema_version"] != 1:
        raise ValueError("Release metadata schema_version must be 1.")
    channel = _validate_update_channel(payload, expected_channel)
    canonical_url = resolve_update_url(source_url, payload.get("metadata_url"), "Metadata address")
    return {
        "schema_version": 1,
        "channel": channel,
        "metadata_url": canonical_url,
        "manager": _parse_component(payload.get("manager"), "manager", source_url),
        "wine": _parse_component(payload.get("wine"), "wine", source_url),
    }


def _https_context() -> ssl.SSLContext:
    """Use a bundled public CA store when running as a portable executable."""
    if getattr(sys, "frozen", False):
        try:
            import certifi
        except ImportError as error:
            raise RuntimeError(
                "The standalone manager is missing its certificate authority bundle."
            ) from error
        return ssl.create_default_context(cafile=certifi.where())
    return ssl.create_default_context()


def fetch_release_metadata(metadata_url: str, output: Output | None = None,
                           expected_channel: str | None = None) -> dict:
    metadata_url = _https_url(metadata_url, "Metadata address")
    if output:
        output(f"Checking {metadata_url}")
    request = urllib.request.Request(
        metadata_url, headers={"User-Agent": "Wine4OfficeManager/1"}
    )
    with urllib.request.urlopen(
            request, timeout=30, context=_https_context()
    ) as response:
        final_url = response.geturl() if hasattr(response, "geturl") else metadata_url
        _https_url(final_url, "Final metadata address")
        payload = response.read(MAX_METADATA_SIZE + 1)
    if len(payload) > MAX_METADATA_SIZE:
        raise ValueError("Release metadata is larger than 1 MiB.")
    return parse_release_metadata(payload, metadata_url, expected_channel)


def _split_version(value: str) -> tuple[list[tuple[int, object]], list[str] | None]:
    if not isinstance(value, str) or not VERSION_PATTERN.fullmatch(value):
        raise ValueError(f"Invalid version: {value!r}")
    precedence = value.split("+", 1)[0]
    release, separator, prerelease = precedence.partition("-")
    release_parts: list[tuple[int, object]] = []
    for token in re.findall(r"[0-9]+|[A-Za-z]+", release):
        release_parts.append((1, int(token)) if token.isdigit() else (0, token.lower()))
    return release_parts, prerelease.split(".") if separator else None


def compare_versions(left: str, right: str) -> int:
    """Compare release versions without lexicographic numeric mistakes."""
    left_release, left_pre = _split_version(left)
    right_release, right_pre = _split_version(right)
    width = max(len(left_release), len(right_release))
    for index in range(width):
        left_part = left_release[index] if index < len(left_release) else (1, 0)
        right_part = right_release[index] if index < len(right_release) else (1, 0)
        if left_part != right_part:
            return 1 if left_part > right_part else -1
    if left_pre is None or right_pre is None:
        if left_pre is right_pre:
            return 0
        return 1 if left_pre is None else -1
    for index in range(max(len(left_pre), len(right_pre))):
        if index >= len(left_pre):
            return -1
        if index >= len(right_pre):
            return 1
        left_part, right_part = left_pre[index], right_pre[index]
        if left_part == right_part:
            continue
        left_numeric, right_numeric = left_part.isdigit(), right_part.isdigit()
        if left_numeric and right_numeric:
            return 1 if int(left_part) > int(right_part) else -1
        if left_numeric != right_numeric:
            return -1 if left_numeric else 1
        return 1 if left_part.lower() > right_part.lower() else -1
    return 0


def available_updates(metadata: dict, skipped: dict | None = None,
                      expected_channel: str | None = None) -> dict:
    _validate_update_channel(metadata, expected_channel)
    skipped = skipped if isinstance(skipped, dict) else {}
    installed = {"manager": current_version(), "wine": current_wine_version()}
    updates: dict[str, dict] = {}
    for name in ("manager", "wine"):
        candidate = metadata[name]
        current = installed[name]
        if str(skipped.get(name, "")) == candidate["version"]:
            continue
        if current == "development":
            if name == "wine" and not runner_update_target().exists():
                updates[name] = dict(candidate)
            continue
        if compare_versions(candidate["version"], current) <= 0:
            continue
        updates[name] = dict(candidate)
    return updates


def check_for_updates(metadata_url: str, skipped: dict | None = None,
                      output: Output | None = None,
                      expected_channel: str | None = None) -> dict:
    metadata = fetch_release_metadata(metadata_url, output, expected_channel)
    return {
        "metadata": metadata,
        "updates": available_updates(metadata, skipped, expected_channel),
    }


def _atomic_write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", dir=path.parent, prefix=f".{path.name}.",
                                     delete=False) as temporary:
        temporary.write(value)
        temporary.flush()
        os.fsync(temporary.fileno())
        temporary_path = Path(temporary.name)
    try:
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def _atomic_write_bytes(path: Path, value: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("wb", dir=path.parent, prefix=f".{path.name}.",
                                     delete=False) as temporary:
        temporary.write(value)
        temporary.flush()
        os.fsync(temporary.fileno())
        temporary_path = Path(temporary.name)
    temporary_path.chmod(mode)
    try:
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)

def _validate_office_language_list(values: Iterable[str]) -> list[str]:
    languages: list[str] = []
    seen: set[str] = set()
    for value in values:
        if not isinstance(value, str):
            raise ValueError("Office language identifiers must be text.")
        language = value.strip().lower()
        if not language or language not in _OFFICE_LANGUAGE_IDS:
            raise ValueError(f"Unsupported Office language identifier: {value!r}")
        if language in seen:
            raise ValueError(f"Duplicate Office language identifier: {value!r}")
        seen.add(language)
        languages.append(language)
    if not languages:
        raise ValueError("Enter at least one Office language identifier.")
    return languages


def validate_office_languages(value) -> list[str]:
    """Parse Office language identifiers separated by commas, semicolons, or whitespace."""
    if not isinstance(value, str):
        raise ValueError("Office languages must be a separated text value.")
    stripped = value.strip()
    if not stripped:
        raise ValueError("Enter at least one Office language identifier.")
    if len(value) > 512:
        raise ValueError("The Office language list is too long.")
    return _validate_office_language_list(re.split(r"[,;\s]+", stripped))


def build_office_configuration(product_id, languages) -> str:
    """Build a deterministic 64-bit Office Deployment Tool configuration."""
    if not isinstance(product_id, str) or product_id not in _OFFICE_PRODUCT_BY_ID:
        raise ValueError(f"Unsupported Office product: {product_id!r}")
    if isinstance(languages, str):
        validated_languages = validate_office_languages(languages)
    else:
        try:
            validated_languages = _validate_office_language_list(languages)
        except TypeError as error:
            raise ValueError("Office languages must be an iterable of identifiers.") from error
    product = _OFFICE_PRODUCT_BY_ID[product_id]
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        "<Configuration>",
        f'  <Add OfficeClientEdition="64" Channel="{product["channel"]}">',
        f'    <Product ID="{product["product_id"]}">',
    ]
    lines.extend(f'      <Language ID="{language}" />' for language in validated_languages)
    lines.extend([
        "    </Product>",
        "  </Add>",
        '  <Display Level="Full" />',
        "</Configuration>",
        "",
    ])
    return "\n".join(lines)


def _xml_local_name(name: str) -> str:
    return name.rsplit("}", 1)[-1]


def _validate_office_configuration_payload(payload: bytes) -> None:
    if not isinstance(payload, bytes):
        raise ValueError("Office configuration payload must be immutable bytes.")
    if not payload:
        raise ValueError("Office configuration XML is empty.")
    if len(payload) > MAX_OFFICE_XML_SIZE:
        raise ValueError("Office configuration XML is larger than 1 MiB.")
    try:
        text = payload.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("Office configuration XML must be UTF-8.") from error
    upper_text = text.upper()
    if "<!DOCTYPE" in upper_text or "<!ENTITY" in upper_text:
        raise ValueError("Office configuration XML cannot contain a document type or entities.")
    try:
        root = ET.fromstring(text)
    except ET.ParseError as error:
        raise ValueError("Office configuration XML is malformed.") from error
    if _xml_local_name(root.tag) != "Configuration":
        raise ValueError("Office configuration XML root must be Configuration.")
    stack = [(root, 1)]
    element_count = 0
    while stack:
        element, depth = stack.pop()
        element_count += 1
        if element_count > 10_000:
            raise ValueError("Office configuration XML contains too many elements.")
        if depth > 64:
            raise ValueError("Office configuration XML is nested too deeply.")
        local_name = _xml_local_name(element.tag)
        folded_local_name = local_name.casefold()
        attributes = {
            _xml_local_name(name).casefold(): value.strip()
            for name, value in element.attrib.items()
        }
        if folded_local_name in {"remove", "removemsi"}:
            raise ValueError(
                f"Office configuration XML cannot contain destructive {local_name} directives."
            )
        if (folded_local_name == "add"
                and attributes.get("migratearch", "").casefold() == "true"):
            raise ValueError(
                "Office configuration XML cannot enable destructive architecture migration."
            )
        if (folded_local_name == "property"
                and attributes.get("name", "").casefold() == "forceappshutdown"
                and attributes.get("value", "").casefold() == "true"):
            raise ValueError(
                "Office configuration XML cannot force running Office applications to close."
            )
        stack.extend((child, depth + 1) for child in element)


def load_office_configuration(path) -> tuple[Path, bytes, str]:
    """Read and validate an Office Configuration once, returning immutable bytes."""
    configuration = normalize_path(path)
    if not configuration.is_file():
        raise FileNotFoundError(f"Office configuration XML was not found: {configuration}")
    size = configuration.stat().st_size
    if size <= 0:
        raise ValueError("Office configuration XML is empty.")
    if size > MAX_OFFICE_XML_SIZE:
        raise ValueError("Office configuration XML is larger than 1 MiB.")
    with configuration.open("rb") as source:
        payload = source.read(MAX_OFFICE_XML_SIZE + 1)
    _validate_office_configuration_payload(payload)
    return configuration, payload, hashlib.sha256(payload).hexdigest()


def validate_office_configuration(path) -> Path:
    """Validate a bounded, non-destructive ODT Configuration without changing it."""
    configuration, _, _ = load_office_configuration(path)
    return configuration


def office_configuration_digest(path) -> str:
    """Return the SHA-256 digest of a validated Office Configuration document."""
    _, _, digest = load_office_configuration(path)
    return digest


def _restore_office_configuration_tombstone(tombstone: Path,
                                             configuration: Path) -> Path | None:
    try:
        os.link(tombstone, configuration, follow_symlinks=False)
    except OSError:
        if os.path.lexists(tombstone):
            return tombstone
        return configuration if os.path.lexists(configuration) else None
    try:
        os.unlink(tombstone)
    except OSError:
        return tombstone
    return configuration


def delete_office_configuration_if_unchanged(path, digest) -> tuple[bool, Path | None]:
    """Atomically isolate and delete only the exact configuration the user approved."""
    try:
        raw_path = path.strip() if isinstance(path, str) else os.fspath(path)
        if not raw_path:
            return False, None
        expanded = os.path.expandvars(raw_path)
        configuration = Path(os.path.abspath(Path(expanded).expanduser()))
    except (TypeError, ValueError, OSError):
        return False, None
    try:
        initial_stat = os.lstat(configuration)
    except FileNotFoundError:
        return False, None
    except OSError:
        return False, configuration
    if not stat.S_ISREG(initial_stat.st_mode):
        return False, configuration
    if (not isinstance(digest, str)
            or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None):
        return False, configuration
    tombstone = configuration.with_name(
        f".{configuration.name}.wine4office-preserved-{uuid.uuid4().hex}"
    )
    try:
        os.rename(configuration, tombstone)
    except OSError:
        return False, configuration if os.path.lexists(configuration) else None

    file_descriptor: int | None = None
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        file_descriptor = os.open(tombstone, flags)
        moved_stat = os.fstat(file_descriptor)
        if (not stat.S_ISREG(moved_stat.st_mode)
                or moved_stat.st_size <= 0
                or moved_stat.st_size > MAX_OFFICE_XML_SIZE):
            preserved = _restore_office_configuration_tombstone(
                tombstone, configuration
            )
            return False, preserved
        calculated = hashlib.sha256()
        read_size = 0
        while True:
            chunk = os.read(file_descriptor, min(64 * 1024, MAX_OFFICE_XML_SIZE - read_size + 1))
            if not chunk:
                break
            read_size += len(chunk)
            if read_size > MAX_OFFICE_XML_SIZE:
                preserved = _restore_office_configuration_tombstone(
                    tombstone, configuration
                )
                return False, preserved
            calculated.update(chunk)
        current_stat = os.lstat(tombstone)
        if ((current_stat.st_dev, current_stat.st_ino)
                != (moved_stat.st_dev, moved_stat.st_ino)):
            return False, tombstone if os.path.lexists(tombstone) else None
        if calculated.hexdigest().lower() != digest.lower():
            preserved = _restore_office_configuration_tombstone(
                tombstone, configuration
            )
            return False, preserved
        try:
            os.unlink(tombstone)
        except OSError:
            return False, tombstone
        return True, None
    except OSError:
        preserved = _restore_office_configuration_tombstone(tombstone, configuration)
        return False, preserved
    finally:
        if file_descriptor is not None:
            try:
                os.close(file_descriptor)
            except OSError:
                pass


def _trusted_microsoft_url(value: str, hosts: frozenset[str], description: str) -> str:
    value = _https_url(value, description)
    parsed = urllib.parse.urlparse(value)
    try:
        port = parsed.port
    except ValueError as error:
        raise ValueError(f"{description} has an invalid port.") from error
    if parsed.hostname is None or parsed.hostname.lower() not in hosts or port not in (None, 443):
        raise ValueError(f"{description} is not on a trusted Microsoft host.")
    return value


class _MicrosoftRedirectHandler(urllib.request.HTTPRedirectHandler):
    def __init__(self, hosts: frozenset[str]):
        super().__init__()
        self._hosts = hosts

    def redirect_request(self, request, file_pointer, code, message, headers, new_url):
        _trusted_microsoft_url(new_url, self._hosts, "Microsoft redirect address")
        return super().redirect_request(
            request, file_pointer, code, message, headers, new_url
        )


def _microsoft_opener(hosts: frozenset[str]):
    return urllib.request.build_opener(
        _MicrosoftRedirectHandler(hosts),
        urllib.request.HTTPSHandler(context=_https_context()),
    )


class _OdtLinkParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        if tag.lower() != "a":
            return
        for name, value in attrs:
            if name.lower() == "href" and isinstance(value, str):
                self.links.append(value)


def _read_bounded_response(response, maximum: int, description: str,
                           cancel_event=None) -> bytes:
    headers = getattr(response, "headers", None)
    declared_size = headers.get("Content-Length") if headers is not None else None
    if declared_size is not None:
        try:
            declared_size = int(declared_size)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{description} has an invalid Content-Length.") from error
        if declared_size < 0 or declared_size > maximum:
            raise ValueError(f"{description} is larger than the allowed limit.")
    payload = bytearray()
    while True:
        if cancel_event is not None and cancel_event.is_set():
            raise RuntimeError("Operation cancelled.")
        chunk = response.read(min(1024 * 1024, maximum - len(payload) + 1))
        if not chunk:
            break
        payload.extend(chunk)
        if len(payload) > maximum:
            raise ValueError(f"{description} is larger than the allowed limit.")
    if declared_size is not None and len(payload) != declared_size:
        raise ValueError(f"{description} size does not match Content-Length.")
    return bytes(payload)

def _copy_bounded_response(response, target, maximum: int, description: str,
                           cancel_event=None) -> tuple[int, bytes]:
    headers = getattr(response, "headers", None)
    declared_size = headers.get("Content-Length") if headers is not None else None
    if declared_size is not None:
        try:
            declared_size = int(declared_size)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{description} has an invalid Content-Length.") from error
        if declared_size < 0 or declared_size > maximum:
            raise ValueError(f"{description} is larger than the allowed limit.")
    downloaded = 0
    signature = bytearray()
    while True:
        if cancel_event is not None and cancel_event.is_set():
            raise RuntimeError("Operation cancelled.")
        chunk = response.read(min(1024 * 1024, maximum - downloaded + 1))
        if not chunk:
            break
        downloaded += len(chunk)
        if downloaded > maximum:
            raise ValueError(f"{description} is larger than the allowed limit.")
        if len(signature) < 2:
            signature.extend(chunk[:2 - len(signature)])
        target.write(chunk)
    if declared_size is not None and downloaded != declared_size:
        raise ValueError(f"{description} size does not match Content-Length.")
    return downloaded, bytes(signature)


def _resolve_latest_odt_url(cancel_event=None) -> str:
    page_url = _trusted_microsoft_url(
        _ODT_DOWNLOAD_PAGE, _ODT_PAGE_HOSTS, "Office Deployment Tool page"
    )
    request = urllib.request.Request(
        page_url, headers={"User-Agent": "Wine4OfficeManager/1"}
    )
    with _microsoft_opener(_ODT_PAGE_HOSTS).open(request, timeout=30) as response:
        final_page_url = response.geturl() if hasattr(response, "geturl") else page_url
        final_page_url = _trusted_microsoft_url(
            final_page_url, _ODT_PAGE_HOSTS, "Final Office Deployment Tool page"
        )
        payload = _read_bounded_response(
            response, MAX_ODT_PAGE_SIZE, "Office Deployment Tool page", cancel_event
        )
    try:
        page = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("Office Deployment Tool page is not valid UTF-8.") from error
    parser = _OdtLinkParser()
    try:
        parser.feed(page)
        parser.close()
    except Exception as error:
        raise ValueError("Office Deployment Tool page contains malformed HTML.") from error
    candidates: set[str] = set()
    for link in parser.links:
        candidate = urllib.parse.urljoin(final_page_url, link)
        try:
            candidate = _trusted_microsoft_url(
                candidate, _ODT_DOWNLOAD_HOSTS, "Office Deployment Tool download"
            )
        except ValueError:
            continue
        if _ODT_LINK_PATTERN.search(urllib.parse.urlparse(candidate).path):
            candidates.add(candidate)
    if len(candidates) != 1:
        raise ValueError(
            "Microsoft's Office Deployment Tool page did not contain one trusted download."
        )
    return candidates.pop()




def _download_odt(url: str, output: Output, cancel_event=None) -> Path:
    url = _trusted_microsoft_url(url, _ODT_DOWNLOAD_HOSTS, "Office Deployment Tool download")
    download_dir = cache_home() / "wine4office/odt"
    download_dir.mkdir(parents=True, exist_ok=True)
    destination = download_dir / f"officedeploymenttool-{uuid.uuid4().hex}.exe"
    request = urllib.request.Request(url, headers={"User-Agent": "Wine4OfficeManager/1"})
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
                "wb", dir=download_dir, prefix=".officedeploymenttool-",
                suffix=".part", delete=False) as target:
            temporary_path = Path(target.name)
            output(f"Downloading Office Deployment Tool from {url}")
            with _microsoft_opener(_ODT_DOWNLOAD_HOSTS).open(
                    request, timeout=60) as response:
                final_url = response.geturl() if hasattr(response, "geturl") else url
                _trusted_microsoft_url(
                    final_url, _ODT_DOWNLOAD_HOSTS, "Final Office Deployment Tool download"
                )
                downloaded, signature = _copy_bounded_response(
                    response, target, MAX_ODT_DOWNLOAD_SIZE,
                    "Office Deployment Tool download", cancel_event,
                )
            if downloaded < 1024 or signature != b"MZ":
                raise ValueError("Office Deployment Tool download is not a valid Windows executable.")
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary_path, destination)
        temporary_path = None
        return destination
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def _require_extracted_setup(path: Path) -> Path:
    candidates = (path / "setup.exe", path / "Setup.exe")
    for candidate in candidates:
        try:
            size = candidate.stat().st_size
            if candidate.is_file() and 1024 <= size <= MAX_ODT_SETUP_SIZE:
                with candidate.open("rb") as source:
                    if source.read(2) == b"MZ":
                        return candidate
        except OSError:
            continue
    raise FileNotFoundError("Office Deployment Tool did not extract a valid setup.exe.")


def install_office_with_odt(prefix, wine, config_path, output,
                            cancel_event=None, process_callback=None, *,
                            configuration_payload=None) -> str:
    """Snapshot configuration, fetch current ODT, then run setup /configure."""
    prefix_path = validate_prefix(prefix)
    if not (prefix_path / "system.reg").is_file():
        raise FileNotFoundError(f"Wine environment is not initialized: {prefix_path}")
    wine_path = require_wine(str(wine))
    if configuration_payload is None:
        _, configuration_payload, _ = load_office_configuration(config_path)
    else:
        _validate_office_configuration_payload(configuration_payload)
    environment = wine_environment(prefix_path, wine_path)
    odt: Path | None = None
    try:
        with tempfile.TemporaryDirectory(prefix="wine4office-odt-") as temporary:
            work_directory = Path(temporary)
            configuration_snapshot = work_directory / "configuration.xml"
            _atomic_write_bytes(configuration_snapshot, configuration_payload, mode=0o400)
            extraction_directory = work_directory / "extract"
            extraction_directory.mkdir(mode=0o700)
            if cancel_event is not None and cancel_event.is_set():
                raise RuntimeError("Operation cancelled.")
            output("Resolving the latest Office Deployment Tool from Microsoft.")
            odt_url = _resolve_latest_odt_url(cancel_event)
            odt = _download_odt(odt_url, output, cancel_event)
            windows_extraction_directory = _windows_document_path(
                str(extraction_directory), wine_path, environment
            )
            if cancel_event is not None and cancel_event.is_set():
                raise RuntimeError("Operation cancelled.")
            output("Extracting the Office Deployment Tool.")
            _stream_command(
                [str(wine_path), str(odt), "/quiet",
                 f"/extract:{windows_extraction_directory}"],
                environment, output, cwd=extraction_directory,
                cancel_event=cancel_event, process_callback=process_callback,
            )
            setup = _require_extracted_setup(extraction_directory)
            windows_configuration = _windows_document_path(
                str(configuration_snapshot), wine_path, environment
            )
            if cancel_event is not None and cancel_event.is_set():
                raise RuntimeError("Operation cancelled.")
            output("Installing Office with the selected configuration.")
            _stream_command(
                [str(wine_path), str(setup), "/configure", windows_configuration],
                environment, output, cwd=extraction_directory,
                cancel_event=cancel_event, process_callback=process_callback,
            )
    finally:
        if odt is not None:
            odt.unlink(missing_ok=True)
    return "Office installation completed successfully."


def persist_metadata_url(metadata_url: str) -> str:
    """Persist only a normalized URL that has already passed metadata validation."""
    metadata_url = _https_url(metadata_url, "Metadata address")
    root = installed_root()
    if root is not None:
        root.mkdir(parents=True, exist_ok=True)
        _atomic_write_text(root / "UPDATE_URL", metadata_url + "\n")
    return metadata_url


def _download_artifact(name: str, component: dict, cancel_event=None,
                       output: Output | None = None) -> Path:
    download_dir = cache_home() / "wine4office/updates"
    download_dir.mkdir(parents=True, exist_ok=True)
    destination = download_dir / f"{name}-{component['version']}.{uuid.uuid4().hex}.part"
    request = urllib.request.Request(
        component["url"], headers={"User-Agent": "Wine4OfficeManager/1"}
    )
    digest = hashlib.sha256()
    downloaded = 0
    if output:
        output(f"Downloading {name} {component['version']}")
    try:
        with urllib.request.urlopen(
                request, timeout=60, context=_https_context()
        ) as response, destination.open("wb") as target:
            final_url = response.geturl() if hasattr(response, "geturl") else component["url"]
            _https_url(final_url, f"Final {name} artifact address")
            headers = getattr(response, "headers", None)
            content_length = headers.get("Content-Length") if headers is not None else None
            if content_length is not None and int(content_length) != component["size"]:
                raise ValueError(f"{name.title()} download size does not match release metadata.")
            while True:
                if cancel_event is not None and cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")
                chunk = response.read(min(1024 * 1024, component["size"] - downloaded + 1))
                if not chunk:
                    break
                downloaded += len(chunk)
                if downloaded > component["size"]:
                    raise ValueError(f"{name.title()} download is larger than release metadata.")
                digest.update(chunk)
                target.write(chunk)
            target.flush()
            os.fsync(target.fileno())
        if downloaded != component["size"]:
            raise ValueError(f"{name.title()} download size does not match release metadata.")
        if digest.hexdigest() != component["sha256"]:
            raise ValueError(f"{name.title()} download failed SHA-256 verification.")
        if output:
            output(f"Verified {downloaded} bytes for {name} {component['version']}")
        return destination
    except Exception:
        destination.unlink(missing_ok=True)
        raise


def _safe_archive_path(path: PurePosixPath, base: tuple[str, ...] = ()) -> tuple[str, ...]:
    if path.is_absolute():
        raise ValueError("Wine archive contains an absolute path.")
    parts = list(base)
    for part in path.parts:
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                raise ValueError("Wine archive path escapes the runner.")
            parts.pop()
        else:
            parts.append(part)
    if not parts:
        raise ValueError("Wine archive contains an empty path.")
    return tuple(parts)


def _validate_tar_member(member: tarfile.TarInfo) -> tuple[str, ...]:
    path = _safe_archive_path(PurePosixPath(member.name))
    if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
        raise ValueError(f"Wine archive contains an unsafe special file: {member.name}")
    if member.size < 0:
        raise ValueError(f"Wine archive contains a negative file size: {member.name}")
    if member.isfile() and member.size > MAX_WINE_FILE_SIZE:
        raise ValueError(f"Wine archive file exceeds the declared-size limit: {member.name}")
    if member.issym():
        _safe_archive_path(PurePosixPath(member.linkname), path[:-1])
    elif member.islnk():
        _safe_archive_path(PurePosixPath(member.linkname))
    return path


def _validated_tar_members(bundle):
    count = 0
    declared_size = 0
    paths: set[tuple[str, ...]] = set()
    for member in bundle:
        count += 1
        if count > MAX_WINE_ARCHIVE_MEMBERS:
            raise ValueError("Wine archive exceeds the member-count limit.")
        path = _validate_tar_member(member)
        if path in paths:
            raise ValueError(f"Wine archive contains a duplicate path: {member.name}")
        paths.add(path)
        if member.isfile():
            declared_size += member.size
            if declared_size > MAX_WINE_EXTRACTED_SIZE:
                raise ValueError("Wine archive exceeds the total declared-size limit.")
        yield member, path


def _archive_target(destination: Path, path: tuple[str, ...]) -> Path:
    target = destination.joinpath(*path)
    destination_resolved = destination.resolve()
    try:
        target.parent.resolve().relative_to(destination_resolved)
    except ValueError as error:
        raise ValueError("Wine archive extraction path escapes the runner.") from error
    return target


def _extract_validated_tar(bundle, destination: Path) -> None:
    extracted_size = 0
    directory_modes: list[tuple[Path, int]] = []
    pending_links: list[tuple[Path, tuple[str, ...]]] = []
    for member, path in _validated_tar_members(bundle):
        target = _archive_target(destination, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        if member.isdir():
            target.mkdir(exist_ok=True)
            directory_modes.append((target, member.mode & 0o777))
            continue
        if member.issym():
            os.symlink(member.linkname, target)
            continue
        if member.islnk():
            pending_links.append((
                target, _safe_archive_path(PurePosixPath(member.linkname))
            ))
            continue
        source = bundle.extractfile(member)
        if source is None:
            raise ValueError(f"Wine archive file has no data: {member.name}")
        written = 0
        try:
            with target.open("xb") as extracted:
                while True:
                    chunk = source.read(1024 * 1024)
                    if not chunk:
                        break
                    written += len(chunk)
                    extracted_size += len(chunk)
                    if written > member.size or written > MAX_WINE_FILE_SIZE:
                        raise ValueError(
                            f"Wine archive file exceeds its declared size: {member.name}"
                        )
                    if extracted_size > MAX_WINE_EXTRACTED_SIZE:
                        raise ValueError(
                            "Wine archive exceeds the total extracted-size limit."
                        )
                    extracted.write(chunk)
        finally:
            source.close()
        if written != member.size:
            raise ValueError(f"Wine archive file size changed during extraction: {member.name}")
        target.chmod(member.mode & 0o777)
    for target, link_path in pending_links:
        source = _archive_target(destination, link_path)
        if not source.is_file():
            raise ValueError(f"Wine archive hard link target is missing: {source}")
        os.link(source, target)
    for directory, mode in reversed(directory_modes):
        directory.chmod(mode)


def _open_zstd_tar(archive: Path):
    try:
        import zstandard
    except ImportError as error:
        raise RuntimeError(
            "The zstandard Python package is required to install Wine runner updates."
        ) from error
    source = archive.open("rb")
    reader = zstandard.ZstdDecompressor().stream_reader(source)
    return source, reader, tarfile.open(fileobj=reader, mode="r|")


def safe_extract_wine_archive(archive: Path, destination: Path) -> Path:
    """Validate all limits before a second bounded streaming extraction pass."""
    source, reader, bundle = _open_zstd_tar(archive)
    try:
        for _member, _path in _validated_tar_members(bundle):
            pass
    except ValueError:
        raise
    except Exception as error:
        raise ValueError("Wine artifact is not a valid tar.zst archive.") from error
    finally:
        bundle.close()
        reader.close()
        source.close()

    destination.mkdir(parents=True, exist_ok=False)
    try:
        source, reader, bundle = _open_zstd_tar(archive)
        try:
            _extract_validated_tar(bundle, destination)
        except ValueError:
            raise
        except Exception as error:
            raise ValueError("Wine artifact could not be safely extracted.") from error
        finally:
            bundle.close()
            reader.close()
            source.close()
        direct = destination / "bin/wine"
        if direct.is_file():
            runner = destination
        else:
            candidates = [
                child for child in destination.iterdir()
                if child.is_dir() and (child / "bin/wine").is_file()
            ]
            if len(candidates) != 1 or any(
                    child != candidates[0] for child in destination.iterdir()):
                raise ValueError(
                    "Wine archive must contain exactly one runner tree with bin/wine."
                )
            runner = candidates[0]
        if not os.access(runner / "bin/wine", os.X_OK):
            raise ValueError("Wine archive bin/wine is not executable.")
        return runner
    except Exception:
        shutil.rmtree(destination, ignore_errors=True)
        raise


def manager_update_target() -> Path | None:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve()
    root = installed_root()
    if root is not None:
        return root / "lib/wine4office-manager-qt"
    return None


def runner_update_target() -> Path:
    root = installed_root()
    return root / "runner" if root is not None else data_home() / "wine4office/runner"


def _remove_update_path(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink(missing_ok=True)


def _stage_update_file(source: Path, target: Path) -> Path:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
            "wb", dir=target.parent, prefix=f".{target.name}.new.", delete=False
    ) as temporary, source.open("rb") as payload:
        shutil.copyfileobj(payload, temporary, 1024 * 1024)
        temporary.flush()
        os.fsync(temporary.fileno())
        staged = Path(temporary.name)
    staged.chmod(0o755)
    return staged


@contextmanager
def _install_update_lock(root: Path | None, manager_target: Path | None,
                         runner_target: Path):
    lock_roots = [root] if root is not None else [runner_target.parent]
    if root is None and manager_target is not None:
        lock_roots.append(manager_target.parent)
    unique_roots = sorted(
        {path.resolve(strict=False) for path in lock_roots},
        key=str,
    )
    locks = []
    try:
        for lock_root in unique_roots:
            lock_root.mkdir(parents=True, exist_ok=True)
            lock = (lock_root / ".wine4office-update.lock").open("a+b")
            try:
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            except BaseException:
                lock.close()
                raise
            locks.append(lock)
        yield
    finally:
        for lock in reversed(locks):
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
            lock.close()


def _commit_update_transaction(
        replacements: list[tuple[Path, Path]],
        text_updates: dict[Path, str],
) -> None:
    snapshots: dict[Path, tuple[bytes, int] | None] = {}
    for path in text_updates:
        if path.exists() or path.is_symlink():
            snapshots[path] = (path.read_bytes(), path.stat().st_mode & 0o777)
        else:
            snapshots[path] = None

    states: list[dict] = []
    try:
        for staged, target in replacements:
            target.parent.mkdir(parents=True, exist_ok=True)
            state = {
                "target": target,
                "backup": target.parent / f".{target.name}.old.{uuid.uuid4().hex}",
                "had_target": target.exists() or target.is_symlink(),
            }
            states.append(state)
            if state["had_target"]:
                os.replace(target, state["backup"])
            os.replace(staged, target)
        for path, value in text_updates.items():
            _atomic_write_text(path, value)
    except BaseException as error:
        rollback_error: BaseException | None = None
        for path, snapshot in reversed(list(snapshots.items())):
            try:
                if snapshot is None:
                    _remove_update_path(path)
                else:
                    _atomic_write_bytes(path, snapshot[0], snapshot[1])
            except BaseException as restore_error:
                rollback_error = rollback_error or restore_error
        for state in reversed(states):
            try:
                backup_exists = (
                    state["backup"].exists() or state["backup"].is_symlink()
                )
                if backup_exists:
                    _remove_update_path(state["target"])
                    os.replace(state["backup"], state["target"])
                elif not state["had_target"]:
                    _remove_update_path(state["target"])
            except BaseException as restore_error:
                rollback_error = rollback_error or restore_error
        if rollback_error is not None:
            raise RuntimeError("Update failed and rollback could not restore all files.") \
                from rollback_error
        raise error
    else:
        for state in states:
            if state["backup"].exists() or state["backup"].is_symlink():
                _remove_update_path(state["backup"])


def install_release_updates(metadata: dict, components: Iterable[str], output: Output,
                            cancel_event=None,
                            expected_channel: str | None = None) -> str:
    """Stage every selected payload, then commit all components transactionally."""
    selected = list(dict.fromkeys(components))
    if not selected or any(name not in ("manager", "wine") for name in selected):
        raise ValueError("Select at least one valid update component.")
    if not isinstance(metadata, dict):
        raise ValueError("Release metadata must be a JSON object.")
    expected_channel = _expected_update_channel(expected_channel)
    metadata = parse_release_metadata(
        metadata, metadata.get("metadata_url", ""), expected_channel
    )
    offered = available_updates(metadata, expected_channel=expected_channel)
    if any(name not in offered for name in selected):
        raise ValueError("Refusing an equal, older, or unknown-version component update.")

    root = installed_root()
    manager_target = manager_update_target() if "manager" in selected else None
    runner_target = runner_update_target()
    if "manager" in selected and manager_target is None:
        raise RuntimeError(
            "Wine4OfficeManager self-update requires a standalone or installed manager."
        )

    downloads: dict[str, Path] = {}
    manager_staged: Path | None = None
    extraction: Path | None = None
    runner_staged: Path | None = None
    try:
        for name in selected:
            downloads[name] = _download_artifact(
                name, metadata[name], cancel_event, output
            )
        if manager_target is not None:
            manager_staged = _stage_update_file(downloads["manager"], manager_target)
        if "wine" in selected:
            runner_target.parent.mkdir(parents=True, exist_ok=True)
            extraction = Path(tempfile.mkdtemp(
                prefix=".wine-update.", dir=runner_target.parent
            ))
            shutil.rmtree(extraction)
            runner_staged = safe_extract_wine_archive(downloads["wine"], extraction)

        with _install_update_lock(root, manager_target, runner_target):
            if cancel_event is not None and cancel_event.is_set():
                raise RuntimeError("Operation cancelled.")
            offered = available_updates(metadata, expected_channel=expected_channel)
            if any(name not in offered for name in selected):
                raise ValueError(
                    "Refusing an equal, older, or unknown-version component update "
                    "after another updater completed."
                )

            replacements: list[tuple[Path, Path]] = []
            if manager_staged is not None and manager_target is not None:
                replacements.append((manager_staged, manager_target))
            if runner_staged is not None:
                replacements.append((runner_staged, runner_target))

            text_updates: dict[Path, str] = {}
            version_root = root if root is not None else runner_target.parent
            if manager_staged is not None and root is not None:
                manager_version = root / "VERSION"
                wine_version = root / "WINE_VERSION"
                if (runner_target.exists() and not wine_version.exists()
                        and not wine_version.is_symlink() and manager_version.is_file()):
                    text_updates[wine_version] = manager_version.read_text()
                text_updates[manager_version] = metadata["manager"]["version"] + "\n"
            elif manager_staged is not None and manager_target is not None:
                standalone_version = standalone_manager_version_path(manager_target)
                if standalone_version is not None:
                    text_updates[standalone_version] = json.dumps({
                        "version": metadata["manager"]["version"],
                        "sha256": metadata["manager"]["sha256"],
                    }, sort_keys=True) + "\n"
            if runner_staged is not None:
                text_updates[version_root / "WINE_VERSION"] = \
                    metadata["wine"]["version"] + "\n"
            if root is not None:
                text_updates[root / "UPDATE_URL"] = metadata["metadata_url"] + "\n"
            _commit_update_transaction(replacements, text_updates)
    finally:
        for download in downloads.values():
            download.unlink(missing_ok=True)
        if manager_staged is not None:
            manager_staged.unlink(missing_ok=True)
        if extraction is not None:
            shutil.rmtree(extraction, ignore_errors=True)

    if "manager" in selected:
        output(f"Installed Wine4OfficeManager {metadata['manager']['version']}.")
    if "wine" in selected:
        output(f"Installed Wine runner {metadata['wine']['version']}.")
    suffix = " Restart Wine4OfficeManager to use the new manager." \
        if "manager" in selected else ""
    return "Selected updates installed." + suffix


def remove_wine4office(prefix_value: str, remove_prefix: bool, output: Output) -> str:
    root = installed_root()
    if root is None:
        raise RuntimeError("Removal is available only from an installed Wine4OfficeManager.")
    uninstaller = root / "bin/wine4office-uninstall"
    if not uninstaller.is_file() or not os.access(uninstaller, os.X_OK):
        raise FileNotFoundError(f"Wine4Office uninstaller is missing: {uninstaller}")
    command = [str(uninstaller), "--purge-runner"]
    if remove_prefix:
        prefix = validate_prefix(prefix_value)
        if classify_prefix(str(prefix)) != "valid":
            raise ValueError(f"Refusing to remove a path that is not a Wine prefix: {prefix}")
        command.extend(["--remove-prefix", str(prefix)])
    _stream_command(command, os.environ.copy(), output)
    return "Wine4Office removed. You may close this browser tab."


PRELOAD_UNIT = "wine4office-preload.service"
PRELOAD_COMPONENTS = ("ClickToRunSvc",)
_LEGACY_PRELOAD_COMPONENTS = ("ClickToRunSvc", "RpcSs")
_PRELOAD_COMPONENT_IMAGES = {"ClickToRunSvc": "OfficeClickToRun.exe"}
_PRELOAD_SCHEMA = 1
_PRELOAD_HEARTBEAT_SCHEMA = 1
_PRELOAD_SYSTEMCTL_TIMEOUT = 8
_PRELOAD_WINE_TIMEOUT = 12
_PRELOAD_HEARTBEAT_MAX_AGE = 20
_PRELOAD_UNIT_MARKER = "# Managed by Wine4OfficeManager: preload-service-v1"


def preload_binding_path() -> Path:
    return config_home() / "wine4office/preload-service.json"


def preload_unit_path() -> Path:
    return config_home() / "systemd/user" / PRELOAD_UNIT


def preload_runtime_status_path() -> Path:
    runtime = os.environ.get("XDG_RUNTIME_DIR", "").strip()
    if runtime:
        return Path(runtime).expanduser() / "wine4office/preload-service.json"
    return cache_home() / "wine4office/preload-service.json"


def _preload_atomic_write(path: Path, payload: str, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        os.fchmod(descriptor, mode)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        os.chmod(path, mode)
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        temporary.unlink(missing_ok=True)
        raise


def _preload_json_write(path: Path, payload: dict, mode: int = 0o600) -> None:
    _preload_atomic_write(path, json.dumps(payload, sort_keys=True, indent=2) + "\n", mode)


def _preload_snapshot(prefix_value: str, wine_value: str,
                      use_x11: bool = True) -> dict:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    if not has_wine_prefix_layout(prefix):
        raise ValueError(f"The preload binding is not a valid Wine environment: {prefix}")
    return {
        "schema": _PRELOAD_SCHEMA,
        "prefix": str(prefix),
        "wine": str(wine.resolve()),
        "use_x11": bool(use_x11),
        "components": list(PRELOAD_COMPONENTS),
    }


def _validate_preload_binding(payload: object) -> dict:
    if not isinstance(payload, dict):
        raise ValueError("The preload binding must be a JSON object.")
    expected_keys = {"schema", "prefix", "wine", "use_x11", "components"}
    if set(payload) != expected_keys or payload.get("schema") != _PRELOAD_SCHEMA:
        raise ValueError("The preload binding schema is invalid.")
    components = payload.get("components")
    if (
        not isinstance(components, list)
        or (
            components != list(PRELOAD_COMPONENTS)
            and components != list(_LEGACY_PRELOAD_COMPONENTS)
        )
    ):
        raise ValueError("The preload component binding is invalid.")
    if not isinstance(payload.get("use_x11"), bool):
        raise ValueError("The preload display binding is invalid.")
    prefix = validate_prefix(payload.get("prefix", ""))
    wine = normalize_path(payload.get("wine", ""))
    if str(prefix) != payload["prefix"] or str(wine) != payload["wine"]:
        raise ValueError("The preload binding paths must be absolute and normalized.")
    return {
        "schema": _PRELOAD_SCHEMA,
        "prefix": str(prefix),
        "wine": str(wine),
        "use_x11": payload["use_x11"],
        "components": list(PRELOAD_COMPONENTS),
    }


def _read_preload_binding(path: Path | None = None) -> dict:
    target = path or preload_binding_path()
    try:
        payload = json.loads(target.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read the preload binding: {error}") from error
    return _validate_preload_binding(payload)


def _systemctl_user(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess:
    executable = shutil.which("systemctl")
    if not executable:
        raise RuntimeError("systemctl is not installed.")
    try:
        result = subprocess.run(
            [executable, "--user", *command],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=_PRELOAD_SYSTEMCTL_TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("The per-user systemd manager timed out.") from error
    except OSError as error:
        raise RuntimeError(f"Cannot run systemctl --user: {error}") from error
    if check and result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(detail or f"systemctl --user {' '.join(command)} failed.")
    return result


def _systemd_user_capability() -> tuple[bool, str]:
    try:
        _systemctl_user(["show-environment"])
    except RuntimeError as error:
        return False, str(error)
    return True, ""


def systemd_user_available() -> bool:
    return _systemd_user_capability()[0]


def _preload_selected_matches(binding: dict, prefix_value: str | None,
                              wine_value: str | None, use_x11: bool | None) -> bool:
    if prefix_value is None or wine_value is None:
        return True
    try:
        prefix = validate_prefix(prefix_value)
        wine = normalize_path(wine_value)
    except (OSError, ValueError):
        return False
    return (
        str(prefix) == binding["prefix"]
        and str(wine) == binding["wine"]
        and (use_x11 is None or bool(use_x11) == binding["use_x11"])
    )


def _systemctl_property(command: str) -> tuple[bool, str]:
    result = _systemctl_user([command, PRELOAD_UNIT], check=False)
    value = result.stdout.strip()
    detail = value or result.stderr.strip()
    if command == "is-enabled":
        known = {
            "enabled", "enabled-runtime", "linked", "linked-runtime", "alias",
            "static", "indirect", "disabled", "masked", "masked-runtime",
            "generated", "transient", "not-found",
        }
        if value not in known:
            raise RuntimeError(detail or "Cannot determine whether the preload service is enabled.")
        return value in {"enabled", "enabled-runtime"}, detail
    known = {
        "active", "reloading", "activating", "deactivating",
        "inactive", "failed", "unknown",
    }
    if value not in known:
        raise RuntimeError(detail or "Cannot determine whether the preload service is active.")
    return value in {"active", "reloading", "activating", "deactivating"}, detail


def _read_preload_heartbeat() -> tuple[dict | None, str]:
    path = preload_runtime_status_path()
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if (
            not isinstance(payload, dict)
            or payload.get("schema") != _PRELOAD_HEARTBEAT_SCHEMA
            or payload.get("binding") != str(preload_binding_path())
            or not isinstance(payload.get("updated_at"), (int, float))
            or not isinstance(payload.get("components"), dict)
        ):
            raise ValueError("invalid heartbeat schema")
        return payload, ""
    except FileNotFoundError:
        return None, ""
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        return None, f"Cannot read preload heartbeat: {error}"


def preload_service_status(prefix_value: str | None = None, wine_value: str | None = None,
                           use_x11: bool | None = None) -> dict:
    result = {
        "supported": False,
        "reason": "",
        "installed": preload_unit_path().is_file(),
        "enabled": False,
        "active": False,
        "state": "unsupported",
        "binding": None,
        "selected_matches": False,
        "components": {},
        "detail": "",
    }
    supported, reason = _systemd_user_capability()
    result["supported"] = supported
    result["reason"] = reason
    if not supported:
        result["detail"] = reason
        return result

    try:
        result["enabled"], enabled_detail = _systemctl_property("is-enabled")
        result["active"], active_detail = _systemctl_property("is-active")
    except RuntimeError as error:
        result["reason"] = str(error)
        result["detail"] = str(error)
        return result

    try:
        binding = _read_preload_binding()
        result["binding"] = binding
    except FileNotFoundError:
        binding = None
    except ValueError as error:
        result["state"] = "binding-invalid"
        result["detail"] = str(error)
        return result

    if not result["installed"]:
        result["state"] = "uninstalled"
        result["detail"] = enabled_detail or "The preload service is not installed."
        return result
    if binding is None:
        result["state"] = "binding-invalid"
        result["detail"] = "The preload binding is missing."
        return result

    result["selected_matches"] = _preload_selected_matches(
        binding, prefix_value, wine_value, use_x11
    )
    heartbeat, heartbeat_error = _read_preload_heartbeat()
    if heartbeat:
        result["components"] = {
            name: dict(value) for name, value in heartbeat["components"].items()
            if name in PRELOAD_COMPONENTS and isinstance(value, dict)
        }

    if not result["selected_matches"]:
        result["state"] = "mismatch"
        result["detail"] = (
            "The preload service is bound to a different Wine environment. "
            "It was not rebound."
        )
    elif result["active"]:
        fresh = bool(
            heartbeat
            and 0 <= time.time() - heartbeat["updated_at"] <= _PRELOAD_HEARTBEAT_MAX_AGE
        )
        healthy = fresh and all(
            result["components"].get(name, {}).get("state") == "running"
            for name in PRELOAD_COMPONENTS
        )
        result["state"] = "active" if healthy else "degraded"
        result["detail"] = (
            heartbeat.get("detail", "") if heartbeat
            else heartbeat_error or "The service is active but has no fresh heartbeat."
        )
        if not fresh:
            result["detail"] = heartbeat_error or "The preload heartbeat is stale."
    else:
        result["state"] = "inactive"
        result["detail"] = active_detail or "The preload service is inactive."
    return result


def _systemd_quote(value: str | Path) -> str:
    text = os.fspath(value)
    if "\0" in text or "\n" in text or "\r" in text:
        raise ValueError("A preload service path contains an unsafe control character.")
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("$", "$$").replace("%", "%%")
    return f'"{escaped}"'


def _preload_manager_executable() -> Path:
    if getattr(sys, "frozen", False):
        manager = Path(sys.executable).resolve()
    else:
        manager = Path(__file__).resolve().with_name("wine4office_manager.py")
    if not manager.is_absolute() or not manager.is_file() or not os.access(manager, os.X_OK):
        raise FileNotFoundError(f"Wine4OfficeManager executable is unavailable: {manager}")
    return manager


def _preload_unit_text(binding_path: Path, status_path: Path) -> str:
    arguments = " ".join(
        _systemd_quote(value) for value in (
            _preload_manager_executable(), "--preload-worker", binding_path, status_path
        )
    )
    return (
        f"{_PRELOAD_UNIT_MARKER}\n"
        "[Unit]\n"
        "Description=Wine4Office Click-to-Run preload\n\n"
        "[Service]\n"
        "Type=simple\n"
        f"ExecStart={arguments}\n"
        "Restart=on-failure\n"
        "KillMode=process\n"
        "TimeoutStopSec=20\n\n"
        "[Install]\n"
        "WantedBy=default.target\n"
    )


def _snapshot_file(path: Path) -> tuple[bytes, int] | None:
    try:
        return path.read_bytes(), stat.S_IMODE(path.stat().st_mode)
    except FileNotFoundError:
        return None


def _restore_file(path: Path, snapshot: tuple[bytes, int] | None) -> None:
    if snapshot is None:
        path.unlink(missing_ok=True)
    else:
        _atomic_write_bytes(path, snapshot[0], snapshot[1])


def install_preload_service(prefix_value: str, wine_value: str,
                            use_x11: bool = True) -> dict:
    supported, reason = _systemd_user_capability()
    if not supported:
        raise RuntimeError(reason)
    binding = _preload_snapshot(prefix_value, wine_value, use_x11)
    binding_path = preload_binding_path()
    unit_path = preload_unit_path()
    if unit_path.exists() and not _owned_preload_unit():
        raise RuntimeError(
            f"Refusing to replace an unowned user service unit: {unit_path}"
        )
    try:
        existing = _read_preload_binding(binding_path)
    except FileNotFoundError:
        existing = None
    was_enabled, _ = _systemctl_property("is-enabled")
    was_active, _ = _systemctl_property("is-active")
    if existing is None and (was_enabled or was_active):
        raise RuntimeError(
            "The installed preload service has no valid binding; disable and stop it "
            "before repairing it."
        )
    if existing is not None and existing != binding and (was_enabled or was_active):
        raise RuntimeError(
            "The existing preload service is bound to a different Wine environment. "
            "Disable it at login and stop it before explicitly rebinding."
        )

    snapshots = {
        binding_path: _snapshot_file(binding_path),
        unit_path: _snapshot_file(unit_path),
    }
    try:
        _preload_json_write(binding_path, binding, 0o600)
        _preload_atomic_write(
            unit_path, _preload_unit_text(binding_path, preload_runtime_status_path()), 0o644
        )
        _systemctl_user(["daemon-reload"])
        _systemctl_user(["enable", PRELOAD_UNIT])
    except BaseException:
        if not was_enabled:
            try:
                _systemctl_user(["disable", PRELOAD_UNIT], check=False)
            except RuntimeError:
                pass
        for path, snapshot in snapshots.items():
            _restore_file(path, snapshot)
        try:
            _systemctl_user(["daemon-reload"], check=False)
        except RuntimeError:
            pass
        raise
    return preload_service_status(prefix_value, wine_value, use_x11)


def preload_office_processes(prefix_value: str, wine_value: str,
                             use_x11: bool = True) -> list[str]:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    try:
        completed = subprocess.run(
            [str(wine), "tasklist.exe", "/FO", "CSV", "/NH"],
            env=wine_environment(prefix, wine, use_x11),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=_PRELOAD_WINE_TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Wine process detection timed out; refusing to stop preload.") from error
    except OSError as error:
        raise RuntimeError(f"Wine process detection failed; refusing to stop preload: {error}") from error
    if completed.returncode:
        detail = (completed.stderr or completed.stdout).strip()
        raise RuntimeError(
            f"Wine process detection failed; refusing to stop preload: {detail or completed.returncode}"
        )
    office_names = {metadata["exe"].casefold() for metadata in APP_META.values()}
    found: list[str] = []
    try:
        rows = csv.reader(io.StringIO(completed.stdout), strict=True)
        for row in rows:
            if not row:
                continue
            if len(row) < 2:
                raise csv.Error("tasklist returned a non-CSV status line")
            image = row[0].strip()
            if image.casefold() in office_names and image.casefold() not in {
                value.casefold() for value in found
            }:
                found.append(image)
    except csv.Error as error:
        raise RuntimeError(
            f"Wine process output was invalid; refusing to stop preload: {error}"
        ) from error
    return found


def manage_preload_service(action: str, prefix_value: str | None = None,
                           wine_value: str | None = None,
                           use_x11: bool | None = None) -> dict:
    if action not in {"disable", "start", "stop"}:
        raise ValueError(f"Unknown preload service action: {action}")
    supported, reason = _systemd_user_capability()
    if not supported:
        raise RuntimeError(reason)

    if action == "disable":
        enabled, _ = _systemctl_property("is-enabled")
        if enabled:
            _systemctl_user(["disable", PRELOAD_UNIT])
        return preload_service_status(prefix_value, wine_value, use_x11)

    binding = _read_preload_binding()
    if not preload_unit_path().is_file():
        raise RuntimeError("The preload service is not installed.")
    if not _preload_selected_matches(binding, prefix_value, wine_value, use_x11):
        raise RuntimeError(
            "The selected Wine environment does not match the fixed preload binding."
        )
    if action == "stop":
        active_office = preload_office_processes(
            binding["prefix"], binding["wine"], binding["use_x11"]
        )
        if active_office:
            raise RuntimeError(
                "Office is active; refusing to stop preload: " + ", ".join(active_office)
            )
    _systemctl_user([action, PRELOAD_UNIT])
    return preload_service_status(prefix_value, wine_value, use_x11)


def _preload_component_process_running(
    binding: dict, component: str, proc_root: Path = Path("/proc")
) -> bool | None:
    """Detect a component before a Wine command can auto-start it.

    ``sc query`` starts Wine's service infrastructure and may auto-start
    ClickToRunSvc. Looking at the existing Linux process first lets the worker
    retain ownership of a service that its first query caused to start.
    ``None`` is conservative: a matching process existed but its environment
    could not be inspected.
    """
    expected_image = _PRELOAD_COMPONENT_IMAGES.get(component)
    if expected_image is None:
        return None
    try:
        processes = tuple(proc_root.iterdir())
    except OSError:
        return None

    matching_unknown = False
    for process in processes:
        if not process.name.isdigit():
            continue
        try:
            arguments = process.joinpath("cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        if not arguments or not arguments[0]:
            continue
        image = re.split(r"[\\/]", os.fsdecode(arguments[0]))[-1]
        if image.casefold() != expected_image.casefold():
            continue
        try:
            environment = process.joinpath("environ").read_bytes().split(b"\0")
        except OSError:
            matching_unknown = True
            continue
        prefix_entry = f"WINEPREFIX={binding['prefix']}".encode(
            sys.getfilesystemencoding(), "surrogateescape"
        )
        if prefix_entry in environment:
            return True
    return None if matching_unknown else False


def _preload_component_state(binding: dict, component: str) -> tuple[str, str]:
    try:
        completed = subprocess.run(
            [binding["wine"], "sc.exe", "query", component],
            env=wine_environment(
                binding["prefix"], binding["wine"], binding["use_x11"]
            ),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=_PRELOAD_WINE_TIMEOUT,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return "unknown", str(error)
    output = f"{completed.stdout}\n{completed.stderr}"
    match = re.search(r"\bSTATE\s*:\s*\d+\s+([A-Z_]+)", output, re.IGNORECASE)
    if match:
        state = match.group(1).casefold()
        if state in {"running", "stopped"}:
            return state, state
        return "pending", state
    if completed.returncode:
        return "missing", output.strip() or f"exit {completed.returncode}"
    return "unknown", output.strip() or "Wine returned no service state."


def _preload_component_action(binding: dict, action: str, component: str) -> tuple[bool, str]:
    try:
        completed = subprocess.run(
            [binding["wine"], "sc.exe", action, component],
            env=wine_environment(
                binding["prefix"], binding["wine"], binding["use_x11"]
            ),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=_PRELOAD_WINE_TIMEOUT,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return False, str(error)
    detail = (completed.stdout or completed.stderr).strip()
    return completed.returncode == 0, detail or f"exit {completed.returncode}"


def _write_preload_heartbeat(path: Path, components: dict, state: str,
                             detail: str = "") -> None:
    _preload_json_write(path, {
        "schema": _PRELOAD_HEARTBEAT_SCHEMA,
        "binding": str(preload_binding_path()),
        "pid": os.getpid(),
        "updated_at": time.time(),
        "state": state,
        "components": components,
        "detail": detail,
    })


def run_preload_worker(snapshot_path: PathValue, status_path: PathValue) -> int:
    snapshot = normalize_path(snapshot_path)
    status = normalize_path(status_path)
    if snapshot != preload_binding_path().resolve(strict=False):
        raise ValueError("The worker binding path is not the owned preload snapshot.")
    if status != preload_runtime_status_path().resolve(strict=False):
        raise ValueError("The worker status path is not the owned preload heartbeat.")
    binding = _read_preload_binding(snapshot)
    prefix = validate_prefix(binding["prefix"])
    if not has_wine_prefix_layout(prefix):
        raise ValueError(f"The bound Wine environment is invalid: {prefix}")
    require_wine(binding["wine"])

    stopping = False

    def request_stop(_signum, _frame) -> None:
        nonlocal stopping
        stopping = True

    previous_term = signal.signal(signal.SIGTERM, request_stop)
    previous_int = signal.signal(signal.SIGINT, request_stop)
    components: dict[str, dict] = {}
    restart_attempts = {name: 0 for name in PRELOAD_COMPONENTS}
    try:
        for component in PRELOAD_COMPONENTS:
            preexisting = _preload_component_process_running(binding, component)
            state, detail = _preload_component_state(binding, component)
            owned = preexisting is False and state == "running"
            if state == "stopped":
                started, start_detail = _preload_component_action(binding, "start", component)
                if started:
                    owned = True
                    state, detail = _preload_component_state(binding, component)
                else:
                    detail = start_detail
            components[component] = {
                "state": state,
                "owned": owned,
                "detail": detail,
            }
            _write_preload_heartbeat(status, components, "starting")

        while not stopping:
            degraded: list[str] = []
            for component in PRELOAD_COMPONENTS:
                state, detail = _preload_component_state(binding, component)
                record = components[component]
                if state != "running" and record["owned"] and restart_attempts[component] < 3:
                    restart_attempts[component] += 1
                    restarted, restart_detail = _preload_component_action(
                        binding, "start", component
                    )
                    if restarted:
                        state, detail = _preload_component_state(binding, component)
                    else:
                        detail = restart_detail
                if state != "running":
                    degraded.append(component)
                record["state"] = state
                record["detail"] = detail
            _write_preload_heartbeat(
                status, components, "degraded" if degraded else "running",
                "Not running: " + ", ".join(degraded) if degraded else "",
            )
            for _ in range(10):
                if stopping:
                    break
                time.sleep(0.5)

        try:
            active_office = preload_office_processes(
                binding["prefix"], binding["wine"], binding["use_x11"]
            )
        except RuntimeError as error:
            _write_preload_heartbeat(
                status, components, "stop-refused",
                f"Process state unknown; owned components were preserved: {error}",
            )
            return 1
        if active_office:
            _write_preload_heartbeat(
                status, components, "stop-refused",
                "Office became active; owned components were preserved: "
                + ", ".join(active_office),
            )
            return 1
        cleanup_failed: list[str] = []
        for component in reversed(PRELOAD_COMPONENTS):
            record = components[component]
            if not record["owned"]:
                continue
            stopped, detail = _preload_component_action(binding, "stop", component)
            record["state"] = "stopped" if stopped else "unknown"
            record["detail"] = detail
            if not stopped:
                cleanup_failed.append(component)
            _write_preload_heartbeat(status, components, "stopping")
        if cleanup_failed:
            _write_preload_heartbeat(
                status, components, "degraded",
                "Could not stop owned components: " + ", ".join(cleanup_failed),
            )
            return 1
        _write_preload_heartbeat(status, components, "stopped")
        return 0
    finally:
        signal.signal(signal.SIGTERM, previous_term)
        signal.signal(signal.SIGINT, previous_int)


def _owned_preload_unit() -> bool:
    try:
        return preload_unit_path().read_text(encoding="utf-8").startswith(
            _PRELOAD_UNIT_MARKER + "\n"
        )
    except (OSError, UnicodeError):
        return False


def uninstall_preload_service() -> None:
    binding_path = preload_binding_path()
    unit_path = preload_unit_path()
    status_path = preload_runtime_status_path()
    if not any(path.exists() for path in (binding_path, unit_path, status_path)):
        return
    owned_unit = _owned_preload_unit()
    try:
        binding = _read_preload_binding(binding_path)
    except (FileNotFoundError, ValueError):
        binding = None
    if owned_unit:
        supported, reason = _systemd_user_capability()
        if not supported:
            raise RuntimeError(
                f"Cannot safely remove the installed preload service: {reason}"
            )
        active, _ = _systemctl_property("is-active")
        enabled, _ = _systemctl_property("is-enabled")
        if active:
            if binding is None:
                raise RuntimeError(
                    "Cannot safely stop preload because its binding is missing or invalid."
                )
            manage_preload_service(
                "stop", binding["prefix"], binding["wine"], binding["use_x11"]
            )
        if enabled:
            _systemctl_user(["disable", PRELOAD_UNIT])
        unit_path.unlink(missing_ok=True)
        _systemctl_user(["daemon-reload"])
    if binding is not None:
        binding_path.unlink(missing_ok=True)
    heartbeat, _ = _read_preload_heartbeat()
    if heartbeat is not None:
        status_path.unlink(missing_ok=True)
