#!/usr/bin/env python3
"""Native Qt Widgets interface for Wine 365 Manager."""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QSize, QTimer, Qt
from PySide6.QtGui import QCloseEvent, QFont, QIcon, QTextCursor
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QCommandLinkButton,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QStackedWidget,
    QStyle,
    QToolBar,
    QToolButton,
    QTreeWidget,
    QTreeWidgetItem,
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
        self.task_sensitive_buttons: list[QPushButton | QCommandLinkButton] = []

        self.setWindowTitle("Wine 365 Manager")
        self.setWindowIcon(QIcon(str(icons / "wine365-manager.svg")))
        self.setMinimumSize(820, 620)
        self.resize(960, 700)
        self._build_ui()

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_state)
        self.timer.start(1200)
        self.refresh_state()

    def _standard_icon(self, icon: QStyle.StandardPixmap) -> QIcon:
        return self.style().standardIcon(icon)

    def _build_ui(self) -> None:
        toolbar = QToolBar("Main")
        toolbar.setMovable(False)
        toolbar.setIconSize(QSize(28, 28))
        toolbar.addAction(QIcon(str(self.icons / "wine365-manager.svg")), "Wine 365 Manager")
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        toolbar.addWidget(spacer)
        self.health_icon = QLabel()
        self.health_icon.setFixedWidth(26)
        self.health = QLabel("Checking…")
        toolbar.addWidget(self.health_icon)
        toolbar.addWidget(self.health)
        toolbar.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.addToolBar(toolbar)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setChildrenCollapsible(False)
        self.navigation = QListWidget()
        self.navigation.setIconSize(QSize(24, 24))
        self.navigation.setSpacing(3)
        self.navigation.setMinimumWidth(180)
        self.navigation.setMaximumWidth(230)
        self.navigation.setAccessibleName("Manager sections")
        self.pages = QStackedWidget()

        sections = [
            ("Environment", QStyle.StandardPixmap.SP_DriveHDIcon, self._environment_page()),
            ("Applications", QStyle.StandardPixmap.SP_FileDialogListView, self._applications_page()),
            ("Wine tools", QStyle.StandardPixmap.SP_ComputerIcon, self._tools_page()),
            ("Maintenance", QStyle.StandardPixmap.SP_BrowserReload, self._maintenance_page()),
        ]
        for title, icon, page in sections:
            self.navigation.addItem(QListWidgetItem(self._standard_icon(icon), title))
            self.pages.addWidget(page)
        self.navigation.currentRowChanged.connect(self.pages.setCurrentIndex)
        self.navigation.setCurrentRow(0)

        splitter.addWidget(self.navigation)
        splitter.addWidget(self.pages)
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([190, 760])
        self.setCentralWidget(splitter)
        self.statusBar().showMessage("Ready")

    def _new_page(self, title: str, description: str) -> tuple[QWidget, QVBoxLayout]:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(22, 18, 22, 18)
        layout.setSpacing(14)
        heading = QLabel(title)
        heading_font = QFont(heading.font())
        heading_font.setPointSize(heading_font.pointSize() + 5)
        heading_font.setBold(True)
        heading.setFont(heading_font)
        layout.addWidget(heading)
        summary = QLabel(description)
        summary.setWordWrap(True)
        layout.addWidget(summary)
        return page, layout

    def _form(self) -> QFormLayout:
        form = QFormLayout()
        form.setHorizontalSpacing(14)
        form.setVerticalSpacing(9)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
        return form

    def _path_row(self, edit: QLineEdit, browse_slot, directory: bool = False) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(6)
        browse = QToolButton()
        browse.setText("Browse…")
        browse.setIcon(self._standard_icon(QStyle.StandardPixmap.SP_DirOpenIcon))
        browse.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        browse.setToolTip("Choose a directory" if directory else "Choose a file")
        browse.clicked.connect(browse_slot)
        layout.addWidget(edit, 1)
        layout.addWidget(browse)
        return row

    def _action_button(self, text: str, slot, icon: QStyle.StandardPixmap | None = None,
                       task_sensitive: bool = True) -> QPushButton:
        button = QPushButton(text)
        if icon is not None:
            button.setIcon(self._standard_icon(icon))
        button.clicked.connect(slot)
        if task_sensitive:
            self.task_sensitive_buttons.append(button)
        return button

    def _environment_page(self) -> QWidget:
        page, layout = self._new_page(
            "Wine environment",
            "Choose the Wine 365 runner and the isolated environment used for Microsoft Office.",
        )
        environment = QGroupBox("Environment paths")
        environment_layout = QVBoxLayout(environment)
        form = self._form()
        self.prefix_edit = QLineEdit()
        self.prefix_edit.setPlaceholderText(str(Path.home() / ".wine365"))
        self.prefix_edit.setAccessibleName("Wine environment path")
        form.addRow("Environment:", self._path_row(self.prefix_edit, self.browse_prefix, True))
        self.wine_edit = QLineEdit()
        self.wine_edit.setAccessibleName("Wine executable path")
        form.addRow("Wine executable:", self._path_row(self.wine_edit, self.browse_wine))
        environment_layout.addLayout(form)
        environment_buttons = QHBoxLayout()
        self.create_button = self._action_button(
            "Create", lambda: self.environment_action(False), QStyle.StandardPixmap.SP_DialogApplyButton
        )
        self.recreate_button = self._action_button(
            "Recreate…", lambda: self.environment_action(True), QStyle.StandardPixmap.SP_BrowserReload
        )
        environment_buttons.addWidget(self.create_button)
        environment_buttons.addWidget(self.recreate_button)
        environment_buttons.addWidget(self._action_button(
            "Stop Wine", lambda: self.launch_tool("stop"), QStyle.StandardPixmap.SP_MediaStop
        ))
        environment_buttons.addStretch()
        environment_buttons.addWidget(self._action_button(
            "Save paths", lambda: self.save_config(True), QStyle.StandardPixmap.SP_DialogSaveButton
        ))
        environment_layout.addLayout(environment_buttons)
        layout.addWidget(environment)

        installer = QGroupBox("Run a Windows executable")
        installer_layout = QVBoxLayout(installer)
        installer_form = self._form()
        self.exe_edit = QLineEdit()
        self.exe_edit.setPlaceholderText(str(Path.home() / "Downloads/OfficeSetup.exe"))
        self.exe_edit.setAccessibleName("Windows executable path")
        installer_form.addRow("Executable:", self._path_row(self.exe_edit, self.browse_executable))
        self.arguments_edit = QLineEdit()
        self.arguments_edit.setPlaceholderText('Example: /configure "/home/user/office.xml"')
        self.arguments_edit.setAccessibleName("Executable arguments")
        installer_form.addRow("Arguments:", self.arguments_edit)
        installer_layout.addLayout(installer_form)
        installer_buttons = QHBoxLayout()
        installer_buttons.addStretch()
        installer_buttons.addWidget(self._action_button(
            "Choose and run…", self.choose_and_run_executable, QStyle.StandardPixmap.SP_DialogOpenButton
        ))
        installer_buttons.addWidget(self._action_button(
            "Run", self.run_executable, QStyle.StandardPixmap.SP_MediaPlay
        ))
        installer_layout.addLayout(installer_buttons)
        layout.addWidget(installer)
        layout.addStretch()
        return page

    def _applications_page(self) -> QWidget:
        page, layout = self._new_page(
            "Office applications",
            "Create desktop integration for installed Office applications or launch one directly.",
        )
        self.app_tree = QTreeWidget()
        self.app_tree.setHeaderLabels(["Application", "Installation status"])
        self.app_tree.setRootIsDecorated(False)
        self.app_tree.setAlternatingRowColors(True)
        self.app_tree.setUniformRowHeights(True)
        self.app_tree.setAccessibleName("Office applications")
        header = self.app_tree.header()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.app_items: dict[str, QTreeWidgetItem] = {}
        names = {"word": "Microsoft Word", "excel": "Microsoft Excel",
                 "powerpoint": "Microsoft PowerPoint", "outlook": "Microsoft Outlook"}
        for app, name in names.items():
            item = QTreeWidgetItem([name, "Checking…"])
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(0, Qt.CheckState.Unchecked)
            item.setIcon(0, QIcon(str(self.icons / backend.APP_META[app]["icon"])))
            self.app_tree.addTopLevelItem(item)
            self.app_items[app] = item
        self.app_tree.itemDoubleClicked.connect(self.launch_tree_item)
        layout.addWidget(self.app_tree, 1)

        self.desktop_copy = QCheckBox("Also place shortcuts on the Desktop")
        layout.addWidget(self.desktop_copy)
        buttons = QHBoxLayout()
        buttons.addWidget(self._action_button(
            "Create or update shortcuts", lambda: self.shortcut_action(True),
            QStyle.StandardPixmap.SP_DialogApplyButton
        ))
        buttons.addWidget(self._action_button(
            "Remove shortcuts", lambda: self.shortcut_action(False), QStyle.StandardPixmap.SP_TrashIcon
        ))
        buttons.addStretch()
        buttons.addWidget(self._action_button(
            "Launch selected", self.launch_selected, QStyle.StandardPixmap.SP_MediaPlay
        ))
        layout.addLayout(buttons)
        return page

    def _tools_page(self) -> QWidget:
        page, layout = self._new_page(
            "Wine tools",
            "Open diagnostic and configuration tools in the selected Wine environment.",
        )
        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(8)
        tools = [
            ("Wine Configuration", "Change display, audio and Windows compatibility settings.", "winecfg",
             QStyle.StandardPixmap.SP_ComputerIcon),
            ("Registry Editor", "Inspect and edit the selected Wine registry.", "regedit",
             QStyle.StandardPixmap.SP_FileDialogDetailedView),
            ("Control Panel", "Open installed applets and system settings.", "control",
             QStyle.StandardPixmap.SP_FileDialogInfoView),
            ("File Explorer", "Browse files using Wine's file manager.", "explorer",
             QStyle.StandardPixmap.SP_DirOpenIcon),
            ("Command Prompt", "Open a Windows command shell.", "cmd",
             QStyle.StandardPixmap.SP_CommandLink),
            ("Task Manager", "Inspect processes running in this environment.", "taskmgr",
             QStyle.StandardPixmap.SP_DesktopIcon),
            ("Uninstaller", "Add or remove Windows applications.", "uninstaller",
             QStyle.StandardPixmap.SP_TrashIcon),
            ("Stop Wine", "Stop every Wine process in the selected environment.", "stop",
             QStyle.StandardPixmap.SP_MediaStop),
        ]
        for index, (title, description, tool, icon) in enumerate(tools):
            button = QCommandLinkButton(title, description)
            button.setIcon(self._standard_icon(icon))
            button.clicked.connect(lambda checked=False, value=tool: self.launch_tool(value))
            self.task_sensitive_buttons.append(button)
            grid.addWidget(button, index // 2, index % 2)
        layout.addLayout(grid)
        layout.addStretch()
        return page

    def _maintenance_page(self) -> QWidget:
        page, layout = self._new_page(
            "Maintenance",
            "Update Wine 365 from a verified release manifest or remove the local installation.",
        )
        update = QGroupBox("Update")
        update_layout = QVBoxLayout(update)
        update_form = self._form()
        self.update_edit = QLineEdit()
        self.update_edit.setPlaceholderText("No update manifest configured")
        self.update_edit.setAccessibleName("Update manifest address")
        self.version_label = QLabel("development")
        update_form.addRow("Installed version:", self.version_label)
        update_form.addRow("Manifest URL:", self.update_edit)
        update_layout.addLayout(update_form)
        update_buttons = QHBoxLayout()
        update_buttons.addStretch()
        self.update_button = self._action_button(
            "Check and install update…", self.start_update, QStyle.StandardPixmap.SP_BrowserReload
        )
        update_buttons.addWidget(self.update_button)
        update_layout.addLayout(update_buttons)
        layout.addWidget(update)

        removal = QGroupBox("Removal")
        removal_layout = QVBoxLayout(removal)
        self.remove_prefix = QCheckBox(
            "Also permanently delete the selected Wine environment and its Office installation"
        )
        removal_layout.addWidget(self.remove_prefix)
        remove_buttons = QHBoxLayout()
        remove_buttons.addStretch()
        self.remove_button = self._action_button(
            "Remove Wine 365…", self.remove_wine365, QStyle.StandardPixmap.SP_TrashIcon
        )
        remove_buttons.addWidget(self.remove_button)
        removal_layout.addLayout(remove_buttons)
        layout.addWidget(removal)

        log_header = QHBoxLayout()
        log_header.addWidget(QLabel("Operation log"))
        log_header.addStretch()
        self.task_label = QLabel("Idle")
        log_header.addWidget(self.task_label)
        layout.addLayout(log_header)
        self.log = QPlainTextEdit("No operation running.")
        self.log.setReadOnly(True)
        self.log.setMinimumHeight(130)
        layout.addWidget(self.log, 1)
        log_buttons = QHBoxLayout()
        log_buttons.addStretch()
        self.cancel_button = self._action_button(
            "Cancel operation", self.cancel_task, QStyle.StandardPixmap.SP_DialogCancelButton,
            task_sensitive=False,
        )
        self.cancel_button.setEnabled(False)
        log_buttons.addWidget(self.cancel_button)
        layout.addLayout(log_buttons)
        return page

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
        return [app for app, item in self.app_items.items()
                if item.checkState(0) == Qt.CheckState.Checked]

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
            self.pages.setCurrentIndex(3)
            self.navigation.setCurrentRow(3)
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

    def launch_tree_item(self, item: QTreeWidgetItem, column: int) -> None:
        for app_item in self.app_items.values():
            app_item.setCheckState(0, Qt.CheckState.Unchecked)
        item.setCheckState(0, Qt.CheckState.Checked)
        self.launch_selected()

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
            self.health_icon.setPixmap(self._standard_icon(
                QStyle.StandardPixmap.SP_MessageBoxWarning).pixmap(20, 20))
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
            health_text = "Environment ready"
            health_icon = QStyle.StandardPixmap.SP_DialogApplyButton
        elif not status["wine_exists"]:
            health_text = "Wine runner missing"
            health_icon = QStyle.StandardPixmap.SP_MessageBoxWarning
        else:
            health_text = "Environment not created"
            health_icon = QStyle.StandardPixmap.SP_MessageBoxWarning
        self.health.setText(health_text)
        self.health_icon.setPixmap(self._standard_icon(health_icon).pixmap(20, 20))

        for app, installed in status["apps"].items():
            item = self.app_items[app]
            item.setText(1, "Installed" if installed else "Not detected")
            item.setIcon(1, self._standard_icon(
                QStyle.StandardPixmap.SP_DialogApplyButton if installed
                else QStyle.StandardPixmap.SP_MessageBoxInformation
            ))

        self.version_label.setText(snapshot["version"])
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
