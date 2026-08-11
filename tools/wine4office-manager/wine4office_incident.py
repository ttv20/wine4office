#!/usr/bin/env python3
"""Privacy-conscious Office crash and hang capture for Wine4Office."""

from __future__ import annotations

import base64
import ctypes
import ctypes.util
import hashlib
import json
import math
import os
import platform
import re
import select
import shutil
import ssl
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Iterable


REPORT_MODE_ASK = "ask"
REPORT_MODE_DISABLED = "disabled"
REPORT_MODES = frozenset({REPORT_MODE_ASK, REPORT_MODE_DISABLED})
MAX_RING_BYTES = 2 * 1024 * 1024
MAX_TRACE_BYTES = 50 * 1024 * 1024
MAX_ATTACHMENT_BYTES = 25 * 1024 * 1024
ATTACHMENT_SUFFIXES = frozenset({
    ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".pdf", ".rtf",
    ".csv", ".txt",
})
STREAM_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}")
REPORTING_ENVIRONMENT_KEYS = frozenset({
    "OPENOBSERVE_URL",
    "OPENOBSERVE_ACCOUNT",
    "OPENOBSERVE_TOKEN",
    "OPENOBSERVE_ORG",
    "OPENOBSERVE_STREAM",
    "OPENOBSERVE_TRACE_STREAM",
    "OPENOBSERVE_ARTIFACT_STREAM",
})
_CACHED_OPENOBSERVE_ENVIRONMENT: dict[str, str] | None = None
SECRET_KEY_NAMES = frozenset({
    "authorization", "credential", "password", "passwd", "secret", "token",
})
APP_EXECUTABLES = {
    "word": "winword.exe",
    "excel": "excel.exe",
    "powerpoint": "powerpnt.exe",
    "outlook": "outlook.exe",
    "setlang": "setlang.exe",
}


def _data_home() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")).expanduser()


def incident_home() -> Path:
    return _data_home() / "wine4office/incidents"


def _https_url(value: str, label: str) -> str:
    parsed = urllib.parse.urlsplit(value.strip())
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username
            or parsed.password or parsed.query or parsed.fragment):
        raise ValueError(f"{label} must be an HTTPS URL without credentials, query or fragment.")
    return urllib.parse.urlunsplit((parsed.scheme, parsed.netloc, parsed.path.rstrip("/"), "", ""))


def _is_reporting_environment_key(key: str) -> bool:
    return key in REPORTING_ENVIRONMENT_KEYS or key.startswith("OPENOBSERVE_")


def clean_reporting_environment(environ: dict[str, str] | None = None) -> dict[str, str]:
    """Copy an environment without reporting configuration or credentials."""
    source = os.environ if environ is None else environ
    return {
        key: value for key, value in source.items()
        if not _is_reporting_environment_key(key)
    }


def _remove_reporting_environment() -> None:
    for key in list(os.environ):
        if _is_reporting_environment_key(key):
            os.environ.pop(key, None)


def openobserve_environment(environ: dict[str, str] | None = None) -> dict[str, str] | None:
    """Return complete runtime OpenObserve configuration, or hide the feature.

    Secrets are read only from the process environment and are never persisted in
    the Manager configuration or an incident file.  Once read, reporting
    configuration is removed from the process environment so Wine, Office and
    helper children cannot inherit it.
    """
    global _CACHED_OPENOBSERVE_ENVIRONMENT
    source = os.environ if environ is None else environ
    names = {
        "url": "OPENOBSERVE_URL",
        "account": "OPENOBSERVE_ACCOUNT",
        "token": "OPENOBSERVE_TOKEN",
        "org": "OPENOBSERVE_ORG",
        "incident_stream": "OPENOBSERVE_STREAM",
        "trace_stream": "OPENOBSERVE_TRACE_STREAM",
        "artifact_stream": "OPENOBSERVE_ARTIFACT_STREAM",
    }
    if (environ is None
            and not any(_is_reporting_environment_key(name) for name in source)):
        return (
            dict(_CACHED_OPENOBSERVE_ENVIRONMENT)
            if _CACHED_OPENOBSERVE_ENVIRONMENT is not None else None
        )
    values = {key: str(source.get(name, "")).strip() for key, name in names.items()}
    if environ is None:
        _remove_reporting_environment()
    if not all(values.values()):
        return None
    values["url"] = _https_url(values["url"], "OPENOBSERVE_URL")
    if "\n" in values["account"] or "\r" in values["account"]:
        raise ValueError("OPENOBSERVE_ACCOUNT is invalid.")
    for key in ("org", "incident_stream", "trace_stream", "artifact_stream"):
        if not STREAM_PATTERN.fullmatch(values[key]):
            raise ValueError(f"{names[key]} is invalid.")
    if environ is None:
        _CACHED_OPENOBSERVE_ENVIRONMENT = dict(values)
    return values


