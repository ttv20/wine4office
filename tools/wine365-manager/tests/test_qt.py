#!/usr/bin/env python3

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
MANAGER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MANAGER_DIR))

try:
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QApplication, QListWidget, QStackedWidget, QTreeWidget
    import wine365_backend as backend
    import wine365_manager as manager
    from wine365_qt import ManagerWindow
    HAS_QT = True
except ImportError:
    HAS_QT = False


@unittest.skipUnless(HAS_QT, "PySide6 is not installed")
class QtManagerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QApplication.instance() or QApplication(["wine365-manager-test"])

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
        self.config = {
            "prefix": str(self.home / ".wine365"),
            "wine": str(self.home / "runner/bin/wine"),
            "desktop_copy": False,
            "update_url": "",
        }
        self.status = {
            "prefix_exists": True,
            "wine_exists": True,
            "apps": {app: app == "word" for app in backend.APP_META},
        }
        self.patches = [
            mock.patch.object(backend, "load_config", return_value=dict(self.config)),
            mock.patch.object(backend, "save_config"),
            mock.patch.object(backend, "environment_status", return_value=dict(self.status)),
        ]
        for patch in self.patches:
            patch.start()
        self.state = manager.ManagerState()
        self.window = ManagerWindow(
            self.state,
            MANAGER_DIR / "wine365-launcher",
            MANAGER_DIR / "icons",
            MANAGER_DIR / "register-office-cloud-fonts.sh",
        )

    def tearDown(self):
        self.window.close()
        self.application.processEvents()
        for patch in reversed(self.patches):
            patch.stop()
        self.environment.stop()
        self.temp.cleanup()

    def test_ui_uses_system_theme_and_native_navigation_controls(self):
        self.assertEqual(self.window.styleSheet(), "")
        self.assertIsInstance(self.window.navigation, QListWidget)
        self.assertIsInstance(self.window.pages, QStackedWidget)
        self.assertIsInstance(self.window.app_tree, QTreeWidget)
        self.assertEqual(self.window.navigation.count(), 4)

    def test_slow_application_launch_does_not_block_qt_and_gates_actions(self):
        self.window.app_items["word"].setCheckState(0, Qt.CheckState.Checked)

        def slow_launch(*args, **kwargs):
            time.sleep(0.35)
            return 4321

        with mock.patch.object(backend, "launch_app", side_effect=slow_launch):
            started = time.monotonic()
            self.window.launch_selected()
            elapsed = time.monotonic() - started
            self.assertLess(elapsed, 0.15)
            self.window.refresh_state()
            self.assertTrue(self.state.snapshot()["task"]["running"])
            self.assertTrue(all(not button.isEnabled() for button in self.window.task_sensitive_buttons))
            self.assertTrue(self.window.cancel_button.isEnabled())
            deadline = time.monotonic() + 2
            while self.state.snapshot()["task"]["running"] and time.monotonic() < deadline:
                time.sleep(0.01)

        task = self.state.snapshot()["task"]
        self.assertEqual(task["status"], "completed")
        self.assertIn("Application started (PID 4321)", task["log"])


if __name__ == "__main__":
    unittest.main()
