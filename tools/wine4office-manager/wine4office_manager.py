#!/usr/bin/env python3
"""Native Qt entry point and task state for Wine4OfficeManager."""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import wine4office_backend as backend  # noqa: E402


def install_root() -> Path:
    configured = os.environ.get("WINE4OFFICE_MANAGER_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    if HERE.name == "lib":
        return HERE.parent
    return HERE


INSTALL_ROOT = install_root()
FROZEN = bool(getattr(sys, "frozen", False))
LAUNCHER = (Path(sys.executable).resolve() if FROZEN else
            INSTALL_ROOT / "bin/wine4office-launcher" if INSTALL_ROOT != HERE
            else HERE / "wine4office-launcher")
ICONS = INSTALL_ROOT / "icons" if INSTALL_ROOT != HERE else HERE / "icons"
FONT_HELPER = (INSTALL_ROOT / "lib/register-office-cloud-fonts.sh"
               if INSTALL_ROOT != HERE else HERE / "register-office-cloud-fonts.sh")


class ManagerState:
    """Thread-safe bridge between the Qt event loop and blocking backend work."""

    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.config = backend.load_config()
        self.task = {"running": False, "kind": "", "status": "idle", "log": ""}
        self.cancel_event = threading.Event()
        self.process: subprocess.Popen | None = None
        self.updater = {
            "checking": False,
            "checked": False,
            "offer": None,
            "error": "",
        }

    def _candidate_config(self, payload: dict) -> dict:
        candidate = dict(self.config)
        for key in ("prefix", "wine", "update_url"):
            if key in payload:
                candidate[key] = str(payload[key]).strip()
        if "desktop_copy" in payload:
            candidate["desktop_copy"] = bool(payload["desktop_copy"])
        return candidate

    def configured_prefix(self) -> str:
        with self.lock:
            return str(self.config["prefix"])

    def update_config(self, payload: dict) -> dict:
        with self.lock:
            candidate = self._candidate_config(payload)
            if not backend.paths_equivalent(self.config["prefix"], candidate["prefix"]):
                raise ValueError(
                    "The Wine environment path must be switched through a validated transition."
                )
            backend.save_config(candidate)
            self.config = candidate
            return dict(self.config)

    def start_environment_transition(self, payload: dict, initialize: bool,
                                     delete_old: bool) -> None:
        with self.lock:
            original = dict(self.config)
            candidate = self._candidate_config(payload)
            old_prefix = str(original["prefix"])
            new_prefix = str(candidate["prefix"])
            if backend.paths_equivalent(old_prefix, new_prefix):
                raise ValueError("The Wine environment path has not changed.")
            target_kind = backend.classify_prefix(new_prefix)
            if target_kind == "unsafe":
                raise ValueError(
                    "The selected target is nonempty and is not a valid Wine prefix: "
                    f"{backend.validate_prefix(new_prefix)}"
                )
            if target_kind in ("missing", "empty") and not initialize:
                raise ValueError("The selected Wine environment must be initialized before switching.")
            if target_kind == "valid" and initialize:
                raise ValueError("An existing valid Wine prefix must not be initialized again.")
            if delete_old:
                backend.validate_environment_deletion(old_prefix, new_prefix)

        def transition() -> str:
            initialized = False
            staged: Path | None = None
            committed = False
            try:
                if initialize:
                    backend.create_environment(
                        new_prefix, candidate["wine"], False, self.output,
                        self.cancel_event, self.set_process,
                    )
                    initialized = True
                if backend.classify_prefix(new_prefix) != "valid":
                    raise ValueError(
                        f"The replacement is not a valid Wine prefix: {new_prefix}"
                    )

                if self.cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")
                if delete_old:
                    staged = backend.stage_environment_deletion(
                        old_prefix, new_prefix, original["wine"], self.output
                    )
                if self.cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")

                with self.lock:
                    if not backend.paths_equivalent(self.config["prefix"], old_prefix):
                        raise RuntimeError("The configured environment changed during the transition.")
                    committed_candidate = self._candidate_config(payload)
                    backend.save_config(committed_candidate)
                    self.config = committed_candidate
                    committed = True
            except Exception:
                if staged and staged.exists():
                    backend.restore_staged_environment(staged, old_prefix)
                if initialized and not committed:
                    try:
                        backend.discard_initialized_environment(new_prefix, self.output)
                    except (OSError, ValueError) as cleanup_error:
                        self.output(f"WARNING: Could not discard uncommitted environment: {cleanup_error}")
                raise

            if staged and staged.exists():
                try:
                    backend.finish_staged_environment_deletion(staged, self.output)
                except OSError as error:
                    self.output(f"WARNING: Could not remove staged old environment: {error}")
            return f"Wine environment switched to {new_prefix}"

        self.start_task("environment-switch", transition)

    def start_update_check(self) -> bool:
        """Check configured metadata off the UI thread without downloading artifacts."""
        with self.lock:
            if self.updater["checking"]:
                return False
            metadata_url = str(self.config["update_url"]).strip()
            if not metadata_url:
                return False
            skipped = dict(self.config.get("skipped_updates", {}))
            self.updater = {
                "checking": True,
                "checked": False,
                "offer": None,
                "error": "",
            }

        def check() -> None:
            try:
                result = backend.check_for_updates(metadata_url, skipped)
                canonical_url = result["metadata"]["metadata_url"]
                with self.lock:
                    candidate = dict(self.config)
                    candidate["update_url"] = canonical_url
                    backend.save_config(candidate)
                    backend.persist_metadata_url(canonical_url)
                    self.config = candidate
                    updates = result["updates"]
                    offer_id = "|".join(
                        f"{name}:{updates[name]['version']}" for name in sorted(updates)
                    )
                    self.updater = {
                        "checking": False,
                        "checked": True,
                        "offer": {
                            "id": offer_id,
                            "metadata": result["metadata"],
                            "updates": updates,
                        } if updates else None,
                        "error": "",
                    }
            except Exception as error:
                with self.lock:
                    self.updater = {
                        "checking": False,
                        "checked": True,
                        "offer": None,
                        "error": str(error),
                    }

        threading.Thread(
            target=check, name="wine4office-update-check", daemon=True
        ).start()
        return True

    def skip_offered_updates(self, components: list[str]) -> None:
        with self.lock:
            offer = self.updater.get("offer")
            if not offer:
                return
            skipped = dict(self.config.get("skipped_updates", {}))
            for name in components:
                if name in offer["updates"]:
                    skipped[name] = offer["updates"][name]["version"]
            candidate = dict(self.config)
            candidate["skipped_updates"] = skipped
            backend.save_config(candidate)
            self.config = candidate
            remaining = {
                name: component for name, component in offer["updates"].items()
                if name not in components
            }
            self.updater["offer"] = (
                {**offer, "updates": remaining} if remaining else None
            )

    def start_offered_update(self, components: list[str]) -> None:
        """Consent gate: this is the only state hook that downloads artifacts."""
        with self.lock:
            offer = self.updater.get("offer")
            if not offer:
                raise RuntimeError("No verified update is currently offered.")
            selected = list(dict.fromkeys(components))
            if not selected or any(name not in offer["updates"] for name in selected):
                raise ValueError("Select at least one offered update.")
            metadata = offer["metadata"]
            config = dict(self.config)

        def install() -> str:
            if "wine" in selected:
                try:
                    backend.stop_wine(config["prefix"], config["wine"])
                    self.output("Stopped the selected Wine environment before updating.")
                except (FileNotFoundError, OSError):
                    pass
            result = backend.install_release_updates(
                metadata, selected, self.output, self.cancel_event
            )
            if "wine" in selected:
                with self.lock:
                    candidate = dict(self.config)
                    candidate["wine"] = str(backend.runner_update_target() / "bin/wine")
                    backend.save_config(candidate)
                    self.config = candidate
            return result

        self.start_task("update", install)
        with self.lock:
            self.updater["offer"] = None

    def snapshot(self) -> dict:
        with self.lock:
            config = dict(self.config)
            config["skipped_updates"] = dict(config.get("skipped_updates", {}))
            task = dict(self.task)
            updater = dict(self.updater)
            if updater.get("offer"):
                updater["offer"] = {
                    **updater["offer"],
                    "metadata": dict(updater["offer"]["metadata"]),
                    "updates": dict(updater["offer"]["updates"]),
                }
        return {
            "config": config,
            "status": backend.environment_status(config["prefix"], config["wine"]),
            "version": backend.current_version(),
            "wine_version": backend.current_wine_version(),
            "task": task,
            "updater": updater,
        }

    def output(self, line: str) -> None:
        with self.lock:
            text = self.task["log"] + line + "\n"
            self.task["log"] = text[-1_000_000:]

    def set_process(self, process: subprocess.Popen | None) -> None:
        with self.lock:
            self.process = process

    def start_task(self, kind: str, operation) -> None:
        with self.lock:
            if self.task["running"]:
                raise RuntimeError("Another operation is already running.")
            self.task = {"running": True, "kind": kind, "status": "running", "log": ""}
            self.cancel_event.clear()

        def worker() -> None:
            try:
                result = operation()
                with self.lock:
                    self.task["status"] = "completed"
                    if result:
                        self.output(str(result))
            except Exception as error:  # surfaced verbatim in the native operation log
                with self.lock:
                    self.task["status"] = "cancelled" if self.cancel_event.is_set() else "failed"
                    self.output(f"ERROR: {error}")
            finally:
                with self.lock:
                    self.task["running"] = False
                    self.process = None

        threading.Thread(target=worker, name=f"wine4office-{kind}", daemon=True).start()

    def cancel(self) -> None:
        self.cancel_event.set()
        with self.lock:
            process = self.process
        if process and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Wine4OfficeManager")
    parser.add_argument("--install-shortcut", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--smoke-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--screenshot", metavar="PATH", help=argparse.SUPPRESS)
    parser.add_argument("--no-browser", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--prefix", help=argparse.SUPPRESS)
    parser.add_argument("--wine", help=argparse.SUPPRESS)
    parser.add_argument("target", nargs="?", choices=[*backend.APP_META, *backend.TOOL_META, "stop"],
                        help=argparse.SUPPRESS)
    parser.add_argument("documents", nargs="*", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.target:
        defaults = backend.load_config()
        prefix = args.prefix or defaults["prefix"]
        wine = args.wine or defaults["wine"]
        if args.target in backend.APP_META:
            backend.launch_app(prefix, wine, args.target, FONT_HELPER, args.documents)
        else:
            if args.documents:
                parser.error("Wine tools do not accept document arguments.")
            backend.launch_tool(prefix, wine, args.target)
        return 0

    if args.install_shortcut:
        path = backend.install_manager_shortcut(
            LAUNCHER if FROZEN else LAUNCHER.with_name("wine4office-manager"), ICONS
        )
        print(path)
        return 0

    try:
        from wine4office_qt import run_manager
    except ImportError as error:
        if error.name and error.name.startswith("PySide6"):
            parser.error(
                "PySide6 is required for the native Qt interface. Install the packaged Wine4Office "
                "release or run: python3 -m pip install --user PySide6"
            )
        raise

    return run_manager(
        ManagerState(),
        launcher=LAUNCHER,
        icons=ICONS,
        font_helper=FONT_HELPER,
        smoke_test=args.smoke_test,
        screenshot=Path(args.screenshot).expanduser() if args.screenshot else None,
    )


if __name__ == "__main__":
    raise SystemExit(main())
