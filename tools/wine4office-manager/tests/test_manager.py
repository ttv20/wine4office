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
            updated = state.update_config({"prefix": original_prefix, "desktop_copy": True})
            snapshot = state.snapshot()
        self.assertEqual(updated["prefix"], original_prefix)
        self.assertTrue(updated["desktop_copy"])
        self.assertTrue(snapshot["status"]["prefix_exists"])

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

    def _wait(self, state):
        deadline = time.monotonic() + 2
        while state.snapshot()["task"]["running"] and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertFalse(state.snapshot()["task"]["running"])


if __name__ == "__main__":
    unittest.main()
