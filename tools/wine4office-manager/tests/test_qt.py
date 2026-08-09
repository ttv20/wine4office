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
    from PySide6.QtGui import QBrush, QCloseEvent
    from PySide6.QtWidgets import (
        QApplication,
        QCheckBox,
        QDialog,
        QLabel,
        QListWidget,
        QMessageBox,
        QStackedWidget,
        QTreeWidget,
    )
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
            "use_x11": True,
            "update_url": "",
            "include_prereleases": False,
            "office_telemetry_disabled": {},
            "automatic_update_checks": False,
            "automatic_update_checks_prompted": True,
            "incident_reporting_mode": "ask",
            "reliability_prompted": True,
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
        self.application.processEvents()

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

    def _preload_snapshot(self, task=None, **changes):
        snapshot = self.state.snapshot()
        preload = {
            "supported": True,
            "reason": "",
            "installed": False,
            "enabled": False,
            "active": False,
            "state": "unbound",
            "binding": None,
            "selected_matches": False,
            "components": {},
            "detail": "",
            "checking": False,
        }
        preload.update(changes)
        snapshot["preload"] = preload
        if task:
            snapshot["task"] = {**snapshot["task"], **task}
        return snapshot

    def _refresh_preload(self, task=None, **changes):
        snapshot = self._preload_snapshot(task=task, **changes)
        with mock.patch.object(self.state, "snapshot", return_value=snapshot):
            self.window.refresh_state()
        return snapshot

    def test_background_preload_controls_are_explicit_and_accessible(self):
        self.assertEqual(self.window.preload_group.title(), "Background services")
        notice = self.window.preload_notice_label.text()
        self.assertEqual(
            notice,
            "Start Office Click-to-Run and App-V services at login. "
            "Uses about 300–600 MB of RAM and makes Office apps open 2–3× faster.",
        )
        self.assertTrue(self.window.preload_notice_label.accessibleName())
        controls = (
            (self.window.preload_enable_button, "Enable && start"),
            (self.window.preload_stop_button, "Stop && disable"),
        )
        for button, text in controls:
            with self.subTest(text=text):
                self.assertEqual(button.text(), text)
                self.assertTrue(button.accessibleName())
                self.assertTrue(button.toolTip())
        self.assertFalse(hasattr(self.window, "preload_disable_button"))
        self.assertFalse(hasattr(self.window, "preload_start_button"))
        self.assertTrue(self.window.preload_selected_label.accessibleName())
        self.assertTrue(self.window.preload_binding_label.accessibleName())
        self.assertFalse(hasattr(self.window, "preload_state_label"))
        self.assertFalse(any(
            "immediate" in checkbox.text().lower()
            for checkbox in self.window.preload_group.findChildren(QCheckBox)
        ))

    def test_background_preload_state_matrix_and_lifecycle_controls(self):
        binding = {
            "prefix": self.config["prefix"],
            "wine": self.config["wine"],
        }
        cases = (
            ("unbound", {}, (True, False)),
            (
                "disabled",
                {"installed": True, "binding": binding, "selected_matches": True},
                (True, False),
            ),
            (
                "enabled",
                {
                    "installed": True,
                    "enabled": True,
                    "binding": binding,
                    "selected_matches": True,
                },
                (True, True),
            ),
            (
                "active",
                {
                    "installed": True,
                    "enabled": True,
                    "active": True,
                    "binding": binding,
                    "selected_matches": True,
                    "components": {
                        "ClickToRunSvc": {"state": "running", "owned": True, "detail": ""},
                        "AppV": {"state": "running", "owned": True, "detail": ""},
                    },
                },
                (False, True),
            ),
            (
                "degraded",
                {
                    "installed": True,
                    "enabled": True,
                    "active": True,
                    "binding": binding,
                    "selected_matches": True,
                    "components": {
                        "ClickToRunSvc": {"state": "failed", "owned": True, "detail": "failed"},
                        "AppV": {"state": "stopped", "owned": False, "detail": "failed"},
                    },
                },
                (False, True),
            ),
        )
        buttons = (
            self.window.preload_enable_button,
            self.window.preload_stop_button,
        )
        for state, values, enabled in cases:
            with self.subTest(state=state):
                self._refresh_preload(state=state, **values)
                self.assertEqual(
                    tuple(button.isEnabled() for button in buttons),
                    enabled,
                )

    def test_background_preload_binding_mismatch_shows_both_environments(self):
        bound = str(self.home / "other-office")
        self._refresh_preload(
            state="binding_mismatch",
            installed=True,
            enabled=True,
            active=True,
            binding={"prefix": bound, "wine": "/other/wine"},
            selected_matches=False,
        )

        self.assertEqual(self.window.preload_selected_label.text(), self.config["prefix"])
        self.assertEqual(self.window.preload_binding_label.text(), bound)
        self.assertIn(self.config["prefix"], self.window.preload_detail_label.text())
        self.assertIn(bound, self.window.preload_detail_label.text())
        self.assertIn("Stop & disable", self.window.preload_detail_label.text())
        self.assertFalse(self.window.preload_enable_button.isEnabled())
        self.assertTrue(self.window.preload_stop_button.isEnabled())

    def test_inactive_mismatch_rebind_requires_explicit_confirmation(self):
        bound = str(self.home / "retired-office")
        self._refresh_preload(
            state="binding_mismatch",
            installed=True,
            enabled=False,
            active=False,
            binding={"prefix": bound, "wine": "/retired/wine"},
            selected_matches=False,
        )

        self.assertTrue(self.window.preload_enable_button.isEnabled())
        self.assertFalse(self.window.preload_stop_button.isEnabled())
        self.assertIn("after confirmation", self.window.preload_detail_label.text())

        with mock.patch.object(
            QMessageBox,
            "question",
            side_effect=(
                QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Yes,
            ),
        ) as question, mock.patch.object(
            self.state, "start_preload_action"
        ) as start, mock.patch.object(
            self.window, "refresh_state"
        ):
            self.window.preload_enable_button.click()
            start.assert_not_called()
            self.window.preload_enable_button.click()

        start.assert_called_once_with("enable-start")
        self.assertEqual(question.call_count, 2)
        self.assertEqual(question.call_args.args[1], "Replace background service binding")
        prompt = question.call_args.args[2]
        self.assertIn(bound, prompt)
        self.assertIn(self.config["prefix"], prompt)
        self.assertIn("enable and start", prompt)


    def test_background_preload_unsupported_has_no_fallback_or_controls(self):
        reason = "The systemd user manager is unavailable."
        self._refresh_preload(
            supported=False,
            reason=reason,
            state="unsupported",
        )

        self.assertEqual(
            self.window.preload_detail_label.text(),
            "Systemd user services are unavailable.",
        )
        self.assertTrue(all(
            not button.isEnabled()
            for button in (
                self.window.preload_enable_button,
                self.window.preload_stop_button,
            )
        ))

    def test_enable_installs_when_unit_is_missing_despite_stale_binding(self):
        self._refresh_preload(
            installed=False,
            enabled=False,
            active=False,
            state="mismatch",
            binding={"prefix": "/old/prefix", "wine": "/old/runner/bin/wine"},
            selected_matches=False,
        )

        self.assertEqual(
            self.window.preload_detail_label.text(),
            "Click Enable & start to use background services.",
        )
        self.assertTrue(self.window.preload_enable_button.isEnabled())

    def test_background_preload_controls_disable_during_checks_and_tasks(self):
        values = {
            "state": "active",
            "installed": True,
            "enabled": True,
            "active": True,
            "binding": {
                "prefix": self.config["prefix"],
                "wine": self.config["wine"],
            },
            "selected_matches": True,
        }
        buttons = (
            self.window.preload_enable_button,
            self.window.preload_stop_button,
        )
        self._refresh_preload(checking=True, detail="Services are ready.", **values)
        self.assertEqual(self.window.preload_detail_label.text(), "Services are ready.")
        self.assertFalse(self.window.preload_enable_button.isEnabled())
        self.assertTrue(self.window.preload_stop_button.isEnabled())

        self._refresh_preload(
            task={"running": True, "kind": "other", "status": "running"},
            **values,
        )
        self.assertTrue(all(not button.isEnabled() for button in buttons))

    def test_background_preload_refresh_changes_only_ram_number(self):
        values = {
            "state": "active",
            "installed": True,
            "enabled": True,
            "active": True,
            "binding": {
                "prefix": self.config["prefix"],
                "wine": self.config["wine"],
            },
            "selected_matches": True,
        }
        self._refresh_preload(memory_bytes=600 * 1024 * 1024, **values)
        stop_enabled = self.window.preload_stop_button.isEnabled()

        self._refresh_preload(
            checking=True, memory_bytes=612 * 1024 * 1024, **values
        )

        self.assertEqual(self.window.preload_memory_label.text(), "612 MB")
        self.assertEqual(self.window.preload_stop_button.isEnabled(), stop_enabled)

    def test_background_preload_buttons_dispatch_exact_manager_actions(self):
        controls = (
            (self.window.preload_enable_button, "enable-start"),
            (self.window.preload_stop_button, "stop-disable"),
        )
        self.window.preload_rebind = None
        with mock.patch.object(self.window, "refresh_state"):
            for button, action in controls:
                with self.subTest(action=action), mock.patch.object(
                    self.state, "start_preload_action"
                ) as start:
                    button.setEnabled(True)
                    button.click()
                    start.assert_called_once_with(action)

    def test_failed_preload_stop_is_visible_and_offers_safe_disable(self):
        snapshot = self._preload_snapshot(
            task={
                "running": False,
                "kind": "preload-stop-disable",
                "status": "failed",
                "log": "ERROR: Office is active; refusing to stop preload",
            },
            state="degraded",
            installed=True,
            enabled=True,
            active=True,
            binding={
                "prefix": self.config["prefix"],
                "wine": self.config["wine"],
            },
            selected_matches=True,
        )
        self.window.last_task_state = "True:running"
        with mock.patch.object(
            self.state, "snapshot", return_value=snapshot
        ), mock.patch.object(
            qt_module.QTimer, "singleShot"
        ) as single_shot, mock.patch.object(
            self.window, "show_error"
        ) as show_error:
            self.window.refresh_state()
            single_shot.assert_called_once()
            single_shot.call_args.args[1]()

        show_error.assert_called_once()
        message = str(show_error.call_args.args[0])
        self.assertIn("Office is active", message)
        self.assertIn("Close Office", message)
        self.assertNotIn("Disable at login", message)

    def test_completed_manager_update_prompts_for_restart_once(self):
        snapshot = self._preload_snapshot(task={
            "running": False,
            "kind": "update",
            "status": "completed",
            "restart_required": True,
        })
        self.window.last_task_state = "True:running"
        with mock.patch.object(
            self.state, "snapshot", return_value=snapshot
        ), mock.patch.object(
            qt_module.QTimer, "singleShot"
        ) as single_shot, mock.patch.object(
            self.window, "prompt_manager_restart"
        ) as prompt:
            self.window.refresh_state()
            restart_calls = [
                call for call in single_shot.call_args_list
                if call.args[1].__name__ == "<lambda>"
            ]
            restart_calls[-1].args[1]()
            self.window.refresh_state()

        prompt.assert_called_once_with(True)
        self.assertTrue(self.window.restart_prompted)

    def test_restart_now_replaces_current_manager_process(self):
        command = ["/updated/Wine4OfficeManager"]
        self.window.restart_command = command
        with mock.patch.object(
            qt_module.QMessageBox, "question",
            return_value=QMessageBox.StandardButton.Yes,
        ), mock.patch.object(qt_module.os, "execvpe") as execvpe:
            self.window.prompt_manager_restart(True)

        execvpe.assert_called_once_with(command[0], command, mock.ANY)
        self.assertFalse(self.window.timer.isActive())

    def test_failed_restart_keeps_current_manager_open(self):
        self.window.restart_command = ["/missing/Wine4OfficeManager"]
        with mock.patch.object(
            qt_module.os, "execvpe", side_effect=FileNotFoundError("missing")
        ), mock.patch.object(self.window, "show_error") as show_error, \
             mock.patch.object(self.window, "close") as close:
            self.window.restart_manager()

        show_error.assert_called_once()
        close.assert_not_called()
        self.assertTrue(self.window.timer.isActive())

    def test_successful_removal_shows_confirmation_then_closes_manager(self):
        snapshot = self._preload_snapshot(task={
            "running": False,
            "kind": "remove",
            "status": "completed",
        })
        self.window.last_task_state = "True:running"
        with mock.patch.object(
            self.state, "snapshot", return_value=snapshot
        ), mock.patch.object(
            qt_module.QTimer, "singleShot"
        ) as single_shot, mock.patch.object(
            qt_module.QMessageBox, "information"
        ) as information, mock.patch.object(
            self.window, "close"
        ) as close:
            self.window.refresh_state()
            single_shot.assert_called_once_with(
                0, self.window.finish_successful_removal
            )
            single_shot.call_args.args[1]()

        information.assert_called_once_with(
            self.window,
            "Wine4Office Uninstalled",
            "Wine4Office was uninstalled successfully.",
        )
        close.assert_called_once()
        self.assertTrue(self.window._automatic_close)

    def test_failed_removal_keeps_manager_open_without_success_confirmation(self):
        snapshot = self._preload_snapshot(task={
            "running": False,
            "kind": "remove",
            "status": "failed",
            "log": "ERROR: removal failed",
        })
        self.window.last_task_state = "True:running"
        with mock.patch.object(
            self.state, "snapshot", return_value=snapshot
        ), mock.patch.object(
            qt_module.QMessageBox, "information"
        ) as information, mock.patch.object(
            self.window, "close"
        ) as close:
            self.window.refresh_state()

        information.assert_not_called()
        close.assert_not_called()
        self.assertFalse(self.window._automatic_close)



    def test_ui_uses_system_theme_and_native_navigation_controls(self):
        self.assertEqual(self.window.styleSheet(), "")
        self.assertEqual(self.window.windowTitle(), "Wine4Office Manager")
        self.assertIsInstance(self.window.navigation, QListWidget)
        self.assertIsInstance(self.window.pages, QStackedWidget)
        self.assertIsInstance(self.window.app_tree, QTreeWidget)
        self.assertEqual(self.window.navigation.count(), 6)
        self.assertEqual(
            self.window.navigation.item(
                self.window.OFFICE_SETTINGS_PAGE
            ).text(),
            "Office settings",
        )
        self.assertIn("teams", self.window.app_items)
        self.assertEqual(
            self.window.app_items["teams"].text(0), "Microsoft Teams"
        )
        self.assertEqual(
            set(self.window.app_items), set(backend.APP_META)
        )
        self.assertEqual(self.window.app_tree.columnCount(), 3)
        self.assertEqual(
            self.window.app_tree.headerItem().text(2), "Compatibility"
        )
        expected_compatibility = {
            "word": "Good",
            "excel": "Good",
            "powerpoint": "Open",
            "outlook": "Not working",
            "access": "Not tested",
            "onenote": "Not tested",
            "publisher": "Not tested",
            "visio": "Not tested",
            "project": "Not tested",
            "teams": "Basic functionality",
            "setlang": "Good",
        }
        for app, compatibility in expected_compatibility.items():
            self.assertEqual(self.window.app_items[app].text(2), compatibility)
        self.assertEqual(self.window.app_items["teams"].text(1), "Not installed")
        self.assertNotEqual(
            self.window.app_items["teams"].foreground(0), QBrush()
        )
        self.assertEqual(self.window.app_items["word"].foreground(0), QBrush())

    def test_first_launch_combines_reporting_and_24_hour_update_choice(self):
        with self.state.lock:
            self.state.config["reliability_prompted"] = False
        enabled = {
            **self.state.config,
            "automatic_update_checks": True,
            "automatic_update_checks_prompted": True,
            "incident_reporting_mode": "ask",
            "reliability_prompted": True,
        }
        dialog, ask, updates = self.window._reliability_dialog()
        self.assertTrue(ask.isChecked())
        self.assertTrue(updates.isChecked())
        self.assertIn("never downloaded or installed automatically", updates.toolTip())
        self.window.reporting_available = True
        with mock.patch.object(dialog, "exec", return_value=QDialog.DialogCode.Accepted), \
             mock.patch.object(
                 self.window, "_reliability_dialog", return_value=(dialog, ask, updates)
             ), mock.patch.object(
                 self.state, "set_reliability_preferences", return_value=enabled
             ) as save, mock.patch.object(
                 self.window, "_set_config_fields"
             ), mock.patch.object(self.window, "_route_after_first_open") as route:
            self.window.prompt_reliability_on_first_launch()

        save.assert_called_once_with("ask", True)
        route.assert_called_once_with()

    def test_startup_finishes_reliability_choice_before_other_prompts(self):
        incident_path = self.home / "incident.json"
        self.window.initial_incident = incident_path
        calls = []
        with mock.patch.object(
            self.window, "prompt_reliability_on_first_launch",
            side_effect=lambda: calls.append("reliability"),
        ), mock.patch.object(
            self.window, "start_background_update_check",
            side_effect=lambda: calls.append("updates"),
        ), mock.patch.object(
            self.window, "review_incident",
            side_effect=lambda path: calls.append(("incident", path)),
        ):
            self.window.finish_startup()

        self.assertEqual(
            calls,
            ["reliability", "updates", ("incident", incident_path)],
        )

    def test_build_without_openobserve_environment_hides_reporting_controls(self):
        self.assertFalse(self.window.reporting_available)
        self.assertFalse(hasattr(self.window, "incident_ask"))

    def test_background_update_checkbox_explains_manager_open_check_remains(self):
        self.assertFalse(self.window.automatic_update_checks.isChecked())
        self.assertIn(
            "still checks whenever you open it",
            self.window.automatic_update_checks.toolTip(),
        )
        enabled = {
            **self.state.config,
            "automatic_update_checks": True,
            "automatic_update_checks_prompted": True,
        }
        with mock.patch.object(
            self.state, "set_automatic_update_checks", return_value=enabled
        ) as set_checks:
            self.window.automatic_update_checks.setChecked(True)

        set_checks.assert_called_once_with(True, prompted=True)

    def test_manager_open_check_runs_even_when_background_schedule_is_disabled(self):
        with self.state.lock:
            self.state.config["automatic_update_checks"] = False
        with mock.patch.object(self.state, "start_update_check") as check:
            self.window.start_background_update_check()
        check.assert_called_once_with()

    def test_combined_update_prompt_installs_once_and_opens_maintenance(self):
        offer = {
            "id": "manager:0.1.6|wine:11.14",
            "updates": {
                "manager": {"version": "0.1.6"},
                "wine": {"version": "11.14"},
            },
        }
        install_button = object()
        later_button = object()
        skip_button = object()
        with mock.patch.object(qt_module, "QMessageBox") as message_box, \
             mock.patch.object(self.state, "start_offered_update") as start, \
             mock.patch.object(self.window, "_show_update_progress") as progress, \
             mock.patch.object(self.window, "refresh_state"):
            message_box.Icon = QMessageBox.Icon
            message_box.ButtonRole = QMessageBox.ButtonRole
            dialog = message_box.return_value
            dialog.addButton.side_effect = (
                install_button,
                later_button,
                skip_button,
            )
            dialog.clickedButton.return_value = install_button

            self.window.prompt_update_offer(offer)

        self.assertEqual(dialog.exec.call_count, 1)
        self.assertIn(
            "Wine4Office Manager: 0.1.6",
            dialog.setInformativeText.call_args.args[0],
        )
        self.assertIn(
            "Wine runner: 11.14",
            dialog.setInformativeText.call_args.args[0],
        )
        start.assert_called_once_with(["manager", "wine"])
        progress.assert_called_once_with(offer, ["manager", "wine"])
        self.assertEqual(
            self.window.navigation.currentRow(),
            self.window.MAINTENANCE_PAGE,
        )

    def test_update_progress_popup_tracks_download_and_completion(self):
        offer = {
            "updates": {"manager": {"version": "0.2.0-rc.1"}},
        }
        self.window._show_update_progress(offer, ["manager"])
        self.application.processEvents()

        self.assertTrue(self.window.update_progress_dialog.isVisible())
        self.assertTrue(any(
            "0.2.0-rc.1" in label.text()
            for label in self.window.update_progress_dialog.findChildren(QLabel)
        ))
        self.window._refresh_update_progress({
            "kind": "update",
            "running": True,
            "status": "running",
            "log": "Downloading the approved update\n",
            "progress_label": "Downloading Wine4Office Manager 0.2.0-rc.1",
            "progress_value": 42,
        })
        self.assertEqual(self.window.update_progress_bar.value(), 42)
        self.assertIn("Downloading", self.window.update_progress_status.text())
        self.assertIn(
            "approved update", self.window.update_progress_log.toPlainText()
        )

        self.window._refresh_update_progress({
            "kind": "update",
            "running": False,
            "status": "completed",
            "log": "Installed.\n",
            "progress_label": "Updates installed",
            "progress_value": 100,
            "restart_required": False,
        })
        self.assertEqual(self.window.update_progress_bar.value(), 100)
        self.assertEqual(self.window.update_progress_button.text(), "Close")
        self.window.update_progress_dialog.accept()

    def test_x11_checkbox_defaults_checked_and_persists_native_wayland_choice(self):
        self.assertTrue(self.window.use_x11.isChecked())
        self.assertIn("X11", self.window.use_x11.text())
        self.assertIn("native Wayland", self.window.use_x11.text())

        self.window.use_x11.setChecked(False)
        saved = self.window.save_config()

        self.assertFalse(saved["use_x11"])
        self.assertFalse(self.state.snapshot()["config"]["use_x11"])

    def test_prerelease_updates_are_opt_in_and_persisted(self):
        self.assertFalse(self.window.include_prereleases.isChecked())
        self.window.include_prereleases.setChecked(True)

        saved = self.window.save_config()

        self.assertTrue(saved["include_prereleases"])
        self.assertTrue(self.state.snapshot()["config"]["include_prereleases"])
        self.assertIn("test versions", self.window.include_prereleases.toolTip())

    def test_privacy_dialog_is_accessible_and_persists_telemetry(self):
        dialog, checkbox = self.window._privacy_settings_dialog(self.state.config)
        self.assertFalse(checkbox.isChecked())
        self.assertIn("telemetry policy", checkbox.accessibleName().lower())
        self.assertIn("Neither", checkbox.accessibleDescription())
        self.assertIn("Required service data", checkbox.toolTip())
        checkbox.setChecked(True)
        dialog.exec = mock.Mock(return_value=QDialog.DialogCode.Accepted)

        with mock.patch.object(
            self.window, "_privacy_settings_dialog",
            return_value=(dialog, checkbox),
        ), mock.patch.object(
            backend, "apply_office_telemetry_policy"
        ) as apply:
            self.window.configure_privacy_settings()

        self.assertTrue(
            backend.office_telemetry_disabled(self.state.snapshot()["config"])
        )
        apply.assert_called_once()
        self.assertIn(
            "Telemetry disabled",
            self.window.privacy_settings_button.description(),
        )

    def test_office_policy_summaries_track_selected_environment(self):
        enabled = self._make_prefix(self.home / "telemetry-disabled-prefix")
        other = self._make_prefix(self.home / "default-policy-prefix")
        self.state.config = backend.set_office_telemetry_disabled(
            self.state.config, enabled, True
        )

        self.window.prefix_edit.setText(str(enabled))
        self.assertIn(
            "Telemetry disabled",
            self.window.privacy_settings_button.description(),
        )
        self.window.prefix_edit.setText(str(other))
        self.assertIn(
            "Office default",
            self.window.privacy_settings_button.description(),
        )

    def test_compatibility_dialog_applies_curated_per_prefix_settings(self):
        current = backend.office_compatibility_settings(self.state.config)
        dialog, by_id = self.window._compatibility_settings_dialog(current)
        by_id["disable_animations"].setChecked(True)
        by_id["skip_start_screen"].setChecked(True)
        dialog.exec = mock.Mock(return_value=QDialog.DialogCode.Accepted)

        with mock.patch.object(
            self.window, "_compatibility_settings_dialog",
            return_value=(dialog, by_id),
        ), mock.patch.object(
            backend, "apply_office_compatibility_policies"
        ) as apply:
            self.window.configure_compatibility_settings()

        saved = backend.office_compatibility_settings(
            self.state.snapshot()["config"]
        )
        self.assertTrue(saved["disable_animations"])
        self.assertTrue(saved["skip_start_screen"])
        self.assertFalse(saved["disable_hardware_acceleration"])
        apply.assert_called_once()
        self.assertIn(
            "2 managed",
            self.window.compatibility_settings_button.description(),
        )

    def test_environment_creation_reapplies_checked_telemetry_policy(self):
        prefix = Path(self.config["prefix"])
        config = backend.set_office_telemetry_disabled(self.config, prefix, True)
        with mock.patch.object(
            backend, "create_environment", return_value="environment ready"
        ) as create, mock.patch.object(
            backend, "apply_office_telemetry_policy"
        ) as apply:
            result = self.window._create_environment(config, True)

        self.assertEqual(result, "environment ready")
        create.assert_called_once_with(
            config["prefix"], config["wine"], True, self.state.output
        )
        apply.assert_called_once_with(
            config["prefix"], config["wine"], True, use_x11=True
        )

    def test_telemetry_policy_failure_is_shown_and_not_saved(self):
        dialog, checkbox = self.window._privacy_settings_dialog(self.state.config)
        checkbox.setChecked(True)
        dialog.exec = mock.Mock(return_value=QDialog.DialogCode.Accepted)

        with mock.patch.object(
            backend, "apply_office_telemetry_policy",
            side_effect=RuntimeError("policy command failed"),
        ), mock.patch.object(
            self.window, "_privacy_settings_dialog",
            return_value=(dialog, checkbox),
        ), mock.patch.object(self.window, "show_error") as show_error:
            self.window.configure_privacy_settings()

        self.assertFalse(backend.office_telemetry_disabled(self.state.config))
        show_error.assert_called_once()
        self.assertIn("policy command failed", str(show_error.call_args.args[0]))

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
        create.assert_called_once_with(
            ["word", "excel", "powerpoint"],
            self.config["prefix"],
            self.config["wine"],
            self.config["desktop_copy"],
            helper=self.window.font_helper,
        )

    def test_slow_application_launch_does_not_block_qt_and_gates_actions(self):
        self.window.app_items["word"].setCheckState(0, Qt.CheckState.Checked)
        self.window.use_x11.setChecked(False)

        def slow_launch(*args, **kwargs):
            time.sleep(0.35)
            return mock.Mock(pid=4321)

        with mock.patch.object(
            qt_module.subprocess, "Popen", side_effect=slow_launch
        ) as launch:
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
        self.assertIn("Application supervisor started (PID 4321)", task["log"])
        self.assertIn(
            [str(self.window.launcher), "word"],
            [call.args[0] for call in launch.call_args_list],
        )
        self.assertFalse(self.state.config["use_x11"])

    def test_wine_tool_launch_propagates_native_wayland_choice(self):
        self.window.use_x11.setChecked(False)
        with mock.patch.object(backend, "launch_tool", return_value=5678) as launch:
            self.window.launch_tool("winecfg")
            deadline = time.monotonic() + 2
            while self.state.snapshot()["task"]["running"] and time.monotonic() < deadline:
                time.sleep(0.01)

        launch.assert_called_once()
        self.assertFalse(launch.call_args.kwargs["use_x11"])

    def test_stop_wine_shows_persistent_status_bar_message(self):
        with mock.patch.object(
            self.state, "start_task"
        ), mock.patch.object(
            self.window.statusBar(), "showMessage"
        ) as show_message:
            self.window.launch_tool("stop")

        show_message.assert_any_call("Stopping Wine environment…", 0)

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

    def test_office_install_page_defaults_and_product_metadata(self):
        self.assertEqual(
            self.window.navigation.item(self.window.INSTALL_PAGE).text(),
            "Install Office & Teams",
        )
        self.assertEqual(
            self.window.teams_install_button.accessibleName(),
            "Install Microsoft Teams with the standalone installer",
        )
        self.assertEqual(self.window.office_languages_edit.text(), "en-US")
        self.assertEqual(
            self.window.office_product_combo.count(),
            len(backend.OFFICE_PRODUCTS),
        )
        self.assertTrue({
            "VisioProRetail",
            "ProjectProRetail",
            "VisioPro2024Volume",
            "ProjectPro2024Volume",
        }.issubset({product["product_id"] for product in backend.OFFICE_PRODUCTS}))

        for index, product in enumerate(backend.OFFICE_PRODUCTS):
            with self.subTest(product_id=product["product_id"]):
                self.assertEqual(
                    self.window.office_product_combo.itemText(index),
                    product["label"],
                )
                self.assertEqual(
                    self.window.office_product_combo.itemData(index),
                    product,
                )
                self.window.office_product_combo.setCurrentIndex(index)
                details = self.window.office_product_details.text()
                self.assertIn(product["product_id"], details)
                self.assertIn(product["channel"], details)

    def test_teams_install_downloads_and_runs_without_save_dialog(self):
        original_page = self.window.pages.currentIndex()
        with mock.patch.object(
            qt_module.QFileDialog, "getSaveFileName"
        ) as save_dialog, mock.patch.object(
            self.window, "save_config", return_value=dict(self.config)
        ), mock.patch.object(
            backend, "install_teams_with_bootstrapper", return_value="installed"
        ) as install, mock.patch.object(
            self.state, "start_task"
        ) as start:
            self.window.install_teams()
            start.call_args.args[1]()

        save_dialog.assert_not_called()
        start.assert_called_once()
        self.assertEqual(start.call_args.args[0], "teams-install")
        install.assert_called_once_with(
            self.config["prefix"],
            self.config["wine"],
            self.state.output,
            cancel_event=self.state.cancel_event,
            process_callback=self.state.set_process,
            progress_callback=self.state.set_progress,
            use_x11=True,
        )
        self.assertEqual(self.window.pages.currentIndex(), original_page)
        self.assertTrue(self.window.update_progress_dialog.isVisible())
        self.assertEqual(
            self.window.update_progress_dialog.windowTitle(),
            "Installing Microsoft Teams",
        )
        self.assertEqual(self.window.update_progress_task_kind, "teams-install")

    def test_generated_office_install_starts_without_save_dialog(self):
        generated_xml = "<Configuration><Add /></Configuration>\n"
        configuration_payload = generated_xml.encode("utf-8")

        with mock.patch.object(
            qt_module.QFileDialog, "getSaveFileName"
        ) as save_dialog, mock.patch.object(
            backend, "install_office_with_odt", return_value="installed"
        ) as install, mock.patch.object(
            self.window, "save_config", return_value=dict(self.config)
        ), mock.patch.object(
            backend, "validate_office_languages", return_value=["en-us"]
        ) as validate_languages, mock.patch.object(
            backend, "build_office_configuration", return_value=generated_xml
        ) as build, mock.patch.object(
            self.state, "start_task"
        ) as start:
            self.window.install_office_from_generated_xml()
            start.call_args.args[1]()

        save_dialog.assert_not_called()
        validate_languages.assert_called_with("en-US")
        build.assert_called_with("O365ProPlusRetail", ["en-us"])
        start.assert_called_once()
        self.assertEqual(start.call_args.args[0], "odt-install")
        install.assert_called_once_with(
            self.config["prefix"],
            self.config["wine"],
            None,
            self.state.output,
            cancel_event=self.state.cancel_event,
            process_callback=self.state.set_process,
            configuration_payload=configuration_payload,
            installer_started_callback=self.state.mark_foreground_ready,
        )
        self.assertIsNone(self.window.pending_odt_xml)

    def test_office_startup_popup_is_indeterminate_until_installer_starts(self):
        self.window._show_office_startup_progress()
        self.application.processEvents()

        self.assertTrue(self.window.office_startup_dialog.isVisible())
        self.assertEqual(self.window.office_startup_bar.minimum(), 0)
        self.assertEqual(self.window.office_startup_bar.maximum(), 0)
        self.window._refresh_office_startup_progress({
            "kind": "odt-install",
            "running": True,
            "foreground_ready": False,
        })
        self.assertIsNotNone(self.window.office_startup_dialog)

        self.window._refresh_office_startup_progress({
            "kind": "odt-install",
            "running": True,
            "foreground_ready": True,
        })
        self.assertIsNone(self.window.office_startup_dialog)
        self.assertEqual(
            self.window.statusBar().currentMessage(), "Office installer opened."
        )

    def test_custom_office_install_waits_for_open_selection(self):
        configuration = self.home / "custom deployment.xml"
        configuration.write_text("<Configuration />", encoding="utf-8")
        configuration_payload = configuration.read_bytes()
        changed_payload = b"<Configuration><Display Level=\"Full\" /></Configuration>"
        config_digest = "b" * 64

        with mock.patch.object(
            qt_module.QFileDialog,
            "getOpenFileName",
            side_effect=[("", ""), (str(configuration), "Office deployment XML (*.xml)")],
        ), mock.patch.object(
            backend,
            "load_office_configuration",
            return_value=(
                configuration.resolve(),
                configuration_payload,
                config_digest,
            ),
        ) as load, mock.patch.object(
            self.window, "save_config", return_value=dict(self.config)
        ), mock.patch.object(
            backend, "install_office_with_odt", return_value="installed"
        ) as install, mock.patch.object(
            self.state, "start_task"
        ) as start:
            self.window.install_office_from_custom_xml()
            start.assert_not_called()
            load.assert_not_called()

            self.window.install_office_from_custom_xml()
            configuration.write_bytes(changed_payload)
            start.call_args.args[1]()

        load.assert_called_once_with(configuration)
        start.assert_called_once()
        self.assertEqual(start.call_args.args[0], "odt-install")
        self.assertEqual(
            install.call_args.kwargs["configuration_payload"],
            configuration_payload,
        )
        self.assertEqual(install.call_args.args[2], configuration.resolve())
        self.assertEqual(configuration.read_bytes(), changed_payload)
        self.assertEqual(
            self.window.pending_odt_xml,
            (configuration.resolve(), configuration_payload, config_digest),
        )

    def test_completed_odt_install_offers_xml_cleanup_once(self):
        configuration = self.home / "completed deployment.xml"
        configuration.write_text("<Configuration />", encoding="utf-8")
        config_digest = "c" * 64
        self.window.pending_odt_xml = (
            configuration,
            configuration.read_bytes(),
            config_digest,
        )
        with self.state.lock:
            self.state.task = {
                "running": False,
                "kind": "odt-install",
                "status": "completed",
                "log": "Office installation completed successfully.\n",
            }

        with mock.patch.object(
            qt_module.QTimer, "singleShot"
        ) as single_shot, mock.patch.object(
            self.window, "prompt_office_xml_cleanup"
        ) as cleanup:
            self.window.refresh_state()
            self.window.refresh_state()
            single_shot.assert_called_once()
            single_shot.call_args.args[1]()

        cleanup.assert_called_once_with(configuration, config_digest)
        self.assertIsNone(self.window.pending_odt_xml)
        self.assertTrue(configuration.exists())

    def test_office_xml_cleanup_defaults_to_keep_and_deletes_only_on_explicit_choice(self):
        for choice in ("Keep", "Delete"):
            with self.subTest(choice=choice):
                configuration = self.home / f"{choice.lower()} deployment.xml"
                configuration.write_text("<Configuration />", encoding="utf-8")
                expected_digest = ("a" if choice == "Keep" else "b") * 64
                dialog = mock.Mock()
                keep_button = object()
                delete_button = object()
                dialog.addButton.side_effect = [keep_button, delete_button]
                dialog.clickedButton.return_value = (
                    keep_button if choice == "Keep" else delete_button
                )

                with mock.patch.object(
                    backend, "office_configuration_digest", return_value=expected_digest
                ), mock.patch.object(
                    backend,
                    "delete_office_configuration_if_unchanged",
                    return_value=(True, None),
                ) as delete, mock.patch.object(
                    qt_module, "QMessageBox"
                ) as message_box:
                    message_box.Icon = QMessageBox.Icon
                    message_box.ButtonRole = QMessageBox.ButtonRole
                    message_box.return_value = dialog
                    self.window.prompt_office_xml_cleanup(
                        configuration, expected_digest
                    )

                self.assertEqual(
                    dialog.addButton.call_args_list,
                    [
                        mock.call("Keep", QMessageBox.ButtonRole.AcceptRole),
                        mock.call("Delete", QMessageBox.ButtonRole.DestructiveRole),
                    ],
                )
                dialog.setDefaultButton.assert_called_once_with(keep_button)
                dialog.setEscapeButton.assert_called_once_with(keep_button)
                dialog.exec.assert_called_once_with()
                if choice == "Keep":
                    delete.assert_not_called()
                else:
                    delete.assert_called_once_with(configuration, expected_digest)
                self.assertTrue(configuration.exists())

    def test_office_xml_cleanup_refuses_changed_or_missing_configuration(self):
        expected_digest = "d" * 64
        cases = (
            ("changed", "e" * 64),
            ("missing", FileNotFoundError("configuration is missing")),
        )
        for condition, digest_result in cases:
            with self.subTest(condition=condition):
                configuration = self.home / f"{condition} deployment.xml"
                if condition == "changed":
                    configuration.write_text("<Configuration />", encoding="utf-8")
                digest = (
                    mock.patch.object(
                        backend,
                        "office_configuration_digest",
                        side_effect=digest_result,
                    )
                    if isinstance(digest_result, Exception)
                    else mock.patch.object(
                        backend,
                        "office_configuration_digest",
                        return_value=digest_result,
                    )
                )

                with digest, mock.patch.object(
                    QMessageBox, "information"
                ) as information, mock.patch.object(
                    backend, "delete_office_configuration_if_unchanged"
                ) as delete:
                    self.window.prompt_office_xml_cleanup(
                        configuration, expected_digest
                    )

                information.assert_called_once()
                self.assertEqual(
                    information.call_args.args[1],
                    "Deployment configuration changed",
                )
                self.assertIn(
                    str(configuration),
                    information.call_args.args[2],
                )
                delete.assert_not_called()
                if condition == "changed":
                    self.assertTrue(configuration.exists())

    def test_failed_or_cancelled_odt_install_keeps_xml_without_prompt(self):
        for status in ("failed", "cancelled"):
            with self.subTest(status=status):
                configuration = self.home / f"{status} deployment.xml"
                configuration.write_text("<Configuration />", encoding="utf-8")
                config_digest = status[0] * 64
                self.window.pending_odt_xml = (
                    configuration,
                    configuration.read_bytes(),
                    config_digest,
                )
                with self.state.lock:
                    self.state.task = {
                        "running": False,
                        "kind": "odt-install",
                        "status": status,
                        "log": f"{status}\n",
                    }

                with mock.patch.object(
                    qt_module.QTimer, "singleShot"
                ) as single_shot, mock.patch.object(
                    self.window, "prompt_office_xml_cleanup"
                ) as cleanup:
                    self.window.refresh_state()

                single_shot.assert_not_called()
                cleanup.assert_not_called()
                self.assertTrue(configuration.exists())
                self.assertIsNone(self.window.pending_odt_xml)

    def test_executable_runner_passes_optional_working_directory(self):
        self.window.exe_edit.setText(str(self.home / "Office Setup.exe"))
        self.window.arguments_edit.setText("/configure office.xml")

        for working_directory, expected in (
            (str(self.home / "installer files"), str(self.home / "installer files")),
            ("", None),
        ):
            with self.subTest(working_directory=working_directory):
                self.window.working_directory_edit.setText(working_directory)
                with mock.patch.object(
                    self.window, "save_config", return_value=dict(self.config)
                ), mock.patch.object(
                    backend, "launch_executable", return_value=4321
                ) as launch:
                    self.window.run_executable()

                launch.assert_called_once_with(
                    self.config["prefix"],
                    self.config["wine"],
                    str(self.home / "Office Setup.exe"),
                    "/configure office.xml",
                    working_directory=expected,
                    use_x11=self.config["use_x11"],
                )


if __name__ == "__main__":
    unittest.main()
