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
import wine4office_post_install as post_install
import wine4office_preload as preload_helper


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

    def _mark_prefix(self, path):
        manager.mark_prefix_owned(path)
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

    def test_state_enables_and_disables_automatic_update_schedule(self):
        state = manager.ManagerState()
        with mock.patch.object(
            backend, "install_automatic_update_schedule"
        ) as install, mock.patch.object(
            backend, "disable_automatic_update_schedule"
        ) as disable:
            enabled = state.set_automatic_update_checks(True)
            disabled = state.set_automatic_update_checks(False)

        self.assertTrue(enabled["automatic_update_checks"])
        self.assertTrue(enabled["automatic_update_checks_prompted"])
        self.assertFalse(disabled["automatic_update_checks"])
        install.assert_called_once_with()
        disable.assert_called_once_with()

    def test_automatic_update_preference_rolls_back_schedule_on_save_failure(self):
        state = manager.ManagerState()
        with mock.patch.object(
            backend, "install_automatic_update_schedule"
        ) as install, mock.patch.object(
            backend, "disable_automatic_update_schedule"
        ) as disable, mock.patch.object(
            backend, "save_config", side_effect=OSError("disk full")
        ):
            with self.assertRaisesRegex(OSError, "disk full"):
                state.set_automatic_update_checks(True)

        install.assert_called_once_with()
        disable.assert_called_once_with()
        self.assertFalse(state.config["automatic_update_checks"])

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

    def test_state_applies_owned_office_compatibility_policies(self):
        state = manager.ManagerState()
        prefix = Path(state.config["prefix"])
        self._make_prefix(prefix)
        desired = {
            policy_id: policy_id in {"disable_animations", "skip_start_screen"}
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }

        with mock.patch.object(
            backend, "apply_office_compatibility_policies"
        ) as apply:
            updated = state.update_config({
                "prefix": str(prefix),
                "office_compatibility_settings": desired,
            })

        self.assertEqual(
            backend.office_compatibility_settings(updated, prefix), desired
        )
        apply.assert_called_once_with(
            str(prefix), updated["wine"], desired,
            {policy_id: False for policy_id in desired},
            use_x11=True,
        )

    def test_compatibility_policy_failure_does_not_save_enabled_state(self):
        state = manager.ManagerState()
        prefix = Path(state.config["prefix"])
        self._make_prefix(prefix)
        desired = {
            policy_id: policy_id == "disable_animations"
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }

        with mock.patch.object(
            backend, "apply_office_compatibility_policies",
            side_effect=RuntimeError("registry write failed"),
        ), mock.patch.object(backend, "save_config") as save:
            with self.assertRaisesRegex(RuntimeError, "registry write failed"):
                state.update_config({
                    "prefix": str(prefix),
                    "office_compatibility_settings": desired,
                })

        save.assert_not_called()
        self.assertFalse(any(
            backend.office_compatibility_settings(state.config, prefix).values()
        ))

    def test_config_save_failure_rolls_back_only_changed_compatibility_values(self):
        state = manager.ManagerState()
        prefix = Path(state.config["prefix"])
        self._make_prefix(prefix)
        before = {
            policy_id: True
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }
        desired = {
            policy_id: False
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }
        state.config = backend.set_office_compatibility_settings(
            state.config, prefix, before
        )

        with mock.patch.object(
            backend, "apply_office_compatibility_policies",
            side_effect=[["disable_animations"], ["disable_animations"]],
        ) as apply, mock.patch.object(
            backend, "save_config", side_effect=OSError("disk full")
        ):
            with self.assertRaisesRegex(OSError, "disk full"):
                state.update_config({
                    "prefix": str(prefix),
                    "office_compatibility_settings": desired,
                })

        rollback = dict(desired)
        rollback["disable_animations"] = True
        self.assertEqual(apply.call_count, 2)
        self.assertEqual(apply.call_args_list[1], mock.call(
            str(prefix), state.config["wine"], rollback, desired,
            use_x11=True,
        ))

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
            self._mark_prefix(Path(prefix))
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

    def test_initialized_environment_applies_saved_compatibility_policies(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        self._make_prefix(old)
        new = self.home / "initialized-prefix"
        desired = {
            policy_id: policy_id == "disable_animations"
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }
        state.config = backend.set_office_compatibility_settings(
            state.config, new, desired
        )

        def initialize(prefix, wine, recreate, output, cancel_event, process_callback):
            self._make_prefix(Path(prefix))
            self._mark_prefix(Path(prefix))
            return "ready"

        with mock.patch.object(
            backend, "create_environment", side_effect=initialize
        ), mock.patch.object(
            backend, "apply_office_compatibility_policies"
        ) as apply:
            state.start_environment_transition(
                {"prefix": str(new)}, True, False
            )
            self._wait(state)

        self.assertEqual(state.snapshot()["task"]["status"], "completed")
        apply.assert_called_once_with(
            str(new), state.config["wine"], desired,
            {policy_id: False for policy_id in desired},
            use_x11=True,
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
             mock.patch.object(manager.incident, "monitoring_enabled", return_value=False), \
             mock.patch.object(backend, "launch_app") as launch, \
             mock.patch.object(sys, "argv", ["Wine4OfficeManager", "word"]):
            result = manager.main()

        self.assertEqual(result, 0)
        self.assertFalse(launch.call_args.kwargs["use_x11"])

    def test_post_update_cli_runs_new_version_hooks_with_explicit_paths(self):
        prefix = str(self.home / "selected-prefix")
        wine = str(self.home / "runner/bin/wine")
        with mock.patch.object(
            post_install, "run_post_install"
        ) as run, mock.patch.object(
            sys, "argv", [
                "Wine4OfficeManager", "--post-update",
                "--prefix", prefix, "--wine", wine,
            ]
        ):
            result = manager.main()

        self.assertEqual(result, 0)
        config = run.call_args.args[0]
        self.assertEqual(config["prefix"], prefix)
        self.assertEqual(config["wine"], wine)
        self.assertEqual(run.call_args.args[1], manager.FONT_HELPER)
        self.assertEqual(run.call_args.args[2], manager.MANAGER_RESTART_COMMAND)
        self.assertEqual(run.call_args.args[3], manager.ICONS)
        self.assertTrue(run.call_args.kwargs["force"])

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
            self._mark_prefix(Path(prefix))
            return "ready"

        with mock.patch.object(backend, "create_environment", side_effect=initialize):
            state.start_environment_transition({"prefix": str(new)}, True, False)
            self._wait(state)

        self.assertEqual(state.config["prefix"], str(new))
        self.assertTrue((old / "system.reg").is_file())
        marker = new / manager.PREFIX_MARKER_NAME
        self.assertEqual(marker.read_bytes(), manager.PREFIX_MARKER_CONTENT)
        self.assertEqual(marker.stat().st_mode & 0o777, 0o600)
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
            self._mark_prefix(Path(prefix))
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
        self._mark_prefix(self._make_prefix(old))
        self._make_prefix(new)
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
        self._mark_prefix(self._make_prefix(old))
        self._make_prefix(new)
        staged = old.with_name(".old-staged")
        events = []

        def stage(old_value, new_value, wine_value, output):
            self.assertTrue((new / "system.reg").is_file())
            self.assertEqual(state.config["prefix"], str(old))
            events.append(("stage", old_value, new_value, wine_value))
            old.rename(staged)
            return staged

        def finish(staged_value):
            self.assertEqual(state.config["prefix"], str(new))
            events.append(("delete", staged_value))

        with mock.patch.object(backend, "stage_environment_deletion", side_effect=stage), \
             mock.patch.object(manager, "remove_owned_prefix", side_effect=finish):
            state.start_environment_transition({"prefix": str(new)}, False, True)
            self._wait(state)

        self.assertEqual(events[0], ("stage", str(old), str(new), state.config["wine"]))
        self.assertEqual(events[1], ("delete", staged))
        self.assertEqual(state.config["prefix"], str(new))

    def test_config_commit_failure_rolls_back_staged_old_environment(self):
        state = manager.ManagerState()
        old = Path(state.config["prefix"])
        new = self.home / "new-prefix"
        self._mark_prefix(self._make_prefix(old))
        self._make_prefix(new)
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
        self._mark_prefix(self._make_prefix(old))
        nested = old / "nested"

        with self.assertRaisesRegex(ValueError, "overlapping"):
            state.start_environment_transition({"prefix": str(nested)}, True, True)

        self.assertEqual(state.config["prefix"], str(old))
        self.assertFalse(state.task["running"])

    def test_owned_prefix_removal_requires_marker_and_layout(self):
        ordinary = self.home / "user-data"
        ordinary.mkdir()
        (ordinary / "important").write_text("keep")
        with self.assertRaisesRegex(ValueError, "Wine prefix"):
            manager.remove_owned_prefix(ordinary)
        self.assertEqual((ordinary / "important").read_text(), "keep")

    def test_owned_prefix_removal_refuses_symlink_swap(self):
        prefix = self._mark_prefix(self._make_prefix(self.home / "selected-prefix"))
        victim = self.home / "user-data"
        victim.mkdir()
        (victim / "important").write_text("keep")
        moved = self.home / "selected-prefix-original"
        prefix.rename(moved)
        prefix.symlink_to(victim, target_is_directory=True)

        with self.assertRaisesRegex(ValueError, "symlink"):
            manager.remove_owned_prefix(prefix)
        self.assertTrue(moved.exists())
        self.assertEqual((victim / "important").read_text(), "keep")

    def test_owned_prefix_removal_deletes_only_valid_owned_prefix(self):
        prefix = self._mark_prefix(self._make_prefix(self.home / "selected-prefix"))
        removed = manager.remove_owned_prefix(prefix)
        self.assertEqual(removed, prefix)
        self.assertFalse(prefix.exists())

    def test_prefix_removal_refuses_when_wine_shutdown_fails(self):
        prefix = self._mark_prefix(self._make_prefix(self.home / "selected-prefix"))
        with mock.patch.object(
            backend, "stop_wine", side_effect=RuntimeError("wineserver still active")
        ):
            with self.assertRaisesRegex(manager.WineShutdownFailed, "shutdown failed"):
                manager.stop_and_remove_owned_prefix(prefix, "/runner/bin/wine")
        self.assertTrue(prefix.exists())

    def test_prefix_removal_reports_missing_wine_executable_separately(self):
        with mock.patch.object(
            backend, "stop_wine", side_effect=FileNotFoundError("wine missing")
        ):
            with self.assertRaisesRegex(
                    manager.WineShutdownUnavailable, "executable is missing"):
                manager.stop_wine_confirmed(self.home / "prefix", "/runner/bin/wine")

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

    def test_periodic_preload_refresh_updates_only_memory(self):
        status = {
            "supported": True, "reason": "", "installed": True,
            "enabled": True, "active": True, "state": "active",
            "binding": {}, "selected_matches": True, "components": {},
            "detail": "",
        }
        with mock.patch.object(
            backend, "preload_service_status", return_value=dict(status)
        ) as full_status, mock.patch.object(
            backend, "preload_service_memory_bytes",
            return_value=600 * 1024 * 1024,
        ) as memory:
            state = manager.ManagerState()
            deadline = time.monotonic() + 1
            while state.snapshot()["preload"]["checking"] and time.monotonic() < deadline:
                time.sleep(0.01)
            full_status.reset_mock()
            memory.return_value = 612 * 1024 * 1024
            memory.reset_mock()
            with state.lock:
                state._preload_checked_at = 0
            self.assertTrue(state._refresh_preload_memory_async())
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                with state.lock:
                    finished = memory.call_count >= 1 and not state._preload_memory_checking
                if finished:
                    break
                time.sleep(0.01)

        full_status.assert_not_called()
        with state.lock:
            self.assertEqual(
                state.preload["memory_bytes"], 612 * 1024 * 1024
            )

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
            state.start_preload_action("stop-disable")
            self.assertEqual(
                state.snapshot()["task"]["kind"], "preload-stop-disable"
            )
            self._wait(state)

        task = state.snapshot()["task"]
        self.assertEqual(task["status"], "failed")
        self.assertIn("Office is active; refusing", task["log"])
        manage.assert_called_once_with(
            "stop", state.config["prefix"], state.config["wine"],
            state.config.get("use_x11", True),
        )

    def test_preload_enable_start_action_runs_both_steps(self):
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
            state.start_preload_action("enable-start")
            self._wait(state)

        install.assert_called_once()
        manage.assert_called_once_with(
            "start", state.config["prefix"], state.config["wine"],
            state.config.get("use_x11", True),
        )
        self.assertIn("enabled and started", state.snapshot()["task"]["log"])

    def test_preload_enable_start_rolls_back_new_enable_when_start_fails(self):
        status = {
            "supported": True, "reason": "", "installed": False,
            "enabled": False, "active": False, "state": "uninstalled",
            "binding": None, "selected_matches": False, "components": {},
            "detail": "",
        }
        actions = []

        def manage(action, *_args):
            actions.append(action)
            if action == "start":
                raise RuntimeError("start failed")

        with mock.patch.object(
            backend, "preload_service_status", return_value=dict(status)
        ), mock.patch.object(
            backend, "install_preload_service", return_value=dict(status)
        ), mock.patch.object(
            backend, "manage_preload_service", side_effect=manage
        ):
            state = manager.ManagerState()
            deadline = time.monotonic() + 1
            while state.snapshot()["preload"]["checking"] and time.monotonic() < deadline:
                time.sleep(0.01)
            state.start_preload_action("enable-start")
            self._wait(state)

        self.assertEqual(actions, ["start", "disable"])
        self.assertEqual(state.snapshot()["task"]["status"], "failed")
        self.assertIn("start failed", state.snapshot()["task"]["log"])

    def test_preload_stop_disable_action_runs_both_steps(self):
        status = {
            "supported": True, "reason": "", "installed": True,
            "enabled": True, "active": True, "state": "active",
            "binding": {}, "selected_matches": True, "components": {},
            "detail": "",
        }
        actions = []
        with mock.patch.object(
            backend, "preload_service_status", return_value=dict(status)
        ), mock.patch.object(
            backend, "manage_preload_service",
            side_effect=lambda action, *_args: actions.append(action),
        ):
            state = manager.ManagerState()
            deadline = time.monotonic() + 1
            while state.snapshot()["preload"]["checking"] and time.monotonic() < deadline:
                time.sleep(0.01)
            state.start_preload_action("stop-disable")
            self._wait(state)

        self.assertEqual(actions, ["stop", "disable"])
        self.assertIn("stopped and disabled", state.snapshot()["task"]["log"])

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

    def test_lightweight_preload_helper_uses_two_explicit_paths(self):
        with mock.patch.object(
            backend, "run_preload_worker", return_value=7
        ) as worker:
            self.assertEqual(preload_helper.main(["/snapshot", "/status"]), 7)
        worker.assert_called_once_with(Path("/snapshot"), Path("/status"))

    def _wait(self, state):
        deadline = time.monotonic() + 2
        while state.snapshot()["task"]["running"] and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertFalse(state.snapshot()["task"]["running"])


if __name__ == "__main__":
    unittest.main()
