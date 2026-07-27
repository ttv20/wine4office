#!/usr/bin/env python3
"""Backend operations for the Wine 365 Manager."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Callable, Iterable

APP_META = {
    "word": {
        "name": "Microsoft Word (Wine 365)",
        "exe": "WINWORD.EXE",
        "icon": "wine365-word.svg",
        "categories": "Office;WordProcessor;",
        "mime": "application/msword;application/vnd.openxmlformats-officedocument.wordprocessingml.document;",
    },
    "excel": {
        "name": "Microsoft Excel (Wine 365)",
        "exe": "EXCEL.EXE",
        "icon": "wine365-excel.svg",
        "categories": "Office;Spreadsheet;",
        "mime": "application/vnd.ms-excel;application/vnd.openxmlformats-officedocument.spreadsheetml.sheet;",
    },
    "powerpoint": {
        "name": "Microsoft PowerPoint (Wine 365)",
        "exe": "POWERPNT.EXE",
        "icon": "wine365-powerpoint.svg",
        "categories": "Office;Presentation;",
        "mime": "application/vnd.ms-powerpoint;application/vnd.openxmlformats-officedocument.presentationml.presentation;",
    },
    "outlook": {
        "name": "Microsoft Outlook (Wine 365)",
        "exe": "OUTLOOK.EXE",
        "icon": "wine365-outlook.svg",
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


def data_home() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")).expanduser()


def config_home() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")).expanduser()


def cache_home() -> Path:
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")).expanduser()


def config_path() -> Path:
    return config_home() / "wine365/config.json"


def installed_root() -> Path | None:
    here = Path(__file__).resolve().parent
    return here.parent if here.name == "lib" else None


def current_version() -> str:
    root = installed_root()
    version = root / "VERSION" if root else None
    if version and version.is_file():
        value = version.read_text(errors="replace").strip()
        if value:
            return value
    return "development"


def configured_update_url() -> str:
    root = installed_root()
    address = root / "UPDATE_URL" if root else None
    return address.read_text(errors="replace").strip() if address and address.is_file() else ""


def default_config() -> dict:
    return {
        "prefix": str(Path.home() / ".wine365"),
        "wine": detect_wine(),
        "desktop_copy": False,
        "update_url": configured_update_url(),
    }


def detect_wine() -> str:
    candidates: list[Path] = []
    if os.environ.get("WINE365_WINE"):
        candidates.append(Path(os.environ["WINE365_WINE"]).expanduser())
    candidates.append(data_home() / "wine365/runner/bin/wine")

    bottles = data_home() / "bottles/bottles/Wine365/bottle.yml"
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
    return str(data_home() / "wine365/runner/bin/wine")


def load_config() -> dict:
    result = default_config()
    path = config_path()
    if path.is_file():
        try:
            saved = json.loads(path.read_text())
            if isinstance(saved, dict):
                for key in result:
                    if key in saved:
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
    return Path(os.path.expandvars(value)).expanduser().resolve(strict=False)


def validate_prefix(value: str) -> Path:
    if not value.strip():
        raise ValueError("The Wine environment path is empty.")
    prefix = normalize_path(value)
    home = Path.home().resolve()
    if prefix in (Path("/"), home):
        raise ValueError(f"Refusing unsafe Wine environment path: {prefix}")
    return prefix


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


def create_environment(prefix_value: str, wine_value: str, recreate: bool, output: Output) -> str:
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    if prefix.exists() and not recreate and any(prefix.iterdir()):
        raise FileExistsError(f"The environment already exists and is not empty: {prefix}")

    backup: Path | None = None
    if recreate and prefix.exists():
        stop_wine(str(prefix), str(wine))
        backup = prefix.with_name(f".{prefix.name}.wine365-backup-{int(time.time())}")
        if backup.exists():
            raise FileExistsError(f"Backup path already exists: {backup}")
        output(f"Moving the current environment to {backup}")
        prefix.rename(backup)

    try:
        prefix.parent.mkdir(parents=True, exist_ok=True)
        wineboot = sibling_tool(wine, "wineboot")
        command = [str(wineboot), "-u"] if wineboot else [str(wine), "wineboot.exe", "-u"]
        _stream_command(command, wine_environment(prefix, wine), output)
        _stream_command([
            str(wine), "reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Graphics",
            "/d", "x11,wayland", "/f",
        ], wine_environment(prefix, wine), output)
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


def launch_tool(prefix_value: str, wine_value: str, tool: str) -> int | None:
    if tool == "stop":
        stop_wine(prefix_value, wine_value)
        return None
    if tool not in TOOL_META:
        raise ValueError(f"Unknown Wine tool: {tool}")
    prefix = validate_prefix(prefix_value)
    wine = require_wine(wine_value)
    binary_name, arguments = TOOL_META[tool]
    binary = wine if binary_name == "wine" else sibling_tool(wine, binary_name)
    if binary is None:
        arguments = [f"{binary_name}.exe", *arguments]
        binary = wine
    process = subprocess.Popen([str(binary), *arguments], env=wine_environment(prefix, wine),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               start_new_session=True)
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


def _owned_desktop_file(path: Path) -> bool:
    try:
        return "X-Wine365-Managed=true" in path.read_text(errors="replace")
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
    lines.append("X-Wine365-Managed=true")
    temporary = path.with_suffix(".tmp")
    temporary.write_text("\n".join(lines) + "\n")
    temporary.chmod(0o755)
    os.replace(temporary, path)


def create_app_shortcuts(apps: Iterable[str], prefix_value: str, wine_value: str, launcher: Path,
                         icons: Path, copy_to_desktop: bool) -> list[str]:
    prefix = validate_prefix(prefix_value)
    wine = normalize_path(wine_value)
    created: list[str] = []
    for app in apps:
        if app not in APP_META:
            raise ValueError(f"Unknown Office application: {app}")
        meta = APP_META[app]
        command = [str(launcher), "--prefix", str(prefix), "--wine", str(wine), app, "%F"]
        filename = f"wine365-{app}.desktop"
        menu_file = data_home() / "applications" / filename
        write_desktop_file(menu_file, meta["name"], f"Launch {meta['name']} in {prefix}", command,
                           icons / meta["icon"], meta["categories"], meta["mime"])
        created.append(str(menu_file))
        desktop_file = desktop_directory() / filename
        if copy_to_desktop:
            write_desktop_file(desktop_file, meta["name"], f"Launch {meta['name']} in {prefix}", command,
                               icons / meta["icon"], meta["categories"], meta["mime"])
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
        filename = f"wine365-{app}.desktop"
        for path in (data_home() / "applications" / filename, desktop_directory() / filename):
            if path.is_file() and _owned_desktop_file(path):
                path.unlink()
                removed.append(str(path))
    refresh_desktop_database()
    return removed


def install_manager_shortcut(manager_launcher: Path, icons: Path) -> Path:
    path = data_home() / "applications/wine365-manager.desktop"
    write_desktop_file(path, "Wine 365 Manager", "Manage Wine 365 environments and shortcuts",
                       [str(manager_launcher)], icons / "wine365-manager.svg", "Utility;Settings;")
    refresh_desktop_database()
    return path


def refresh_desktop_database() -> None:
    utility = shutil.which("update-desktop-database")
    if utility:
        subprocess.run([utility, str(data_home() / "applications")], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _https_url(value: str, description: str) -> str:
    value = value.strip()
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValueError(f"{description} must be an HTTPS URL.")
    return value


def update_wine365(update_url: str, output: Output, cancel_event=None,
                   process_callback=None) -> str:
    if not update_url.strip():
        raise ValueError("No update address is configured yet.")
    manifest_url = _https_url(update_url, "Update manifest address")
    output(f"Checking {manifest_url}")
    request = urllib.request.Request(manifest_url, headers={"User-Agent": "Wine365-Manager/1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        manifest_bytes = response.read(1_048_577)
    if len(manifest_bytes) > 1_048_576:
        raise ValueError("Update manifest is larger than 1 MiB.")
    try:
        manifest = json.loads(manifest_bytes)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("Update manifest is not valid JSON.") from error
    if not isinstance(manifest, dict):
        raise ValueError("Update manifest must be a JSON object.")

    version = str(manifest.get("version", "")).strip()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}", version):
        raise ValueError("Update manifest has an invalid version.")
    if version == current_version():
        return f"Wine 365 {version} is already installed."
    installer_url = urllib.parse.urljoin(manifest_url, str(manifest.get("installer_url", "")).strip())
    installer_url = _https_url(installer_url, "Installer address")
    expected = str(manifest.get("sha256", "")).lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Update manifest has an invalid SHA-256 digest.")

    download_dir = cache_home() / "wine365/updates"
    download_dir.mkdir(parents=True, exist_ok=True)
    installer = download_dir / f"wine365-{version}.run.part"
    digest = hashlib.sha256()
    downloaded = 0
    output(f"Downloading Wine 365 {version}")
    request = urllib.request.Request(installer_url, headers={"User-Agent": "Wine365-Manager/1"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, installer.open("wb") as destination:
            size_header = response.headers.get("Content-Length")
            if size_header and int(size_header) > 8 * 1024**3:
                raise ValueError("Update installer is larger than 8 GiB.")
            while True:
                if cancel_event is not None and cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                downloaded += len(chunk)
                if downloaded > 8 * 1024**3:
                    raise ValueError("Update installer is larger than 8 GiB.")
                digest.update(chunk)
                destination.write(chunk)
        if digest.hexdigest() != expected:
            raise ValueError("Downloaded installer failed SHA-256 verification.")
        installer.chmod(0o700)
        output(f"Verified {downloaded} bytes; installing update")
        _stream_command([str(installer), "--update"], os.environ.copy(), output,
                        cancel_event=cancel_event, process_callback=process_callback)
    finally:
        installer.unlink(missing_ok=True)
    return f"Wine 365 {version} installed. Restart the manager to use the update."


def remove_wine365(prefix_value: str, remove_prefix: bool, output: Output) -> str:
    root = installed_root()
    if root is None:
        raise RuntimeError("Removal is available only from an installed Wine 365 Manager.")
    uninstaller = root / "bin/wine365-uninstall"
    if not uninstaller.is_file() or not os.access(uninstaller, os.X_OK):
        raise FileNotFoundError(f"Wine 365 uninstaller is missing: {uninstaller}")
    command = [str(uninstaller), "--purge-runner"]
    if remove_prefix:
        command.extend(["--remove-prefix", str(validate_prefix(prefix_value))])
    _stream_command(command, os.environ.copy(), output)
    return "Wine 365 removed. You may close this browser tab."
