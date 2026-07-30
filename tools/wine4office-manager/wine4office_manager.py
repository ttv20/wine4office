#!/usr/bin/env python3
"""Native Qt entry point and task state for Wine4Office Manager."""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import wine4office_backend as backend  # noqa: E402
import wine4office_post_install as post_install  # noqa: E402


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


def manager_restart_command() -> list[str]:
    if FROZEN:
        return [str(Path(sys.executable).resolve())]
    if INSTALL_ROOT != HERE:
        return [str(INSTALL_ROOT / "bin/wine4office-manager")]
    return [str(Path(sys.executable).resolve()), str(HERE / "wine4office_manager.py")]


MANAGER_RESTART_COMMAND = manager_restart_command()


class ManagerState:
    """Thread-safe bridge between the Qt event loop and blocking backend work."""

    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.config = backend.load_config()
        self.task = {
            "running": False, "kind": "", "status": "idle", "log": "",
            "restart_required": False,
        }
        self.cancel_event = threading.Event()
        self.process: subprocess.Popen | None = None
        self.updater = {
            "checking": False,
            "checked": False,
            "offer": None,
            "error": "",
        }
        self.preload = {
            "supported": False,
            "reason": "",
            "installed": False,
            "enabled": False,
            "active": False,
            "state": "checking",
            "binding": None,
            "selected_matches": False,
            "components": {},
            "detail": "Checking per-user service support.",
            "checking": False,
        }
        self._preload_generation = 0
        self._preload_request_key = None
        self._preload_checked_at = 0.0
        self._refresh_preload_status_async()

    def _candidate_config(self, payload: dict) -> dict:
        candidate = dict(self.config)
        saved_policies = candidate.get("office_telemetry_disabled", {})
        candidate["office_telemetry_disabled"] = (
            dict(saved_policies) if isinstance(saved_policies, dict) else {}
        )
        for key in ("prefix", "wine", "update_url"):
            if key in payload:
                candidate[key] = str(payload[key]).strip()
        for key in ("desktop_copy", "use_x11"):
            if key in payload:
                candidate[key] = bool(payload[key])
        if "disable_office_telemetry" in payload:
            candidate = backend.set_office_telemetry_disabled(
                candidate, candidate["prefix"], bool(payload["disable_office_telemetry"])
            )
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
            policy_was_disabled = backend.office_telemetry_disabled(
                self.config, candidate["prefix"]
            )
            policy_disabled = backend.office_telemetry_disabled(candidate)
            policy_applied = False
            if ((policy_disabled or policy_was_disabled)
                    and backend.classify_prefix(candidate["prefix"]) == "valid"):
                backend.apply_office_telemetry_policy(
                    candidate["prefix"], candidate["wine"], policy_disabled,
                    remove_managed=policy_was_disabled,
                    use_x11=candidate.get("use_x11", True),
                )
                policy_applied = True
            try:
                backend.save_config(candidate)
            except Exception:
                if policy_applied and policy_disabled != policy_was_disabled:
                    backend.apply_office_telemetry_policy(
                        candidate["prefix"], candidate["wine"], policy_was_disabled,
                        remove_managed=policy_disabled,
                        use_x11=candidate.get("use_x11", True),
                    )
                raise
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
            policy_was_disabled = backend.office_telemetry_disabled(original, new_prefix)
            policy_disabled = backend.office_telemetry_disabled(candidate, new_prefix)


        def transition() -> str:
            initialized = False
            staged: Path | None = None
            committed = False
            policy_applied = False
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

                if policy_disabled or policy_was_disabled:
                    backend.apply_office_telemetry_policy(
                        new_prefix, candidate["wine"], policy_disabled,
                        remove_managed=policy_was_disabled,
                        use_x11=candidate.get("use_x11", True),
                    )
                    policy_applied = True
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
                    if delete_old:
                        committed_candidate = backend.set_office_telemetry_disabled(
                            committed_candidate, old_prefix, False
                        )
                    backend.save_config(committed_candidate)
                    self.config = committed_candidate
                    committed = True
            except Exception:
                if staged and staged.exists():
                    backend.restore_staged_environment(staged, old_prefix)
                if policy_applied and not initialized and policy_disabled != policy_was_disabled:
                    try:
                        backend.apply_office_telemetry_policy(
                            new_prefix, candidate["wine"], policy_was_disabled,
                            remove_managed=policy_disabled,
                            use_x11=candidate.get("use_x11", True),
                        )
                    except Exception as rollback_error:
                        self.output(
                            "WARNING: Could not restore the replacement environment's "
                            f"telemetry policy: {rollback_error}"
                        )
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
                    backend.stop_wine(
                        config["prefix"], config["wine"],
                        use_x11=config.get("use_x11", True),
                    )
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
                    config.update(candidate)
            if "manager" in selected:
                with self.lock:
                    self.task["restart_required"] = True
                self._run_updated_manager_post_install(config)
            return result

        self.start_task("update", install)
        with self.lock:
            self.updater["offer"] = None

    def _run_updated_manager_post_install(self, config: dict) -> None:
        target = backend.manager_update_target()
        if target is None or not target.is_file() or not os.access(target, os.X_OK):
            raise FileNotFoundError(
                "The updated Wine4Office Manager executable is unavailable."
            )
        command = [
            str(target), "--post-update",
            "--prefix", str(config["prefix"]),
            "--wine", str(config["wine"]),
        ]
        completed = subprocess.run(
            command,
            env=os.environ.copy(),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=180,
            check=False,
        )
        for line in completed.stdout.splitlines():
            self.output(line)
        if completed.returncode:
            raise RuntimeError(
                f"The new manager post-install hook exited with status "
                f"{completed.returncode}. Restart the manager to retry it."
            )

    def _preload_status(self, config: dict) -> dict:
        status = backend.preload_service_status(
            config["prefix"], config["wine"], config.get("use_x11", True)
        )
        status["checking"] = False
        return status

    def _refresh_preload_status_async(self, force: bool = False) -> bool:
        with self.lock:
            config = dict(self.config)
            key = (
                str(config["prefix"]),
                str(config["wine"]),
                bool(config.get("use_x11", True)),
            )
            now = time.monotonic()
            if not force:
                if self.preload.get("checking") and self._preload_request_key == key:
                    return False
                if (
                    self._preload_request_key == key
                    and now - self._preload_checked_at < 5
                ):
                    return False
            self._preload_generation += 1
            generation = self._preload_generation
            self._preload_request_key = key
            self.preload["checking"] = True

        def check() -> None:
            try:
                status = self._preload_status(config)
            except Exception as error:
                status = {
                    "supported": False,
                    "reason": str(error),
                    "installed": False,
                    "enabled": False,
                    "active": False,
                    "state": "unsupported",
                    "binding": None,
                    "selected_matches": False,
                    "components": {},
                    "detail": str(error),
                    "checking": False,
                }
            with self.lock:
                if generation == self._preload_generation:
                    self.preload = status
                    self._preload_checked_at = time.monotonic()

        threading.Thread(
            target=check, name="wine4office-preload-status", daemon=True
        ).start()
        return True

    def start_preload_action(self, action: str) -> None:
        if action not in {"enable", "disable", "start", "stop"}:
            raise ValueError(f"Unknown preload service action: {action}")
        with self.lock:
            if self.task["running"]:
                raise RuntimeError("Another operation is already running.")
            config = dict(self.config)
            self.preload["checking"] = True
            self._preload_generation += 1
            generation = self._preload_generation
            self._preload_request_key = (
                str(config["prefix"]),
                str(config["wine"]),
                bool(config.get("use_x11", True)),
            )

        def operation() -> str:
            try:
                if action == "enable":
                    backend.install_preload_service(
                        config["prefix"], config["wine"], config.get("use_x11", True)
                    )
                else:
                    backend.manage_preload_service(
                        action, config["prefix"], config["wine"],
                        config.get("use_x11", True),
                    )
                return {
                    "enable": "Preload enabled for login; it was not started.",
                    "disable": "Preload disabled for login; a running service was not stopped.",
                    "start": "Preload service started without enabling login startup.",
                    "stop": "Preload service stopped without disabling login startup.",
                }[action]
            finally:
                try:
                    status = self._preload_status(config)
                except Exception as error:
                    status = dict(self.preload)
                    status.update({
                        "reason": str(error),
                        "detail": str(error),
                        "checking": False,
                    })
                with self.lock:
                    if generation == self._preload_generation:
                        self.preload = status
                        self._preload_checked_at = time.monotonic()

        self.start_task(f"preload-{action}", operation)

    def snapshot(self) -> dict:
        self._refresh_preload_status_async()
        with self.lock:
            config = dict(self.config)
            config["skipped_updates"] = dict(config.get("skipped_updates", {}))
            telemetry = config.get("office_telemetry_disabled", {})
            config["office_telemetry_disabled"] = (
                dict(telemetry) if isinstance(telemetry, dict) else {}
            )
            task = dict(self.task)
            updater = dict(self.updater)
            if updater.get("offer"):
                updater["offer"] = {
                    **updater["offer"],
                    "metadata": dict(updater["offer"]["metadata"]),
                    "updates": dict(updater["offer"]["updates"]),
                }
            preload = {
                **self.preload,
                "binding": (
                    dict(self.preload["binding"])
                    if isinstance(self.preload.get("binding"), dict) else None
                ),
                "components": {
                    name: dict(value) for name, value in self.preload.get("components", {}).items()
                    if isinstance(value, dict)
                },
            }
        return {
            "config": config,
            "status": backend.environment_status(config["prefix"], config["wine"]),
            "version": backend.current_version(),
            "wine_version": backend.current_wine_version(),
            "task": task,
            "updater": updater,
            "preload": preload,
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
            self.task = {
                "running": True, "kind": kind, "status": "running", "log": "",
                "restart_required": False,
            }
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
    parser = argparse.ArgumentParser(description="Wine4Office Manager")
    parser.add_argument("--install-shortcut", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--post-update", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--smoke-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--screenshot", metavar="PATH", help=argparse.SUPPRESS)
    parser.add_argument("--no-browser", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--prefix", help=argparse.SUPPRESS)
    parser.add_argument("--wine", help=argparse.SUPPRESS)
    telemetry_policy = parser.add_mutually_exclusive_group()
    telemetry_policy.add_argument(
        "--disable-office-telemetry", action="store_true",
        help="Set Microsoft's Office diagnostic-data policy to Neither for the selected prefix.",
    )
    telemetry_policy.add_argument(
        "--restore-office-telemetry-default", action="store_true",
        help="Remove the Office telemetry value previously managed for the selected prefix.",
    )

    parser.add_argument(
        "--preload-service", choices=("status", "enable", "disable", "start", "stop"),
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--preload-worker", nargs=2, metavar=("SNAPSHOT", "STATUS"),
        help=argparse.SUPPRESS,
    )
    parser.add_argument("target", nargs="?", choices=[*backend.APP_META, *backend.TOOL_META, "stop"],
                        help=argparse.SUPPRESS)
    parser.add_argument("documents", nargs="*", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.post_update:
        if (args.target or args.documents or args.preload_service or args.preload_worker
                or args.install_shortcut or args.disable_office_telemetry
                or args.restore_office_telemetry_default or args.smoke_test
                or args.screenshot):
            parser.error("--post-update cannot be combined with another operation.")
        config = backend.load_config()
        if args.prefix:
            config["prefix"] = args.prefix
        if args.wine:
            config["wine"] = args.wine
        try:
            post_install.run_post_install(
                config, FONT_HELPER, MANAGER_RESTART_COMMAND, ICONS,
                output=print, force=True,
            )
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            print(f"wine4office post-install: {error}", file=sys.stderr)
            return 1
        return 0
    if args.preload_worker:
        if (args.target or args.documents or args.preload_service
                or args.disable_office_telemetry or args.restore_office_telemetry_default):
            parser.error("--preload-worker cannot be combined with another operation.")

        try:
            return backend.run_preload_worker(*args.preload_worker)
        except Exception as error:
            print(f"wine4office preload worker: {error}", file=sys.stderr)
            return 1

    if args.disable_office_telemetry or args.restore_office_telemetry_default:
        if args.target or args.documents or args.preload_service or args.install_shortcut:
            parser.error("Office telemetry policy options cannot be combined with another operation.")
        config = backend.load_config()
        prefix = args.prefix or config["prefix"]
        wine = args.wine or config["wine"]
        was_disabled = backend.office_telemetry_disabled(config, prefix)
        disable = args.disable_office_telemetry
        try:
            if disable or was_disabled:
                backend.apply_office_telemetry_policy(
                    prefix, wine, disable, remove_managed=was_disabled,
                    use_x11=config.get("use_x11", True),
                )
            updated = backend.set_office_telemetry_disabled(config, prefix, disable)
            backend.save_config(updated)
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            print(f"wine4office Office telemetry policy: {error}", file=sys.stderr)
            return 1
        print(
            "Microsoft Office telemetry policy disabled."
            if disable else "Microsoft Office telemetry policy restored to its default."
        )
        return 0

    if args.preload_service:
        if args.target or args.documents:
            parser.error("--preload-service cannot be combined with an app or tool.")
        defaults = backend.load_config()
        prefix = args.prefix or defaults["prefix"]
        wine = args.wine or defaults["wine"]
        use_x11 = defaults.get("use_x11", True)
        try:
            if args.preload_service == "status":
                status = backend.preload_service_status(prefix, wine, use_x11)
                print(__import__("json").dumps(status, sort_keys=True))
                return 0 if status["supported"] else 3
            if args.preload_service == "enable":
                status = backend.install_preload_service(prefix, wine, use_x11)
            else:
                status = backend.manage_preload_service(
                    args.preload_service, prefix, wine, use_x11
                )
            print(__import__("json").dumps(status, sort_keys=True))
            return 0
        except (OSError, RuntimeError, ValueError) as error:
            print(f"wine4office preload service: {error}", file=sys.stderr)
            return 1

    if args.target:
        defaults = backend.load_config()
        prefix = args.prefix or defaults["prefix"]
        wine = args.wine or defaults["wine"]
        use_x11 = defaults["use_x11"]
        if args.target in backend.APP_META:
            backend.launch_app(
                prefix, wine, args.target, FONT_HELPER, args.documents, use_x11=use_x11
            )
        else:
            if args.documents:
                parser.error("Wine tools do not accept document arguments.")
            backend.launch_tool(prefix, wine, args.target, use_x11=use_x11)
        return 0

    if args.install_shortcut:
        path = backend.install_manager_shortcut(
            LAUNCHER if FROZEN else LAUNCHER.with_name("wine4office-manager"), ICONS
        )
        print(path)
        return 0

    try:
        post_install.run_post_install(
            backend.load_config(), FONT_HELPER, MANAGER_RESTART_COMMAND, ICONS,
            output=lambda line: print(line, file=sys.stderr),
        )
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"wine4office post-install warning: {error}", file=sys.stderr)

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
        restart_command=MANAGER_RESTART_COMMAND,
        smoke_test=args.smoke_test,
        screenshot=Path(args.screenshot).expanduser() if args.screenshot else None,
    )


if __name__ == "__main__":
    raise SystemExit(main())
