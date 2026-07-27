#!/usr/bin/env python3
"""Backend operations for Wine4OfficeManager."""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import struct
import tarfile
import tempfile
import sys
import time
import uuid
import urllib.parse
import urllib.request
from contextlib import contextmanager
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable

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
}

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


def normalize_path(value: str) -> Path:
    return Path(os.path.expandvars(value.strip())).expanduser().resolve(strict=False)


def validate_prefix(value: str) -> Path:
    if not value.strip():
        raise ValueError("The Wine environment path is empty.")
    prefix = normalize_path(value)
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


def wine_environment(prefix: str | Path, wine: str | Path) -> dict[str, str]:
    env = os.environ.copy()
    env.update({
        "WINEPREFIX": str(prefix),
        "WINEARCH": "win64",
        "WINEDLLOVERRIDES": env.get("WINEDLLOVERRIDES", "riched20=n;mscoree=;mshtml=b"),
    })
    wine_bin = str(Path(wine).parent)
    env["PATH"] = wine_bin + os.pathsep + env.get("PATH", "")
    if env.get("WAYLAND_DISPLAY"):
        env.pop("DISPLAY", None)
    return env


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


def stop_wine(prefix_value: str, wine_value: str) -> None:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    wineserver = sibling_tool(wine, "wineserver")
    if wineserver:
        subprocess.run([str(wineserver), "-k"], env=wine_environment(prefix, wine),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15, check=False)


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


def register_cloud_fonts(prefix: Path, wine: Path, helper: Path | None = None) -> None:
    candidates = [prefix / "register-office-cloud-fonts.sh"]
    if helper:
        candidates.append(helper)
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            subprocess.run([str(candidate)], env=wine_environment(prefix, wine), check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
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


def launch_app(prefix_value: str, wine_value: str, app: str, helper: Path | None = None,
               documents: Iterable[str] = ()) -> int:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    executable = find_office_app(str(prefix), app)
    if not executable:
        raise FileNotFoundError(f"{APP_META[app]['exe']} is not installed in {prefix}")
    if app == "word":
        prepare_office_building_blocks(prefix)
    register_cloud_fonts(prefix, wine, helper)
    arguments = list(documents)
    env = wine_environment(prefix, wine)
    if app == "outlook":
        prepare_outlook_first_run(prefix, wine, env)
        env = _outlook_environment(env)
    process = subprocess.Popen([str(wine), str(executable), *arguments], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               start_new_session=True)
    return process.pid


def launch_executable(prefix_value: str, wine_value: str, executable_value: str,
                      arguments: str | Iterable[str] = ()) -> int:
    prefix = validate_prefix(prefix_value)
    if not (prefix / "system.reg").is_file():
        raise FileNotFoundError(f"Wine environment is not initialized: {prefix}")
    wine = require_wine(wine_value)
    executable = normalize_path(executable_value)
    if not executable.is_file():
        raise FileNotFoundError(f"Windows executable was not found: {executable}")
    if executable.suffix.lower() != ".exe":
        raise ValueError("Only .exe files can be launched from this control.")
    parsed_arguments = shlex.split(arguments) if isinstance(arguments, str) else list(arguments)
    process = subprocess.Popen([str(wine), str(executable), *parsed_arguments],
                               env=wine_environment(prefix, wine),
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


def launch_tool(prefix_value: str, wine_value: str, tool: str) -> int | None:
    if tool == "stop":
        stop_wine(prefix_value, wine_value)
        return None
    if tool not in TOOL_META:
        raise ValueError(f"Unknown Wine tool: {tool}")
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    env = wine_environment(prefix, wine)
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


def create_app_shortcuts(apps: Iterable[str], prefix_value: str, wine_value: str, launcher: Path,
                         copy_to_desktop: bool) -> list[str]:
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
        command = [str(launcher), "--prefix", str(prefix), "--wine", str(wine), app, "%F"]
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
    source_icon = icons / "wine4office-manager.svg"
    if not source_icon.is_file():
        raise FileNotFoundError(f"Wine4OfficeManager icon is missing: {source_icon}")
    installed_icon = data_home() / "icons/hicolor/scalable/apps/wine4office-manager.svg"
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


def fetch_release_metadata(metadata_url: str, output: Output | None = None,
                           expected_channel: str | None = None) -> dict:
    metadata_url = _https_url(metadata_url, "Metadata address")
    if output:
        output(f"Checking {metadata_url}")
    request = urllib.request.Request(
        metadata_url, headers={"User-Agent": "Wine4OfficeManager/1"}
    )
    with urllib.request.urlopen(request, timeout=30) as response:
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
        with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as target:
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
