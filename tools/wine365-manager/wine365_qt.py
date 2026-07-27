#!/usr/bin/env python3
"""Native Qt Widgets interface for Wine 365 Manager."""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QSize, QTimer, Qt
from PySide6.QtGui import QCloseEvent, QIcon, QPixmap, QTextCursor
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

import wine365_backend as backend


class ManagerWindow(QMainWindow):
    def __init__(self, state, launcher: Path, icons: Path, font_helper: Path) -> None:
        super().__init__()
        self.state = state
        self.launcher = launcher
        self.icons = icons
        self.font_helper = font_helper
        self.initialized = False
        self.last_log = ""
        self.last_task_state = ""
        self.task_sensitive_buttons: list[QPushButton] = []

        self.setWindowTitle("Wine 365 Manager")
        self.setWindowIcon(QIcon(str(icons / "wine365-manager.svg")))
        self.setMinimumSize(780, 640)
        self.resize(980, 820)
        self.setObjectName("managerWindow")
        self._build_ui()
        self._apply_style()

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_state)
        self.timer.start(1200)
        self.refresh_state()

    def _apply_style(self) -> None:
        self.setStyleSheet("""
            QMainWindow#managerWindow { background: #f4f6fa; }
            QFrame#hero { background: #ffffff; border: 1px solid #dfe4ec; border-radius: 14px; }
            QLabel#title { color: #172033; font-size: 22px; font-weight: 700; }
            QLabel#subtitle, QLabel#hint, QLabel#appStatus { color: #667085; }
            QLabel#healthReady { color: #087a3e; background: #e9f8ef; border-radius: 12px;
                                 padding: 6px 11px; font-weight: 600; }
            QLabel#healthMissing { color: #9a3412; background: #fff2e8; border-radius: 12px;
                                   padding: 6px 11px; font-weight: 600; }
            QGroupBox { background: #ffffff; border: 1px solid #dfe4ec; border-radius: 12px;
                        margin-top: 13px; padding: 14px 12px 12px 12px; font-weight: 650; }
            QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; color: #253047; }
            QLineEdit, QPlainTextEdit { background: #ffffff; border: 1px solid #cfd6e1;
                                      border-radius: 7px; padding: 7px; selection-background-color: #2563eb; }
            QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid #2563eb; }
            QPushButton { min-height: 30px; padding: 2px 12px; border: 1px solid #cbd3df;
                          border-radius: 7px; background: #ffffff; color: #263247; }
            QPushButton:hover { background: #f1f5ff; border-color: #7da2ef; }
            QPushButton:pressed { background: #e5edff; }
            QPushButton:disabled { color: #98a2b3; background: #f4f5f7; }
            QPushButton#primaryButton { color: #ffffff; background: #2563eb; border-color: #2563eb;
                                        font-weight: 650; }
            QPushButton#primaryButton:hover { background: #1d4ed8; }
            QPushButton#dangerButton { color: #b42318; }
            QPlainTextEdit#operationLog { color: #d7deea; background: #111827; border: 0;
                                          font-family: monospace; }
            QScrollArea { border: 0; background: transparent; }
            QStatusBar { background: #ffffff; border-top: 1px solid #dfe4ec; }
        """)

    def _build_ui(self) -> None:
        root = QWidget(self)
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(18, 18, 18, 12)
        root_layout.setSpacing(14)

        hero = QFrame()
        hero.setObjectName("hero")
        hero_layout = QHBoxLayout(hero)
        hero_layout.setContentsMargins(18, 14, 18, 14)
        logo = QLabel()
        logo.setFixedSize(52, 52)
        pixmap = QPixmap(str(self.icons / "wine365-manager.svg"))
        if not pixmap.isNull():
            logo.setPixmap(pixmap.scaled(52, 52, Qt.AspectRatioMode.KeepAspectRatio,
                                         Qt.TransformationMode.SmoothTransformation))
        titles = QVBoxLayout()
        title = QLabel("Wine 365 Manager")
        title.setObjectName("title")
        subtitle = QLabel("Office environments, applications and Wine tools")
        subtitle.setObjectName("subtitle")
        titles.addWidget(title)
        titles.addWidget(subtitle)
        self.health = QLabel("Checking…")
        self.health.setObjectName("healthMissing")
        self.health.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hero_layout.addWidget(logo)
        hero_layout.addSpacing(7)
        hero_layout.addLayout(titles)
        hero_layout.addStretch()
        hero_layout.addWidget(self.health)
        root_layout.addWidget(hero)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(1, 1, 1, 8)
        content_layout.setSpacing(13)
        content_layout.addWidget(self._environment_group())
        content_layout.addWidget(self._installer_group())
        content_layout.addWidget(self._office_group())
        content_layout.addWidget(self._tools_group())
        content_layout.addWidget(self._maintenance_group())
        content_layout.addStretch()
        scroll.setWidget(content)
        root_layout.addWidget(scroll, 1)

        self.setCentralWidget(root)
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("Ready")

    def _path_row(self, edit: QLineEdit, browse_slot, directory: bool = False) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(7)
        browse = QPushButton("Browse…")
        browse.clicked.connect(browse_slot)
        browse.setProperty("directoryPicker", directory)
        layout.addWidget(edit, 1)
        layout.addWidget(browse)
        return row

    def _form(self) -> QFormLayout:
        form = QFormLayout()
        form.setHorizontalSpacing(14)
        form.setVerticalSpacing(9)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
        return form

    def _button(self, text: str, slot, role: str = "", object_name: str = "",
                task_sensitive: bool = True) -> QPushButton:
        button = QPushButton(text)
        if role == "primary":
            button.setObjectName("primaryButton")
        elif role == "danger":
            button.setObjectName("dangerButton")
        if object_name:
            button.setAccessibleName(object_name)
        button.clicked.connect(slot)
        if task_sensitive:
            self.task_sensitive_buttons.append(button)
        return button

    def _environment_group(self) -> QGroupBox:
        group = QGroupBox("Wine environment")
        layout = QVBoxLayout(group)
        form = self._form()
        self.prefix_edit = QLineEdit()
        self.prefix_edit.setPlaceholderText(str(Path.home() / ".wine365"))
        self.prefix_edit.setAccessibleName("Wine environment path")
        form.addRow("Environment path", self._path_row(self.prefix_edit, self.browse_prefix, True))
        self.wine_edit = QLineEdit()
        self.wine_edit.setAccessibleName("Wine executable path")
        form.addRow("Wine 365 executable", self._path_row(self.wine_edit, self.browse_wine))
        layout.addLayout(form)
        buttons = QHBoxLayout()
        self.create_button = self._button("Create environment", lambda: self.environment_action(False),
                                          "primary", "Create environment")
        self.recreate_button = self._button("Recreate environment", lambda: self.environment_action(True),
                                            "danger", "Recreate environment")
        buttons.addWidget(self.create_button)
        buttons.addWidget(self.recreate_button)
        buttons.addWidget(self._button("Stop Wine processes", lambda: self.launch_tool("stop")))
        buttons.addWidget(self._button("Save paths", lambda: self.save_config(True)))
        buttons.addStretch()
        layout.addLayout(buttons)
        return group

    def _installer_group(self) -> QGroupBox:
        group = QGroupBox("Run an Office installer or Windows EXE")
        layout = QVBoxLayout(group)
        form = self._form()
        self.exe_edit = QLineEdit()
        self.exe_edit.setPlaceholderText(str(Path.home() / "Downloads/OfficeSetup.exe"))
        self.exe_edit.setAccessibleName("Windows executable path")
        form.addRow("Local EXE path", self._path_row(self.exe_edit, self.browse_executable))
        self.arguments_edit = QLineEdit()
        self.arguments_edit.setPlaceholderText('Example: /configure "/home/user/office.xml"')
        self.arguments_edit.setAccessibleName("Executable arguments")
        form.addRow("Optional arguments", self.arguments_edit)
        layout.addLayout(form)
        hint = QLabel("The executable runs directly in the selected Wine environment; no browser upload or copy is used.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        buttons = QHBoxLayout()
        buttons.addWidget(self._button("Choose EXE…", self.choose_and_run_executable))
        buttons.addWidget(self._button("Run EXE", self.run_executable, "primary", "Run executable"))
        buttons.addStretch()
        layout.addLayout(buttons)
        return group

    def _office_group(self) -> QGroupBox:
        group = QGroupBox("Office applications and shortcuts")
        layout = QVBoxLayout(group)
        apps = QGridLayout()
        apps.setHorizontalSpacing(14)
        self.app_checks: dict[str, QCheckBox] = {}
        self.app_status: dict[str, QLabel] = {}
        names = {"word": "Word", "excel": "Excel", "powerpoint": "PowerPoint", "outlook": "Outlook"}
        for column, (app, name) in enumerate(names.items()):
            cell = QFrame()
            cell_layout = QHBoxLayout(cell)
            cell_layout.setContentsMargins(6, 5, 6, 5)
            check = QCheckBox(name)
            check.setIcon(QIcon(str(self.icons / backend.APP_META[app]["icon"])))
            check.setIconSize(QSize(25, 25))
            check.setAccessibleName(f"Select {name}")
            status = QLabel("Not detected")
            status.setObjectName("appStatus")
            cell_layout.addWidget(check)
            cell_layout.addStretch()
            cell_layout.addWidget(status)
            apps.addWidget(cell, column // 2, column % 2)
            self.app_checks[app] = check
            self.app_status[app] = status
        layout.addLayout(apps)
        self.desktop_copy = QCheckBox("Also copy selected shortcuts to the visible Desktop")
        layout.addWidget(self.desktop_copy)
        buttons = QHBoxLayout()
        buttons.addWidget(self._button("Create/update selected", lambda: self.shortcut_action(True), "primary"))
        buttons.addWidget(self._button("Remove selected", lambda: self.shortcut_action(False), "danger"))
        buttons.addWidget(self._button("Launch selected", self.launch_selected))
        buttons.addStretch()
        layout.addLayout(buttons)
        return group

    def _tools_group(self) -> QGroupBox:
        group = QGroupBox("Wine tools")
        grid = QGridLayout(group)
        tools = [
            ("Configuration", "winecfg"), ("Registry Editor", "regedit"),
            ("Control Panel", "control"), ("File Explorer", "explorer"),
            ("Command Prompt", "cmd"), ("Task Manager", "taskmgr"),
            ("Uninstaller", "uninstaller"), ("Stop Wine", "stop"),
        ]
        for index, (label, tool) in enumerate(tools):
            role = "danger" if tool == "stop" else ""
            grid.addWidget(self._button(label, lambda checked=False, value=tool: self.launch_tool(value), role),
                           index // 4, index % 4)
        return group

    def _maintenance_group(self) -> QGroupBox:
        group = QGroupBox("Update or remove Wine 365")
        layout = QVBoxLayout(group)
        version_row = QHBoxLayout()
        version_row.addWidget(QLabel("Update manifest address"))
        version_row.addStretch()
        self.version_label = QLabel("Version: development")
        self.version_label.setObjectName("hint")
        version_row.addWidget(self.version_label)
        layout.addLayout(version_row)
        self.update_edit = QLineEdit()
        self.update_edit.setPlaceholderText("Not configured yet")
        self.update_edit.setAccessibleName("Update manifest address")
        layout.addWidget(self.update_edit)
        hint = QLabel("Updates use an HTTPS manifest and verify the installer SHA-256 before execution.")
        hint.setObjectName("hint")
        layout.addWidget(hint)
        self.remove_prefix = QCheckBox(
            "When removing Wine 365, also permanently delete the selected environment and Office installation"
        )
        layout.addWidget(self.remove_prefix)
        buttons = QHBoxLayout()
        self.update_button = self._button("Update Wine 365", self.start_update)
        self.remove_button = self._button("Remove Wine 365", self.remove_wine365, "danger")
        self.cancel_button = self._button("Cancel operation", self.cancel_task, task_sensitive=False)
        self.cancel_button.setEnabled(False)
        buttons.addWidget(self.update_button)
        buttons.addWidget(self.remove_button)
        buttons.addWidget(self.cancel_button)
        buttons.addStretch()
        layout.addLayout(buttons)
        log_header = QHBoxLayout()
        log_header.addWidget(QLabel("Operation log"))
        log_header.addStretch()
        self.task_label = QLabel("Idle")
        self.task_label.setObjectName("hint")
        log_header.addWidget(self.task_label)
        layout.addLayout(log_header)
        self.log = QPlainTextEdit("No operation running.")
        self.log.setObjectName("operationLog")
        self.log.setReadOnly(True)
        self.log.setMinimumHeight(170)
        self.log.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        layout.addWidget(self.log)
        return group

    def config_values(self) -> dict:
        return {
            "prefix": self.prefix_edit.text(),
            "wine": self.wine_edit.text(),
            "desktop_copy": self.desktop_copy.isChecked(),
            "update_url": self.update_edit.text(),
        }

    def save_config(self, show: bool = False) -> dict | None:
        try:
            config = self.state.update_config(self.config_values())
            if show:
                self.notify("Settings saved.")
            return config
        except Exception as error:
            self.show_error(error)
            return None

    def selected_apps(self) -> list[str]:
        return [app for app, check in self.app_checks.items() if check.isChecked()]

    def ensure_idle(self) -> bool:
        with self.state.lock:
            running = self.state.task["running"]
        if running:
            self.show_error("Wait for the current operation to finish or cancel it first.")
            return False
        return True

    def require_selected_apps(self, exactly_one: bool = False) -> list[str] | None:
        apps = self.selected_apps()
        if (exactly_one and len(apps) != 1) or (not exactly_one and not apps):
            message = "Select exactly one Office application." if exactly_one else "Select at least one Office application."
            self.show_error(message)
            return None
        return apps

    def environment_action(self, recreate: bool) -> None:
        config = self.save_config()
        if not config:
            return
        if recreate:
            result = QMessageBox.warning(
                self,
                "Recreate Wine environment",
                f"Permanently replace {config['prefix']}?\n\n"
                "Office, settings, accounts and files stored inside that environment will be removed. "
                "The old environment is restored only if initialization fails.",
                QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
                QMessageBox.StandardButton.Cancel,
            )
            if result != QMessageBox.StandardButton.Yes:
                return
        try:
            self.state.start_task(
                "environment",
                lambda: backend.create_environment(config["prefix"], config["wine"], recreate, self.state.output),
            )
            self.notify("Environment operation started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def shortcut_action(self, create: bool) -> None:
        if not self.ensure_idle():
            return
        apps = self.require_selected_apps()
        config = self.save_config()
        if not apps or not config:
            return
        try:
            if create:
                files = backend.create_app_shortcuts(apps, config["prefix"], config["wine"],
                                                     self.launcher, self.icons, config["desktop_copy"])
                self.notify(f"Created {len(files)} shortcut file(s).")
            else:
                files = backend.remove_app_shortcuts(apps)
                self.notify(f"Removed {len(files)} shortcut file(s).")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def launch_selected(self) -> None:
        if not self.ensure_idle():
            return
        apps = self.require_selected_apps(exactly_one=True)
        config = self.save_config()
        if not apps or not config:
            return

        def launch() -> str:
            pid = backend.launch_app(config["prefix"], config["wine"], apps[0], self.font_helper)
            return f"Application started (PID {pid})."

        try:
            self.state.start_task("launch", launch)
            self.notify("Launching the selected application…")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def launch_tool(self, tool: str) -> None:
        if not self.ensure_idle():
            return
        config = self.save_config()
        if not config:
            return

        def launch() -> str:
            pid = backend.launch_tool(config["prefix"], config["wine"], tool)
            return "Wine processes stopped." if pid is None else f"Tool started (PID {pid})."

        try:
            self.state.start_task("tool", launch)
            self.notify("Wine tool operation started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def run_executable(self) -> None:
        if not self.ensure_idle():
            return
        config = self.save_config()
        if not config:
            return
        executable = self.exe_edit.text().strip()
        if not executable:
            self.show_error("Choose a local .exe file first.")
            return
        try:
            pid = backend.launch_executable(config["prefix"], config["wine"], executable,
                                            self.arguments_edit.text())
            self.notify(f"Executable started (PID {pid}).")
        except Exception as error:
            self.show_error(error)

    def choose_and_run_executable(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self, "Choose a Windows executable", str(Path.home() / "Downloads"),
            "Windows executables (*.exe);;All files (*)"
        )
        if filename:
            self.exe_edit.setText(filename)
            self.run_executable()

    def browse_executable(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self, "Choose a Windows executable", self.exe_edit.text() or str(Path.home()),
            "Windows executables (*.exe);;All files (*)"
        )
        if filename:
            self.exe_edit.setText(filename)

    def browse_prefix(self) -> None:
        directory = QFileDialog.getExistingDirectory(
            self, "Choose Wine environment directory", self.prefix_edit.text() or str(Path.home())
        )
        if directory:
            self.prefix_edit.setText(directory)

    def browse_wine(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self, "Choose Wine executable", self.wine_edit.text() or str(Path.home()), "All files (*)"
        )
        if filename:
            self.wine_edit.setText(filename)

    def start_update(self) -> None:
        config = self.save_config()
        if not config:
            return
        if not config["update_url"]:
            self.show_error("No update address is configured yet.")
            return
        result = QMessageBox.question(
            self, "Update Wine 365", "Download, verify and install the available Wine 365 update?",
            QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
            QMessageBox.StandardButton.Cancel,
        )
        if result != QMessageBox.StandardButton.Yes:
            return

        def update() -> str:
            try:
                backend.stop_wine(config["prefix"], config["wine"])
                self.state.output("Stopped the selected Wine environment before updating.")
            except (FileNotFoundError, OSError):
                pass
            return backend.update_wine365(config["update_url"], self.state.output,
                                          self.state.cancel_event, self.state.set_process)

        try:
            self.state.start_task("update", update)
            self.notify("Update started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def remove_wine365(self) -> None:
        config = self.save_config()
        if not config:
            return
        remove_prefix = self.remove_prefix.isChecked()
        detail = (f"This also permanently deletes {config['prefix']} and everything inside it."
                  if remove_prefix else "The Wine environment and Office files will be preserved.")
        result = QMessageBox.warning(
            self, "Remove Wine 365", f"Remove the Wine 365 runner, manager and shortcuts?\n\n{detail}",
            QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
            QMessageBox.StandardButton.Cancel,
        )
        if result != QMessageBox.StandardButton.Yes:
            return
        try:
            self.state.start_task(
                "remove",
                lambda: backend.remove_wine365(config["prefix"], remove_prefix, self.state.output),
            )
            self.notify("Removal started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def cancel_task(self) -> None:
        self.state.cancel()
        self.notify("Cancellation requested.")

    def notify(self, message: str) -> None:
        self.statusBar().showMessage(message, 5000)

    def show_error(self, error) -> None:
        QMessageBox.critical(self, "Wine 365 Manager", str(error))

    def refresh_state(self) -> None:
        try:
            snapshot = self.state.snapshot()
        except Exception as error:
            self.health.setText("Status unavailable")
            self.health.setObjectName("healthMissing")
            self.health.style().unpolish(self.health)
            self.health.style().polish(self.health)
            self.statusBar().showMessage(str(error), 4000)
            return

        if not self.initialized:
            config = snapshot["config"]
            self.prefix_edit.setText(config["prefix"])
            self.wine_edit.setText(config["wine"])
            self.update_edit.setText(config["update_url"])
            self.desktop_copy.setChecked(config["desktop_copy"])
            self.initialized = True

        status = snapshot["status"]
        healthy = status["prefix_exists"] and status["wine_exists"]
        if healthy:
            text, object_name = "●  Environment ready", "healthReady"
        elif not status["wine_exists"]:
            text, object_name = "●  Wine runner missing", "healthMissing"
        else:
            text, object_name = "●  Environment not created", "healthMissing"
        if self.health.text() != text or self.health.objectName() != object_name:
            self.health.setText(text)
            self.health.setObjectName(object_name)
            self.health.style().unpolish(self.health)
            self.health.style().polish(self.health)

        for app, installed in status["apps"].items():
            label = self.app_status[app]
            label.setText("Installed" if installed else "Not detected")
            label.setStyleSheet("color: #087a3e; font-weight: 600;" if installed else "")

        self.version_label.setText(f"Version: {snapshot['version']}")
        task = snapshot["task"]
        self.task_label.setText(f"{task['kind']}: running" if task["running"] else task["status"].capitalize())
        for button in self.task_sensitive_buttons:
            button.setDisabled(task["running"])
        self.cancel_button.setEnabled(task["running"])
        if task["log"] != self.last_log:
            self.log.setPlainText(task["log"] or "No operation running.")
            self.log.moveCursor(QTextCursor.MoveOperation.End)
            self.last_log = task["log"]
        task_state = f"{task['running']}:{task['status']}"
        if self.last_task_state.endswith(":running") and not task["running"]:
            self.notify("Operation completed." if task["status"] == "completed" else "Operation failed; see the log.")
        self.last_task_state = task_state

    def closeEvent(self, event: QCloseEvent) -> None:
        self.timer.stop()
        self.state.cancel()
        event.accept()


def run_manager(state, launcher: Path, icons: Path, font_helper: Path,
                smoke_test: bool = False, screenshot: Path | None = None) -> int:
    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setApplicationName("Wine 365 Manager")
    app.setOrganizationName("Wine 365")
    app.setDesktopFileName("wine365-manager")
    window = ManagerWindow(state, launcher, icons, font_helper)
    window.show()

    if screenshot:
        def save_screenshot() -> None:
            screenshot.parent.mkdir(parents=True, exist_ok=True)
            if not window.grab().save(str(screenshot)):
                print(f"Failed to save screenshot: {screenshot}", file=sys.stderr)
                app.exit(1)
                return
            app.quit()
        QTimer.singleShot(700, save_screenshot)
    elif smoke_test:
        QTimer.singleShot(250, app.quit)

    return app.exec()
