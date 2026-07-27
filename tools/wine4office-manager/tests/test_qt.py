#!/usr/bin/env python3

import os
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
MANAGER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MANAGER_DIR))

try:
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QCloseEvent
    from PySide6.QtWidgets import QApplication, QListWidget, QMessageBox, QStackedWidget, QTreeWidget
    import wine4office_backend as backend
    import wine4office_manager as manager
    import wine4office_qt as qt_module
    from wine4office_qt import ManagerWindow
    HAS_QT = True
except ImportError:
    HAS_QT = False


@unittest.skipUnless(HAS_QT, "PySide6 is not installed")
class QtManagerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QApplication.instance() or QApplication(["wine4office-manager-test"])

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
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "runner/bin/wine"),
            "desktop_copy": False,
            "update_url": "",
        }
        self.old_prefix = self._make_prefix(Path(self.config["prefix"]))
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
            MANAGER_DIR / "wine4office-launcher",
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

    def _make_prefix(self, path, registry="registry"):
        path.mkdir(parents=True, exist_ok=True)
        (path / "system.reg").write_text(registry)
        (path / "user.reg").write_text(registry)
        (path / "drive_c").mkdir(exist_ok=True)
        (path / "dosdevices").mkdir(exist_ok=True)
        return path


    def test_ui_uses_system_theme_and_native_navigation_controls(self):
        self.assertEqual(self.window.styleSheet(), "")
        self.assertEqual(self.window.windowTitle(), "Wine4OfficeManager")
        self.assertIsInstance(self.window.navigation, QListWidget)
        self.assertIsInstance(self.window.pages, QStackedWidget)
        self.assertIsInstance(self.window.app_tree, QTreeWidget)
        self.assertEqual(self.window.navigation.count(), 4)

    def test_selected_rows_are_used_for_shortcut_actions(self):
        self.window.app_items["excel"].setSelected(True)
        self.window.app_items["powerpoint"].setSelected(True)
        self.assertEqual(self.window.selected_apps(), ["excel", "powerpoint"])

    def test_shortcut_creation_defaults_to_every_installed_application(self):
        self.window.app_tree.clearSelection()
        for item in self.window.app_items.values():
            item.setCheckState(0, Qt.CheckState.Unchecked)
        self.window.installed_apps = {"word", "excel", "powerpoint"}
        with mock.patch.object(backend, "create_app_shortcuts", return_value=["one", "two", "three"]) as create:
            self.window.shortcut_action(True)
        self.assertEqual(create.call_args.args[0], ["word", "excel", "powerpoint"])

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

    def test_equivalent_symlink_path_saves_without_prompts(self):
        alias = self.home / "prefix-alias"
        alias.symlink_to(self.old_prefix, target_is_directory=True)
        self.window.prefix_edit.setText(str(alias))

        with mock.patch.object(QMessageBox, "question") as question, \
             mock.patch.object(QMessageBox, "warning") as warning:
            result = self.window.save_config()

        self.assertEqual(result["prefix"], str(alias))
        question.assert_not_called()
        warning.assert_not_called()

    def test_missing_target_cancel_restores_config_without_delete_prompt(self):
        missing = self.home / "missing-prefix"
        self.window.prefix_edit.setText(str(missing))

        with mock.patch.object(
            QMessageBox, "question", return_value=QMessageBox.StandardButton.Cancel
        ) as question, mock.patch.object(
            self.window, "_prompt_old_environment_disposition"
        ) as disposition, mock.patch.object(
            self.state, "start_environment_transition"
        ) as start:
            result = self.window.save_config()

        self.assertIsNone(result)
        self.assertIn(str(missing), question.call_args.args[2])
        disposition.assert_not_called()
        start.assert_not_called()
        self.assertEqual(self.window.prefix_edit.text(), str(self.old_prefix))
        self.assertEqual(self.state.config["prefix"], str(self.old_prefix))

    def test_empty_target_initializes_and_preserves_old_on_explicit_preserve(self):
        empty = self.home / "empty-prefix"
        empty.mkdir()
        self.window.prefix_edit.setText(str(empty))

        with mock.patch.object(
            QMessageBox, "question", return_value=QMessageBox.StandardButton.Yes
        ), mock.patch.object(
            self.window, "_prompt_old_environment_disposition", return_value=False
        ) as disposition, mock.patch.object(
            self.state, "start_environment_transition"
        ) as start:
            self.window.save_config()

        disposition.assert_called_once_with(str(self.old_prefix))
        start.assert_called_once()
        self.assertEqual(start.call_args.args[0]["prefix"], str(empty))
        self.assertEqual(start.call_args.args[1:], (True, False))

    def test_existing_valid_target_skips_initialization_prompt(self):
        target = self.home / "existing-prefix"
        self._make_prefix(target)
        self.window.prefix_edit.setText(str(target))

        with mock.patch.object(QMessageBox, "question") as question, \
             mock.patch.object(
                 self.window, "_prompt_old_environment_disposition", return_value=False
             ), mock.patch.object(self.state, "start_environment_transition") as start:
            self.window.save_config()

        question.assert_not_called()
        self.assertEqual(start.call_args.args[1:], (False, False))

    def test_explicit_delete_choice_sets_delete_old(self):
        target = self._make_prefix(self.home / "existing-prefix")
        self.window.prefix_edit.setText(str(target))

        with mock.patch.object(
            self.window, "_prompt_old_environment_disposition", return_value=True
        ) as disposition, mock.patch.object(
            self.state, "start_environment_transition"
        ) as start:
            self.window.save_config()

        disposition.assert_called_once_with(self.config["prefix"])
        self.assertEqual(start.call_args.args[1:], (False, True))

    def test_old_environment_dialog_uses_explicit_labels_and_roles(self):
        results = {}
        for clicked_label in ("Preserve", "Delete permanently", "Cancel"):
            with self.subTest(clicked_label=clicked_label):
                dialog = mock.Mock()
                buttons = {
                    "Preserve": object(),
                    "Delete permanently": object(),
                    "Cancel": object(),
                }
                dialog.addButton.side_effect = [
                    buttons["Preserve"], buttons["Delete permanently"], buttons["Cancel"]
                ]
                dialog.clickedButton.return_value = buttons[clicked_label]
                with mock.patch.object(qt_module, "QMessageBox") as message_box:
                    message_box.Icon = QMessageBox.Icon
                    message_box.ButtonRole = QMessageBox.ButtonRole
                    message_box.StandardButton = QMessageBox.StandardButton
                    message_box.return_value = dialog
                    results[clicked_label] = (
                        self.window._prompt_old_environment_disposition(
                            self.config["prefix"]
                        )
                    )

                self.assertIn(self.config["prefix"], dialog.setText.call_args.args[0])
                self.assertIn("delete it permanently", dialog.setText.call_args.args[0])
                self.assertEqual(
                    dialog.addButton.call_args_list,
                    [
                        mock.call("Preserve", QMessageBox.ButtonRole.AcceptRole),
                        mock.call(
                            "Delete permanently", QMessageBox.ButtonRole.DestructiveRole
                        ),
                        mock.call(QMessageBox.StandardButton.Cancel),
                    ],
                )
                dialog.setDefaultButton.assert_called_once_with(buttons["Preserve"])

        self.assertIs(results["Preserve"], False)
        self.assertIs(results["Delete permanently"], True)
        self.assertIsNone(results["Cancel"])

    def test_old_environment_prompt_cancel_restores_saved_path(self):
        target = self._make_prefix(self.home / "existing-prefix")
        self.window.prefix_edit.setText(str(target))

        with mock.patch.object(
            self.window, "_prompt_old_environment_disposition", return_value=None
        ), mock.patch.object(self.state, "start_environment_transition") as start:
            self.window.save_config()

        start.assert_not_called()
        self.assertEqual(self.window.prefix_edit.text(), str(self.old_prefix))
        self.assertEqual(self.state.config["prefix"], str(self.old_prefix))

    def test_nonempty_invalid_target_is_rejected_without_any_prompt(self):
        target = self.home / "documents"
        target.mkdir()
        important = target / "important"
        important.write_text("keep")
        self.window.prefix_edit.setText(str(target))

        with mock.patch.object(QMessageBox, "question") as question, \
             mock.patch.object(QMessageBox, "warning") as warning, \
             mock.patch.object(self.window, "show_error") as show_error, \
             mock.patch.object(self.state, "start_environment_transition") as start:
            self.window.save_config()

        question.assert_not_called()
        warning.assert_not_called()
        start.assert_not_called()
        show_error.assert_called_once()
        self.assertEqual(important.read_text(), "keep")
        self.assertEqual(self.window.prefix_edit.text(), str(self.old_prefix))

    def test_transition_start_failure_restores_all_config_fields(self):
        target = self._make_prefix(self.home / "existing-prefix")
        self.window.prefix_edit.setText(str(target))
        self.window.wine_edit.setText("/edited/wine")

        with mock.patch.object(
            self.window, "_prompt_old_environment_disposition", return_value=False
        ), mock.patch.object(
            self.state, "start_environment_transition", side_effect=RuntimeError("busy")
        ), mock.patch.object(self.window, "show_error") as show_error:
            self.window.save_config()

        show_error.assert_called_once()
        self.assertEqual(self.window.prefix_edit.text(), self.config["prefix"])
        self.assertEqual(self.window.wine_edit.text(), self.config["wine"])
        self.assertFalse(self.window.pending_environment_transition)

    def test_finished_failed_transition_restores_edited_path(self):
        self.window.prefix_edit.setText(str(self.home / "failed-prefix"))
        self.window.pending_environment_transition = True
        with self.state.lock:
            self.state.task = {
                "running": False,
                "kind": "environment-switch",
                "status": "failed",
                "log": "ERROR: failed\n",
            }

        self.window.refresh_state()

        self.assertEqual(self.window.prefix_edit.text(), self.config["prefix"])
        self.assertFalse(self.window.pending_environment_transition)

    def test_initialization_and_switch_work_stays_off_qt_event_loop(self):
        target = self.home / "slow-prefix"
        self.window.prefix_edit.setText(str(target))

        def initialize(prefix, wine, recreate, output, cancel_event, process_callback):
            time.sleep(0.35)
            self._make_prefix(Path(prefix))
            return "ready"

        with mock.patch.object(
            QMessageBox, "question", return_value=QMessageBox.StandardButton.Yes
        ), mock.patch.object(
            self.window, "_prompt_old_environment_disposition", return_value=False
        ), mock.patch.object(backend, "create_environment", side_effect=initialize):
            started = time.monotonic()
            self.window.save_config()
            elapsed = time.monotonic() - started
            self.assertLess(elapsed, 0.15)
            self.assertTrue(self.state.snapshot()["task"]["running"])
            deadline = time.monotonic() + 2
            while self.state.snapshot()["task"]["running"] and time.monotonic() < deadline:
                time.sleep(0.01)

        self.window.refresh_state()
        self.assertEqual(self.state.config["prefix"], str(target))
        self.assertEqual(self.window.prefix_edit.text(), str(target))

    def test_close_during_staged_transition_waits_for_rollback(self):
        target = self._make_prefix(self.home / "replacement-prefix")
        staged = self.old_prefix.with_name(".old-staged-for-close")
        staged_ready = threading.Event()
        cancel_observed = threading.Event()
        allow_stage_return = threading.Event()

        def stage(old_value, new_value, wine_value, output):
            self.old_prefix.rename(staged)
            staged_ready.set()
            if self.state.cancel_event.wait(1):
                cancel_observed.set()
            allow_stage_return.wait(1)
            return staged

        with mock.patch.object(
            backend, "stage_environment_deletion", side_effect=stage
        ), mock.patch.object(
            self.window, "prompt_update_offer"
        ) as update_prompt, mock.patch.object(
            self.window, "close"
        ) as automatic_close:
            self.window.pending_environment_transition = True
            self.state.start_environment_transition(
                {"prefix": str(target)}, False, True
            )
            self.assertTrue(staged_ready.wait(1))

            close_event = QCloseEvent()
            self.window.closeEvent(close_event)

            self.assertFalse(close_event.isAccepted())
            self.assertTrue(cancel_observed.wait(1))
            self.assertTrue(self.state.snapshot()["task"]["running"])
            self.window.refresh_state()
            automatic_close.assert_not_called()

            allow_stage_return.set()
            deadline = time.monotonic() + 2
            while self.state.snapshot()["task"]["running"] and time.monotonic() < deadline:
                time.sleep(0.01)

            self.assertFalse(self.state.snapshot()["task"]["running"])
            self.assertEqual(self.state.snapshot()["task"]["status"], "cancelled")
            self.assertTrue(backend.has_wine_prefix_layout(self.old_prefix))
            self.assertFalse(staged.exists())
            with self.state.lock:
                self.state.updater["checked"] = True
                self.state.updater["offer"] = {
                    "id": "offer-during-close",
                    "metadata": {},
                    "updates": {},
                }
            self.window.refresh_state()
            self.application.processEvents()
            automatic_close.assert_called_once_with()
            update_prompt.assert_not_called()


if __name__ == "__main__":
    unittest.main()