def reporting_available(environ: dict[str, str] | None = None) -> bool:
    try:
        return openobserve_environment(environ) is not None
    except ValueError:
        return False


def monitoring_enabled(config: dict, environ: dict[str, str] | None = None) -> bool:
    return (
        reporting_available(environ)
        and config.get("incident_reporting_mode", REPORT_MODE_ASK) == REPORT_MODE_ASK
    )


def _initialize_reporting_environment() -> None:
    if not any(_is_reporting_environment_key(name) for name in os.environ):
        return
    try:
        openobserve_environment()
    except ValueError:
        _remove_reporting_environment()


_initialize_reporting_environment()


def _is_secret_key(name: object) -> bool:
    normalized = re.sub(r"[-.]", "_", str(name).strip("\"'")).casefold()
    if normalized in SECRET_KEY_NAMES:
        return True
    return normalized.endswith((
        "token", "secret", "password", "passwd", "authorization", "credential",
        "_api_key", "_access_key", "apikey", "accesskey",
    ))


def _redact_structure(value):
    if isinstance(value, dict):
        return {
            key: "<redacted>" if _is_secret_key(key) else _redact_structure(item)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [_redact_structure(item) for item in value]
    if isinstance(value, tuple):
        return tuple(_redact_structure(item) for item in value)
    if isinstance(value, str):
        return redact_text(value)
    return value


def redact_text(value: str) -> str:
    """Remove common identity, path and structured credential shapes."""
    text = str(value)
    home = str(Path.home())
    if home and home != "/":
        text = text.replace(home, "$HOME")
    text = re.sub(r"(?<![A-Za-z0-9._-])/home/[^/\s:'\"]+", "/home/$USER", text)
    text = re.sub(
        r"(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b", "<redacted-email>", text,
    )

    def replace_quoted(match: re.Match[str]) -> str:
        if not _is_secret_key(match.group("key")):
            return match.group(0)
        quote = match.group("quote")
        return f"{match.group('key')}{match.group('separator')}{quote}<redacted>{quote}"

    for quote in ('"', "'"):
        quoted = re.compile(
            rf'(?P<key>"?[A-Za-z][A-Za-z0-9_.-]*"?)'
            rf'(?P<separator>\s*[:=]\s*)(?P<quote>{quote})'
            rf'(?P<value>(?:\\.|[^{quote}\\])*){quote}'
        )
        text = quoted.sub(replace_quoted, text)

    def replace_unquoted(match: re.Match[str]) -> str:
        if not _is_secret_key(match.group("key")):
            return match.group(0)
        return f"{match.group('key')}{match.group('separator')}<redacted>"

    authorization = re.compile(
        r'(?P<key>"?[A-Za-z0-9_.-]*authorization"?)'
        r'(?P<separator>\s*[:=]\s*)(?P<value>[^,\r\n;}\]]+)',
        re.I,
    )
    text = authorization.sub(replace_unquoted, text)
    unquoted = re.compile(
        r'(?P<key>"?[A-Za-z][A-Za-z0-9_.-]*"?)'
        r'(?P<separator>\s*[:=]\s*)(?P<value>[^,\s;}\]]+)'
    )
    return unquoted.sub(replace_unquoted, text)


def _linux_release() -> str:
    path = Path("/etc/os-release")
    try:
        fields = {}
        for line in path.read_text(errors="replace").splitlines():
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            fields[key] = value.strip().strip('"')
        return fields.get("PRETTY_NAME") or fields.get("NAME") or "Unknown Linux"
    except OSError:
        return "Unknown Linux"


def _memory_class() -> str:
    try:
        match = re.search(r"^MemTotal:\s+(\d+)\s+kB", Path("/proc/meminfo").read_text(), re.M)
        total_gib = int(match.group(1)) / 1024 / 1024 if match else 0
    except (OSError, ValueError):
        return "unknown"
    for boundary in (2, 4, 8, 16, 32, 64, 128):
        if total_gib <= boundary:
            return f"up to {boundary} GiB"
    return "more than 128 GiB"


def _gpu_summary() -> str:
    lspci = shutil.which("lspci")
    if not lspci:
        return "unavailable"
    try:
        completed = subprocess.run(
            [lspci], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, timeout=4, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return "unavailable"
    devices = []
    for line in completed.stdout.splitlines():
        if re.search(r"\b(VGA compatible controller|3D controller|Display controller):", line, re.I):
            line = re.sub(r"^[0-9a-f:.]+\s+", "", line, flags=re.I)
            devices.append(redact_text(line)[:300])
    return " | ".join(devices[:3]) or "unavailable"


def technical_report(*, app: str, use_x11: bool, manager_version: str,
                     runner_version: str) -> dict:
    return {
        "application": app,
        "manager_version": manager_version,
        "runner_version": runner_version,
        "display_protocol": "X11/XWayland" if use_x11 else "Wayland",
        "linux_release": _linux_release(),
        "kernel": platform.release(),
        "architecture": platform.machine() or "unknown",
        "cpu_cores": os.cpu_count(),
        "memory_class": _memory_class(),
        "gpu": _gpu_summary(),
    }


def _write_private(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    try:
        path.parent.chmod(0o700)
    except OSError:
        pass
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(content)
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def create_incident(*, kind: str, app: str, summary: str, trace: str,
                    use_x11: bool, manager_version: str, runner_version: str,
                    exit_code: int | None = None) -> Path:
    if kind not in {"crash", "hang"}:
        raise ValueError("Incident kind must be crash or hang.")
    incident_id = str(uuid.uuid4())
    root = incident_home()
    trace_path = root / f"{incident_id}.trace.txt"
    report_path = root / f"{incident_id}.json"
    encoded = redact_text(trace).encode("utf-8", errors="replace")[-MAX_TRACE_BYTES:]
    trace_text = encoded.decode("utf-8", errors="replace")
    _write_private(trace_path, trace_text)
    record = {
        "schema_version": 1,
        "incident_id": incident_id,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "status": "pending",
        "kind": kind,
        "summary": redact_text(str(summary))[:500],
        "exit_code": exit_code,
        "context": "",
        "technical": _redact_structure(technical_report(
            app=app, use_x11=use_x11, manager_version=manager_version,
            runner_version=runner_version,
        )),
        "trace_file": trace_path.name,
        "attachment": None,
    }
    _write_private(report_path, json.dumps(record, indent=2, sort_keys=True) + "\n")
    return report_path


def load_incident(path_value: str | os.PathLike[str]) -> tuple[Path, dict, str]:
    path = Path(path_value).expanduser().resolve()
    root = incident_home().resolve()
    if path.parent != root or not re.fullmatch(r"[0-9a-f-]{36}\.json", path.name):
        raise ValueError("Incident path is outside Wine4Office incident storage.")
    record = json.loads(path.read_text())
    if not isinstance(record, dict) or record.get("incident_id") != path.stem:
        raise ValueError("Incident record is invalid.")
    trace_name = record.get("trace_file")
    if not isinstance(trace_name, str) or Path(trace_name).name != trace_name:
        raise ValueError("Incident trace reference is invalid.")
    trace_path = path.parent / trace_name
    trace = trace_path.read_text(errors="replace") if trace_path.is_file() else ""
    return path, record, trace


def delete_incident(path_value: str | os.PathLike[str]) -> None:
    path, record, _trace = load_incident(path_value)
    trace = path.parent / str(record["trace_file"])
    trace.unlink(missing_ok=True)
    path.unlink(missing_ok=True)


def _attachment_metadata(path_value: str | os.PathLike[str] | None) -> dict | None:
    if not path_value:
        return None
    path = Path(path_value).expanduser().resolve()
    if not path.is_file():
        raise FileNotFoundError(f"The selected attachment is unavailable: {path}")
    if path.suffix.lower() not in ATTACHMENT_SUFFIXES:
        raise ValueError("Only Office documents, PDF, RTF, CSV or text files can be attached.")
    size = path.stat().st_size
    if size > MAX_ATTACHMENT_BYTES:
        raise ValueError("The selected attachment exceeds the 25 MiB limit.")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"name": path.name, "size": size, "sha256": digest.hexdigest(), "path": path}


def report_preview(record: dict, *, context: str, technical: dict, trace: str,
                   attachment: str | os.PathLike[str] | None = None) -> dict:
    if not isinstance(technical, dict):
        raise ValueError("Technical report data must be a JSON object.")
    metadata = _attachment_metadata(attachment)
    return {
        "schema_version": 1,
        "incident_id": record["incident_id"],
        "created_at": record["created_at"],
        "kind": record["kind"],
        "summary": redact_text(str(record["summary"])),
        "exit_code": record.get("exit_code"),
        "context": redact_text(str(context)),
        "technical": _redact_structure(technical),
        "trace": {
            "included": bool(trace),
            "bytes": len(trace.encode("utf-8", errors="replace")),
            "stream": "OPENOBSERVE_TRACE_STREAM" if trace else None,
        },
        "attachment": (
            {key: metadata[key] for key in ("name", "size", "sha256")}
            if metadata else None
        ),
    }


def _bulk_record(stream: str, document: dict) -> list[str]:
    return [
        json.dumps({"index": {"_index": stream}}, separators=(",", ":")),
        json.dumps(document, separators=(",", ":"), ensure_ascii=False),
    ]


def _tls_context() -> ssl.SSLContext:
    """Build a verified TLS context that also sees the host Linux CA store."""
    context = ssl.create_default_context()
    # Frozen Python builds can prefer a bundled CA file and overlook a CA or
    # intermediate installed by the distribution. Loading these augments the
    # trust store; certificate and hostname verification remain enabled.
    for candidate in (
        "/etc/ssl/certs/ca-certificates.crt",  # Debian, Ubuntu, Arch
        "/etc/ssl/cert.pem",                  # Alpine, openSUSE
        "/etc/pki/tls/certs/ca-bundle.crt",   # Fedora, RHEL
        "/etc/ssl/ca-bundle.pem",             # openSUSE fallback
    ):
        if Path(candidate).is_file():
            try:
                context.load_verify_locations(cafile=candidate)
            except (OSError, ssl.SSLError):
                continue
    return context


class _SafeRedirectHandler(urllib.request.HTTPRedirectHandler):
    @staticmethod
    def _origin(value: str) -> tuple[str, str, int] | None:
        parsed = urllib.parse.urlsplit(value)
        if parsed.scheme.lower() not in {"http", "https"}:
            return None
        if not parsed.hostname or parsed.username or parsed.password:
            return None
        try:
            port = parsed.port
        except ValueError:
            return None
        if port is None:
            port = 443 if parsed.scheme.lower() == "https" else 80
        return parsed.scheme.lower(), parsed.hostname.rstrip(".").casefold(), port

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        target = urllib.parse.urljoin(req.full_url, newurl.replace(" ", "%20"))
        source_origin = self._origin(req.full_url)
        target_origin = self._origin(target)
        source_scheme = urllib.parse.urlsplit(req.full_url).scheme.lower()
        target_scheme = urllib.parse.urlsplit(target).scheme.lower()
        if (source_origin is None or target_origin is None
                or source_origin != target_origin
                or (source_scheme == "https" and target_scheme == "http")):
            raise urllib.error.HTTPError(
                target, code, "Unsafe report redirect refused.", headers, fp,
            )
        return super().redirect_request(req, fp, code, msg, headers, target)


def submit_incident(path_value: str | os.PathLike[str], *, context: str,
                    technical: dict, trace: str,
                    attachment: str | os.PathLike[str] | None = None,
                    environ: dict[str, str] | None = None,
                    timeout: float = 30) -> dict:
    settings = openobserve_environment(environ)
    if settings is None:
        raise RuntimeError("Incident reporting is unavailable in this build.")
    path, record, _stored_trace = load_incident(path_value)
    trace_bytes = redact_text(trace).encode("utf-8", errors="replace")
    if len(trace_bytes) > MAX_TRACE_BYTES:
        raise ValueError("The trace exceeds the 50 MiB limit.")
    metadata = _attachment_metadata(attachment)
    preview = report_preview(
        record, context=context, technical=technical,
        trace=trace_bytes.decode("utf-8", errors="replace"), attachment=attachment,
    )
    lines = _bulk_record(settings["incident_stream"], preview)
    if trace_bytes:
        trace_id = f"{record['incident_id']}-trace"
        for index in range(0, len(trace_bytes), 128 * 1024):
            lines.extend(_bulk_record(settings["trace_stream"], {
                "incident_id": record["incident_id"],
                "trace_id": trace_id,
                "chunk": index // (128 * 1024),
                "text": trace_bytes[index:index + 128 * 1024].decode("utf-8", errors="replace"),
            }))
    if metadata:
        attachment_path = metadata.pop("path")
        with attachment_path.open("rb") as source:
            chunk_number = 0
            while True:
                chunk = source.read(512 * 1024)
                if not chunk:
                    break
                lines.extend(_bulk_record(settings["artifact_stream"], {
                    "incident_id": record["incident_id"],
                    "artifact": metadata,
                    "chunk": chunk_number,
                    "base64": base64.b64encode(chunk).decode("ascii"),
                }))
                chunk_number += 1
    payload = ("\n".join(lines) + "\n").encode("utf-8")
    endpoint = (
        f"{settings['url']}/api/{urllib.parse.quote(settings['org'], safe='')}/_bulk"
    )
    credentials = base64.b64encode(
        f"{settings['account']}:{settings['token']}".encode("utf-8")
    ).decode("ascii")
    request = urllib.request.Request(
        endpoint, data=payload, method="POST",
        headers={
            "Authorization": f"Basic {credentials}",
            "Content-Type": "application/x-ndjson",
            "User-Agent": "Wine4Office-Manager/incident-report",
        },
    )
    opener = urllib.request.build_opener(
        _SafeRedirectHandler(),
        urllib.request.HTTPSHandler(context=_tls_context()),
    )
    with opener.open(request, timeout=timeout) as response:
        response_data = json.loads(response.read().decode("utf-8"))
        status = response.status
    if status != 200 or response_data.get("errors") is not False:
        raise RuntimeError("OpenObserve rejected one or more report records.")
    record["status"] = "sent"
    record["summary"] = preview["summary"]
    record["sent_at"] = datetime.now(timezone.utc).isoformat()
    record["context"] = redact_text(str(context))
    record["technical"] = _redact_structure(technical)
    record["attachment"] = preview["attachment"]
    _write_private(path, json.dumps(record, indent=2, sort_keys=True) + "\n")
    return {
        "incident_id": record["incident_id"],
        "items": len(response_data.get("items", [])),
    }


class RollingTrace:
    def __init__(self, maximum: int = MAX_RING_BYTES) -> None:
        self.maximum = maximum
        self._data = bytearray()
        self._lock = threading.Lock()

    def append(self, value: bytes) -> None:
        if not value:
            return
        with self._lock:
            self._data.extend(value)
            overflow = len(self._data) - self.maximum
            if overflow > 0:
                del self._data[:overflow]

    def text(self) -> str:
        with self._lock:
            data = bytes(self._data)
        return redact_text(data.decode("utf-8", errors="replace"))


def _read_trace(source, ring: RollingTrace) -> None:
    if source is None:
        return
    try:
        while True:
            chunk = source.read(64 * 1024)
            if not chunk:
                break
            ring.append(chunk if isinstance(chunk, bytes) else str(chunk).encode())
    except (OSError, ValueError):
        pass


def _process_candidates(root_pid: int, prefix: Path, app: str) -> set[int]:
    candidates = {root_pid}
    parent_map: dict[int, int] = {}
    proc = Path("/proc")
    executable = APP_EXECUTABLES.get(app, "")
    try:
        entries = list(proc.iterdir())
    except OSError:
        return candidates
    for entry in entries:
        if not entry.name.isdigit():
            continue
        pid = int(entry.name)
        try:
            status = (entry / "status").read_text(errors="replace")
            parent_match = re.search(r"^PPid:\s+(\d+)", status, re.M)
            if parent_match:
                parent_map[pid] = int(parent_match.group(1))
            if executable:
                environment = (entry / "environ").read_bytes().split(b"\0")
                prefix_entry = f"WINEPREFIX={prefix}".encode()
                command = (entry / "cmdline").read_bytes().replace(b"\0", b" ").lower()
                if prefix_entry in environment and executable.encode() in command:
                    candidates.add(pid)
        except (OSError, UnicodeError):
            continue
    changed = True
    while changed:
        changed = False
        for pid, parent in parent_map.items():
            if parent in candidates and pid not in candidates:
                candidates.add(pid)
                changed = True
    return candidates


def process_snapshot(root_pid: int, prefix: Path, app: str) -> str:
    lines = ["Bounded Wine4Office process snapshot"]
    for pid in sorted(_process_candidates(root_pid, prefix, app))[:64]:
        path = Path("/proc") / str(pid)
        try:
            status = path.joinpath("status").read_text(errors="replace")
        except OSError:
            continue
        selected = []
        for key in ("Name", "State", "Threads", "VmRSS"):
            match = re.search(rf"^{key}:\s*(.+)$", status, re.M)
            if match:
                selected.append(f"{key}={match.group(1).strip()}")
        try:
            selected.append(f"wchan={path.joinpath('wchan').read_text().strip()[:120]}")
        except OSError:
            pass
        lines.append(f"pid={pid} " + " ".join(selected))
    return redact_text("\n".join(lines))


class _XClientData(ctypes.Union):
    _fields_ = [
        ("b", ctypes.c_char * 20),
        ("s", ctypes.c_short * 10),
        ("l", ctypes.c_long * 5),
    ]


class _XClientMessage(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("serial", ctypes.c_ulong),
        ("send_event", ctypes.c_int),
        ("display", ctypes.c_void_p),
        ("window", ctypes.c_ulong),
        ("message_type", ctypes.c_ulong),
        ("format", ctypes.c_int),
        ("data", _XClientData),
    ]


class _XEvent(ctypes.Union):
    _fields_ = [("type", ctypes.c_int), ("xclient", _XClientMessage),
                ("pad", ctypes.c_long * 24)]


class X11WindowProbe:
    """Probe Wine's EWMH ping support without blocking the Office process."""

    CLIENT_MESSAGE = 33
    SUBSTRUCTURE_NOTIFY_MASK = 1 << 19

    def __init__(self, root_pid: int, prefix: Path, app: str,
                 display_name: str | None = None) -> None:
        library_name = ctypes.util.find_library("X11")
        if not library_name:
            raise RuntimeError("libX11 is unavailable.")
        self.lib = ctypes.CDLL(library_name)
        self._configure()
        encoded = (display_name or os.environ.get("DISPLAY", "")).encode() or None
        self.display = self.lib.XOpenDisplay(encoded)
        if not self.display:
            raise RuntimeError("The X11 display is unavailable.")
        self.root = self.lib.XDefaultRootWindow(self.display)
        self.root_pid = root_pid
        self.prefix = prefix
        self.app = app
        self.wm_protocols = self._atom("WM_PROTOCOLS")
        self.net_wm_ping = self._atom("_NET_WM_PING")
        self.net_wm_pid = self._atom("_NET_WM_PID")
        self.net_client_list = self._atom("_NET_CLIENT_LIST")
        self.lib.XSelectInput(self.display, self.root, self.SUBSTRUCTURE_NOTIFY_MASK)
        self.lib.XSync(self.display, 0)

    def _configure(self) -> None:
        lib = self.lib
        lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        lib.XOpenDisplay.restype = ctypes.c_void_p
        lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        lib.XDefaultRootWindow.restype = ctypes.c_ulong
        lib.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        lib.XInternAtom.restype = ctypes.c_ulong
        lib.XGetWindowProperty.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_long,
            ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
        ]
        lib.XGetWindowProperty.restype = ctypes.c_int
        lib.XGetWMProtocols.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_int),
        ]
        lib.XGetWMProtocols.restype = ctypes.c_int
        lib.XFree.argtypes = [ctypes.c_void_p]
        lib.XSelectInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_long]
        lib.XSendEvent.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long,
            ctypes.POINTER(_XEvent),
        ]
        lib.XFlush.argtypes = [ctypes.c_void_p]
        lib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.XPending.argtypes = [ctypes.c_void_p]
        lib.XPending.restype = ctypes.c_int
        lib.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.POINTER(_XEvent)]
        lib.XConnectionNumber.argtypes = [ctypes.c_void_p]
        lib.XConnectionNumber.restype = ctypes.c_int

    def _atom(self, name: str) -> int:
        return int(self.lib.XInternAtom(self.display, name.encode(), 0))

    def _property_longs(self, window: int, atom: int) -> list[int]:
        actual_type = ctypes.c_ulong()
        actual_format = ctypes.c_int()
        count = ctypes.c_ulong()
        remaining = ctypes.c_ulong()
        data = ctypes.POINTER(ctypes.c_ubyte)()
        status = self.lib.XGetWindowProperty(
            self.display, window, atom, 0, 4096, 0, 0,
            ctypes.byref(actual_type), ctypes.byref(actual_format),
            ctypes.byref(count), ctypes.byref(remaining), ctypes.byref(data),
        )
        if status != 0 or not data or actual_format.value != 32:
            if data:
                self.lib.XFree(data)
            return []
        try:
            values = ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))
            return [int(values[index]) for index in range(count.value)]
        finally:
            self.lib.XFree(data)

    def _supports_ping(self, window: int) -> bool:
        protocols = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_int()
        if not self.lib.XGetWMProtocols(
                self.display, window, ctypes.byref(protocols), ctypes.byref(count)):
            return False
        try:
            return any(int(protocols[index]) == self.net_wm_ping for index in range(count.value))
        finally:
            if protocols:
                self.lib.XFree(protocols)

    def _windows(self) -> list[int]:
        pids = _process_candidates(self.root_pid, self.prefix, self.app)
        result = []
        for window in self._property_longs(self.root, self.net_client_list):
            values = self._property_longs(window, self.net_wm_pid)
            if values and values[0] in pids and self._supports_ping(window):
                result.append(window)
        return result

    def ping(self, timeout: float) -> bool | None:
        windows = self._windows()
        if not windows:
            return None
        while self.lib.XPending(self.display):
            event = _XEvent()
            self.lib.XNextEvent(self.display, ctypes.byref(event))
        nonce = int(time.monotonic_ns() // 1_000_000) & 0x7fffffff
        for window in windows:
            event = _XEvent()
            event.xclient.type = self.CLIENT_MESSAGE
            event.xclient.send_event = 1
            event.xclient.display = self.display
            event.xclient.window = window
            event.xclient.message_type = self.wm_protocols
            event.xclient.format = 32
            event.xclient.data.l[0] = self.net_wm_ping
            event.xclient.data.l[1] = nonce
            event.xclient.data.l[2] = window
            self.lib.XSendEvent(self.display, window, 0, 0, ctypes.byref(event))
        self.lib.XFlush(self.display)
        descriptor = self.lib.XConnectionNumber(self.display)
        deadline = time.monotonic() + max(0.05, timeout)
        while time.monotonic() < deadline:
            wait = max(0, deadline - time.monotonic())
            ready, _write, _error = select.select([descriptor], [], [], wait)
            if not ready:
                break
            while self.lib.XPending(self.display):
                event = _XEvent()
                self.lib.XNextEvent(self.display, ctypes.byref(event))
                if (event.type == self.CLIENT_MESSAGE
                        and int(event.xclient.message_type) == self.wm_protocols
                        and int(event.xclient.data.l[0]) == self.net_wm_ping
                        and int(event.xclient.data.l[1]) == nonce):
                    return True
        return False

    def close(self) -> None:
        if self.display:
            self.lib.XCloseDisplay(self.display)
            self.display = None


def _notify(path: Path, review_command: Iterable[str] | None, kind: str) -> None:
    notifier = shutil.which("notify-send")
    if not notifier:
        return
    safe_env = clean_reporting_environment()
    command = [
        notifier, "--app-name=Wine4Office", "--urgency=normal",
        "--action=review=Review report",
        "Wine4Office stopped responding" if kind == "hang" else "Wine4Office app closed unexpectedly",
        "A local diagnostic report is ready. Choose whether you want to report and add context.",
    ]
    try:
        completed = subprocess.run(
            command, env=safe_env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, timeout=300, check=False,
        )
        if completed.stdout.strip() == "review" and review_command:
            subprocess.Popen(
                [*review_command, "--review-incident", str(path)],
                env=safe_env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, start_new_session=True,
            )
    except (OSError, subprocess.SubprocessError):
        try:
            subprocess.run(
                [notifier, "Wine4Office issue detected",
                 "A local diagnostic report is ready in Wine4Office Manager."],
                env=safe_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=10, check=False,
            )
        except (OSError, subprocess.SubprocessError):
            pass


def _terminate_and_wait(process: subprocess.Popen) -> None:
    """Stop a detached child and reap it without masking the original failure."""
    try:
        process.terminate()
    except BaseException:
        pass
    try:
        process.wait(timeout=5)
        return
    except TypeError:
        try:
            process.wait()
        except BaseException:
            pass
        return
    except BaseException:
        pass
    try:
        process.kill()
    except BaseException:
        pass
    try:
        process.wait()
    except BaseException:
        pass


def supervise_process(process: subprocess.Popen, *, app: str, prefix: Path,
                      use_x11: bool, manager_version: str, runner_version: str,
                      review_command: Iterable[str] | None = None,
                      probe_factory: Callable[[int, Path, str], object] | None = None,
                      hang_interval: float | None = None,
                      hang_timeout: float | None = None,
                      hang_failures: int | None = None) -> int:
    ring = None
    reader = None
    reader_started = False
    probe = None
    incident_path = None
    try:
        ring = RollingTrace()
        reader = threading.Thread(
            target=_read_trace, args=(process.stdout, ring),
            name=f"wine4office-{app}-trace", daemon=True,
        )
        reader.start()
        reader_started = True
        interval = hang_interval if hang_interval is not None else float(
            os.environ.get("WINE4OFFICE_HANG_INTERVAL_SECONDS", "5")
        )
        timeout = hang_timeout if hang_timeout is not None else float(
            os.environ.get("WINE4OFFICE_HANG_TIMEOUT_SECONDS", "3")
        )
        threshold = hang_failures if hang_failures is not None else int(
            os.environ.get("WINE4OFFICE_HANG_FAILURES", "3")
        )
        if not math.isfinite(interval) or not math.isfinite(timeout):
            raise ValueError("Hang monitoring settings must be finite numbers.")
        if use_x11 and os.environ.get("DISPLAY"):
            try:
                factory = probe_factory or X11WindowProbe
                probe = factory(process.pid, prefix, app)
            except (OSError, RuntimeError, ValueError):
                probe = None
        next_probe = time.monotonic() + max(0.05, interval)
        failures = 0
        while process.poll() is None:
            now = time.monotonic()
            if probe is not None and now >= next_probe and incident_path is None:
                responsive = probe.ping(timeout)
                if responsive is False:
                    failures += 1
                elif responsive is True:
                    failures = 0
                if failures >= max(1, threshold):
                    snapshot = process_snapshot(process.pid, prefix, app)
                    trace = ring.text()
                    if trace:
                        snapshot += "\n\nBounded Wine exception trace\n" + trace
                    incident_path = create_incident(
                        kind="hang", app=app,
                        summary=f"{app.title()} stopped responding to X11 window pings.",
                        trace=snapshot, use_x11=use_x11,
                        manager_version=manager_version, runner_version=runner_version,
                    )
                    threading.Thread(
                        target=_notify, args=(incident_path, review_command, "hang"),
                        name="wine4office-incident-notification", daemon=True,
                    ).start()
                next_probe = now + max(0.05, interval)
            time.sleep(0.05)
        return_code = int(process.wait())
    except BaseException:
        _terminate_and_wait(process)
        raise
    finally:
        if probe is not None:
            try:
                probe.close()
            except BaseException:
                pass
        if reader_started and reader is not None:
            reader.join(timeout=2)
    trace = ring.text()
    crash_markers = re.search(r"(?i)(unhandled exception|wine:.*fault|segmentation fault)", trace)
    if incident_path is None and (return_code != 0 or crash_markers):
        incident_path = create_incident(
            kind="crash", app=app,
            summary=f"{app.title()} exited unexpectedly.", trace=trace,
            use_x11=use_x11, manager_version=manager_version,
            runner_version=runner_version, exit_code=return_code,
        )
        threading.Thread(
            target=_notify, args=(incident_path, review_command, "crash"),
            name="wine4office-incident-notification", daemon=True,
        ).start()
    return return_code
