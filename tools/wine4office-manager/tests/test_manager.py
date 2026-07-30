#!/usr/bin/env python3

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

MANAGER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MANAGER_DIR))
import wine4office_backend as backend
import wine4office_manager as manager


class ManagerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name) / "home"
        self.home.mkdir()
        self.environment = mock.patch.dict(os.environ, {
            "HOME": str(self.home),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
        })
        self.environment.start()

    def tearDown(self):
        self.environment.stop()
        self.temp.cleanup()

    def _make_prefix(self, path, registry="registry"):
        path.mkdir(parents=True, exist_ok=True)
        (path / "system.reg").write_text(registry)
        (path / "user.reg").write_text(registry)
        (path / "drive_c").mkdir(exist_ok=True)
        (path / "dosdevices").mkdir(exist_ok=True)
        return path


    def test_manager_uses_native_qt_without_web_server(self):
        entrypoint = (MANAGER_DIR / "wine4office_manager.py").read_text()
        qt_source = (MANAGER_DIR / "wine4office_qt.py").read_text()
        self.assertIn("from PySide6.QtWidgets import", qt_source)
        self.assertNotIn("http.server", entrypoint)
        self.assertNotIn("webbrowser", entrypoint)
        self.assertFalse((MANAGER_DIR / "ui.html").exists())

    def test_state_updates_config_and_reports_snapshot(self):
        with mock.patch.object(backend, "environment_status", return_value={
            "prefix_exists": True, "wine_exists": True,
            "apps": {app: False for app in backend.APP_META},
        }):
            state = manager.ManagerState()
            original_prefix = state.config["prefix"]
            updated = state.update_config({
                "prefix": original_prefix,
                "desktop_copy": True,
                "use_x11": False,
            })
            snapshot = state.snapshot()
        self.assertEqual(updated["prefix"], original_prefix)
        self.assertTrue(updated["desktop_copy"])
        self.assertFalse(updated["use_x11"])
        self.assertFalse(snapshot["config"]["use_x11"])
        self.assertFalse(backend.load_config()["use_x11"])
        self.assertTrue(snapshot["status"]["prefix_exists"])

    def test_state_applies_and_removes_owned_office_telemetry_policy(self):
        state = manager.ManagerState()
        prefix = Path(state.config["prefix"])
        self._make_prefix(prefix)

        with mock.patch.object(backend, "apply_office_telemetry_policy") as apply:
            updated = state.update_config({
                "prefix": str(prefix),
                "disable_office_telemetry": True,
            })
            self.assertTrue(backend.office_telemetry_disabled(updated, prefix))
            apply.assert_called_once_with(
                str(prefix), updated["wine"], True,
                remove_managed=False, use_x11=True,
            )

            apply.reset_mock()
            updated = state.update_config({
                "prefix": str(prefix),
                "disable_office_telemetry": False,
            })
            self.assertFalse(backend.office_telemetry_disabled(updated, prefix))
            apply.assert_called_once_with(
                str(prefix), updated["wine"], False,
                remove_managed=True, use_x11=True,
            )

    def test_office_telemetry_apply_failure_does_not_save_enabled_state(self):
        state = manager.ManagerState()
        prefix = Path(state.config["prefix"])
        self._make_prefix(prefix)

        with mock.patch.object(
            backend, "apply_office_telemetry_policy",
            side_effect=RuntimeError("registry write failed"),
        ), mock.patch.object(backend, "save_config") as save:
            with self.assertRaisesRegex(RuntimeError, "registry write failed"):
                state.update_config({
                    "prefix": str(prefix),
                    "disable_office_telemetry": True,
                })

        save.assert_not_called()
        self.assertFalse(backend.office_telemetry_disabled(state.config, prefix))

    def test_environment_switch_keeps_telemetry_choice_per_prefix(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        new = self.home / "other-prefix"
        for prefix in (old, new):
            self._make_prefix(prefix)
        state.config = backend.set_office_telemetry_disabled(state.config, old, True)

        with mock.patch.object(backend, "apply_office_telemetry_policy") as apply:
            state.start_environment_transition({
                "prefix": str(new),
                "disable_office_telemetry": True,
            }, False, False)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(new))
        self.assertTrue(backend.office_telemetry_disabled(state.config, old))
        self.assertTrue(backend.office_telemetry_disabled(state.config, new))
        apply.assert_called_once_with(
            str(new), state.config["wine"], True,
            remove_managed=False, use_x11=True,
        )

    def test_initialized_environment_reapplies_selected_telemetry_policy(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        self._make_prefix(old)
        new = self.home / "initialized-prefix"

        def initialize(prefix, wine, recreate, output, cancel_event, process_callback):
            self._make_prefix(Path(prefix))
            return "ready"

        with mock.patch.object(
            backend, "create_environment", side_effect=initialize
        ), mock.patch.object(backend, "apply_office_telemetry_policy") as apply:
            state.start_environment_transition({
                "prefix": str(new),
                "disable_office_telemetry": True,
            }, True, False)
            self._wait(state)

        self.assertEqual(state.snapshot()["task"]["status"], "completed")
        apply.assert_called_once_with(
            str(new), state.config["wine"], True,
            remove_managed=False, use_x11=True,
        )

    def test_background_task_completion_and_failure_are_visible(self):
        state = manager.ManagerState()
        state.start_task("success", lambda: "native task completed")
        self._wait(state)
        snapshot = state.snapshot()["task"]
        self.assertEqual(snapshot["status"], "completed")
        self.assertIn("native task completed", snapshot["log"])

        state.start_task("failure", lambda: (_ for _ in ()).throw(RuntimeError("visible failure")))
        self._wait(state)
        snapshot = state.snapshot()["task"]
        self.assertEqual(snapshot["status"], "failed")
        self.assertIn("ERROR: visible failure", snapshot["log"])

    def test_installed_root_honors_standalone_binary_environment(self):
        root = self.home / ".local/share/wine4office"
        with mock.patch.dict(os.environ, {"WINE4OFFICE_MANAGER_ROOT": str(root)}):
            self.assertEqual(backend.installed_root(), root.resolve())

    def test_standalone_manager_launcher_uses_current_display_mode(self):
        config = backend.default_config()
        config["use_x11"] = False
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(backend, "launch_app") as launch, \
             mock.patch.object(sys, "argv", ["Wine4OfficeManager", "word"]):
            result = manager.main()

        self.assertEqual(result, 0)
        self.assertFalse(launch.call_args.kwargs["use_x11"])

    def test_cli_manages_telemetry_policy_for_explicit_prefix(self):
        prefix = self._make_prefix(self.home / "cli-prefix")
        config = backend.default_config()
        config["prefix"] = str(self.home / "different-prefix")
        saved = []

        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(backend, "apply_office_telemetry_policy") as apply, \
             mock.patch.object(backend, "save_config", side_effect=saved.append), \
             mock.patch("builtins.print"), \
             mock.patch.object(sys, "argv", [
                 "Wine4OfficeManager", "--prefix", str(prefix), "--wine",
                 config["wine"], "--disable-office-telemetry",
             ]):
            result = manager.main()

        self.assertEqual(result, 0)
        self.assertTrue(backend.office_telemetry_disabled(saved[0], prefix))
        apply.assert_called_once_with(
            str(prefix), config["wine"], True,
            remove_managed=False, use_x11=True,
        )

    def test_equivalent_symlink_edit_updates_without_a_transition(self):
        state = manager.ManagerState()
        real = self.home / "real-prefix"
        self._make_prefix(real)
        alias = self.home / "prefix-alias"
        alias.symlink_to(real, target_is_directory=True)
        state.config["prefix"] = str(real)

        updated = state.update_config({"prefix": str(alias)})

        self.assertEqual(updated["prefix"], str(alias))

    def test_missing_target_is_initialized_before_config_commit(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        self._make_prefix(old, "old")
        new = self.home / "new-prefix"

        def initialize(prefix, wine, recreate, output, cancel_event, process_callback):
            self.assertEqual(state.config["prefix"], str(old))
            self._make_prefix(Path(prefix), "new")
            return "ready"

        with mock.patch.object(backend, "create_environment", side_effect=initialize):
            state.start_environment_transition({"prefix": str(new)}, True, False)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(new))
        self.assertTrue((old / "system.reg").is_file())
        self.assertEqual(state.snapshot()["task"]["status"], "completed")

    def test_existing_valid_target_switches_without_initialization(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        new = self.home / "existing-prefix"
        for prefix in (old, new):
            self._make_prefix(prefix)

        with mock.patch.object(backend, "create_environment") as initialize:
            state.start_environment_transition({"prefix": str(new)}, False, False)
            self._wait(state)

        initialize.assert_not_called()
        self.assertEqual(state.config["prefix"], str(new))
        self.assertTrue(old.exists())

    def test_nonempty_invalid_target_is_rejected_without_starting_work(self):
        state = manager.ManagerState()
        target = self.home / "documents"
        target.mkdir()
        (target / "important").write_text("keep")

        with self.assertRaisesRegex(ValueError, "not a valid Wine prefix"):
            state.start_environment_transition({"prefix": str(target)}, False, False)

        self.assertEqual(state.config["prefix"], str(self.home / ".wine4office"))
        self.assertFalse(state.task["running"])
        self.assertEqual((target / "important").read_text(), "keep")

    def test_initialization_failure_preserves_old_config_and_environment(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        self._make_prefix(old, "old")
        new = self.home / "failed-prefix"

        with mock.patch.object(
            backend, "create_environment", side_effect=RuntimeError("wineboot failed")
        ):
            state.start_environment_transition({"prefix": str(new)}, True, False)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(old))
        self.assertEqual((old / "system.reg").read_text(), "old")
        self.assertEqual(state.snapshot()["task"]["status"], "failed")

    def test_cancellation_discards_new_prefix_and_preserves_old(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        self._make_prefix(old, "old")
        new = self.home / "cancelled-prefix"

        def initialize(prefix, wine, recreate, output, cancel_event, process_callback):
            self._make_prefix(Path(prefix), "new")
            cancel_event.set()
            return "ready"

        with mock.patch.object(backend, "create_environment", side_effect=initialize):
            state.start_environment_transition({"prefix": str(new)}, True, False)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(old))
        self.assertTrue(old.exists())
        self.assertFalse(new.exists())
        self.assertEqual(state.snapshot()["task"]["status"], "cancelled")

    def test_cancellation_after_staging_restores_old_environment(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        new = self.home / "new-prefix"
        for prefix in (old, new):
            self._make_prefix(prefix)
        staged = old.with_name(".old-staged")

        def stage(old_value, new_value, wine_value, output):
            old.rename(staged)
            state.cancel_event.set()
            return staged

        with mock.patch.object(backend, "stage_environment_deletion", side_effect=stage):
            state.start_environment_transition({"prefix": str(new)}, False, True)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(old))
        self.assertTrue((old / "system.reg").is_file())
        self.assertFalse(staged.exists())
        self.assertEqual(state.snapshot()["task"]["status"], "cancelled")

    def test_approved_deletion_runs_only_after_replacement_is_ready(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        new = self.home / "new-prefix"
        for prefix in (old, new):
            self._make_prefix(prefix)
        staged = old.with_name(".old-staged")
        events = []

        def stage(old_value, new_value, wine_value, output):
            self.assertTrue((new / "system.reg").is_file())
            self.assertEqual(state.config["prefix"], str(old))
            events.append(("stage", old_value, new_value, wine_value))
            old.rename(staged)
            return staged

        def finish(staged_value, output):
            self.assertEqual(state.config["prefix"], str(new))
            events.append(("delete", staged_value))

        with mock.patch.object(backend, "stage_environment_deletion", side_effect=stage), \
             mock.patch.object(backend, "finish_staged_environment_deletion", side_effect=finish):
            state.start_environment_transition({"prefix": str(new)}, False, True)
            self._wait(state)

        self.assertEqual(events[0], ("stage", str(old), str(new), state.config["wine"]))
        self.assertEqual(events[1], ("delete", staged))
        self.assertEqual(state.config["prefix"], str(new))

    def test_config_commit_failure_rolls_back_staged_old_environment(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        new = self.home / "new-prefix"
        for prefix in (old, new):
            self._make_prefix(prefix)
        staged = old.with_name(".old-staged")

        def stage(old_value, new_value, wine_value, output):
            old.rename(staged)
            return staged

        with mock.patch.object(backend, "stage_environment_deletion", side_effect=stage), \
             mock.patch.object(backend, "save_config", side_effect=OSError("disk full")):
            state.start_environment_transition({"prefix": str(new)}, False, True)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(old))
        self.assertTrue((old / "system.reg").is_file())
        self.assertFalse(staged.exists())
        self.assertEqual(state.snapshot()["task"]["status"], "failed")

    def test_overlapping_deletion_is_rejected_before_work_starts(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        self._make_prefix(old)
        nested = old / "nested"

        with self.assertRaisesRegex(ValueError, "overlapping"):
            state.start_environment_transition({"prefix": str(nested)}, True, True)

        self.assertEqual(state.config["prefix"], str(old))
        self.assertFalse(state.task["running"])

    def test_preload_status_cache_refreshes_off_main_thread(self):
        entered = __import__("threading").Event()
        release = __import__("threading").Event()
        main_thread = __import__("threading").current_thread()
        callers = []
        status = {
            "supported": True, "reason": "", "installed": False,
            "enabled": False, "active": False, "state": "uninstalled",
            "binding": None, "selected_matches": False, "components": {},
            "detail": "",
        }

        def blocking_status(*_args):
            callers.append(__import__("threading").current_thread())
            entered.set()
            release.wait(1)
            return dict(status)

        with mock.patch.object(
            backend, "preload_service_status", side_effect=blocking_status
        ):
            state = manager.ManagerState()
            self.assertTrue(entered.wait(1))
            self.assertTrue(state.snapshot()["preload"]["checking"])
            release.set()
            deadline = time.monotonic() + 1
            while state.snapshot()["preload"]["checking"] and time.monotonic() < deadline:
                time.sleep(0.01)

        self.assertIsNot(callers[0], main_thread)
        self.assertEqual(state.snapshot()["preload"]["state"], "uninstalled")

    def test_preload_action_dispatches_async_and_surfaces_stop_refusal(self):
        status = {
            "supported": True, "reason": "", "installed": True,
            "enabled": True, "active": True, "state": "active",
            "binding": {}, "selected_matches": True, "components": {},
            "detail": "",
        }
        with mock.patch.object(
            backend, "preload_service_status", return_value=dict(status)
        ), mock.patch.object(
            backend, "manage_preload_service",
            side_effect=RuntimeError("Office is active; refusing to stop preload"),
        ) as manage:
            state = manager.ManagerState()
            deadline = time.monotonic() + 1
            while state.snapshot()["preload"]["checking"] and time.monotonic() < deadline:
                time.sleep(0.01)
            state.start_preload_action("stop")
            self.assertEqual(state.snapshot()["task"]["kind"], "preload-stop")
            self._wait(state)

        task = state.snapshot()["task"]
        self.assertEqual(task["status"], "failed")
        self.assertIn("Office is active; refusing", task["log"])
        manage.assert_called_once_with(
            "stop", state.config["prefix"], state.config["wine"],
            state.config.get("use_x11", True),
        )

    def test_preload_enable_action_never_dispatches_start(self):
        status = {
            "supported": True, "reason": "", "installed": True,
            "enabled": True, "active": False, "state": "inactive",
            "binding": {}, "selected_matches": True, "components": {},
            "detail": "",
        }
        with mock.patch.object(
            backend, "preload_service_status", return_value=dict(status)
        ), mock.patch.object(
            backend, "install_preload_service", return_value=dict(status)
        ) as install, mock.patch.object(
            backend, "manage_preload_service"
        ) as manage:
            state = manager.ManagerState()
            deadline = time.monotonic() + 1
            while state.snapshot()["preload"]["checking"] and time.monotonic() < deadline:
                time.sleep(0.01)
            state.start_preload_action("enable")
            self._wait(state)

        install.assert_called_once()
        manage.assert_not_called()
        self.assertIn("was not started", state.snapshot()["task"]["log"])

    def test_preload_cli_routes_actions_and_exit_codes(self):
        config = backend.default_config()
        supported = {
            "supported": True, "reason": "", "installed": False,
            "enabled": False, "active": False, "state": "uninstalled",
            "binding": None, "selected_matches": False, "components": {},
            "detail": "",
        }
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "preload_service_status", return_value=supported
             ) as status, mock.patch("builtins.print"), \
             mock.patch.object(
                 sys, "argv", ["Wine4OfficeManager", "--preload-service", "status"]
             ):
            self.assertEqual(manager.main(), 0)
        status.assert_called_once_with(
            config["prefix"], config["wine"], config.get("use_x11", True)
        )

        unsupported = dict(supported, supported=False, reason="no user bus")
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "preload_service_status", return_value=unsupported
             ), mock.patch("builtins.print"), \
             mock.patch.object(
                 sys, "argv", ["Wine4OfficeManager", "--preload-service", "status"]
             ):
            self.assertEqual(manager.main(), 3)

        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "install_preload_service",
                 side_effect=RuntimeError("enable failed"),
             ), mock.patch("builtins.print"), \
             mock.patch.object(
                 sys, "argv", ["Wine4OfficeManager", "--preload-service", "enable"]
             ):
            self.assertEqual(manager.main(), 1)

    def test_internal_worker_cli_uses_two_explicit_paths(self):
        with mock.patch.object(
            backend, "run_preload_worker", return_value=7
        ) as worker, mock.patch.object(
            sys, "argv",
            ["Wine4OfficeManager", "--preload-worker", "/snapshot", "/status"],
        ):
            self.assertEqual(manager.main(), 7)
        worker.assert_called_once_with("/snapshot", "/status")

    def _wait(self, state):
        deadline = time.monotonic() + 2
        while state.snapshot()["task"]["running"] and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertFalse(state.snapshot()["task"]["running"])


if __name__ == "__main__":
    unittest.main()
