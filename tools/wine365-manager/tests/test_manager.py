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
import wine365_backend as backend
import wine365_manager as manager


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

    def test_manager_uses_native_qt_without_web_server(self):
        entrypoint = (MANAGER_DIR / "wine365_manager.py").read_text()
        qt_source = (MANAGER_DIR / "wine365_qt.py").read_text()
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
            updated = state.update_config({"prefix": " /tmp/native-qt-prefix ", "desktop_copy": True})
            snapshot = state.snapshot()
        self.assertEqual(updated["prefix"], "/tmp/native-qt-prefix")
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
        root = self.home / ".local/share/wine365"
        with mock.patch.dict(os.environ, {"WINE365_MANAGER_ROOT": str(root)}):
            self.assertEqual(backend.installed_root(), root.resolve())

    def _wait(self, state):
        deadline = time.monotonic() + 2
        while state.snapshot()["task"]["running"] and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertFalse(state.snapshot()["task"]["running"])


if __name__ == "__main__":
    unittest.main()
