#!/usr/bin/env python3
"""Native Qt Widgets interface for Wine4Office Manager."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

from PySide6.QtCore import QObject, QSize, QTimer, Qt, QUrl, Signal, Slot
from PySide6.QtGui import (
    QAction,
    QBrush,
    QCloseEvent,
    QDesktopServices,
    QFont,
    QIcon,
    QPalette,
    QShowEvent,
    QTextCursor,
)
from PySide6.QtWidgets import (
    QAbstractButton,
    QAbstractItemView,
    QApplication,
    QButtonGroup,
    QCheckBox,
    QCommandLinkButton,
    QComboBox,
    QDialog,
    QDialogButtonBox,
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
    QProgressBar,
    QPushButton,
    QRadioButton,
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

import wine4office_backend as backend
import wine4office_incident as incident
import wine4office_desktop as desktop
import wine4office_i18n as i18n


def _create_environment_worker(state, config: dict, recreate: bool) -> str:
    """Create a prefix and apply its policies without a Qt object reference."""
    result = backend.create_environment(
        config["prefix"], config["wine"], recreate, state.output,
        cancel_event=state.cancel_event,
        process_callback=state.set_process,
    )
    if state.cancel_event.is_set():
        raise RuntimeError("Operation cancelled.")
    if backend.office_telemetry_disabled(config):
        backend.apply_office_telemetry_policy(
            config["prefix"], config["wine"], True,
            use_x11=config.get("use_x11", True),
            cancel_event=state.cancel_event,
            process_callback=state.set_process,
        )
    if state.cancel_event.is_set():
        raise RuntimeError("Operation cancelled.")
    compatibility = backend.office_compatibility_settings(config)
    if any(compatibility.values()):
        backend.apply_office_compatibility_policies(
            config["prefix"], config["wine"], compatibility,
            {policy_id: False for policy_id in compatibility},
            use_x11=config.get("use_x11", True),
            cancel_event=state.cancel_event,
            process_callback=state.set_process,
        )
    if state.cancel_event.is_set():
        raise RuntimeError("Operation cancelled.")
    return result


class _UiDispatcher(QObject):
    """Queue worker completions onto the QObject/main thread."""

    invoke = Signal(object)

    def __init__(self) -> None:
        super().__init__()
        self.invoke.connect(self._invoke, Qt.ConnectionType.QueuedConnection)

    @Slot(object)
    def _invoke(self, callback) -> None:
        callback()



class ManagerWindow(QMainWindow):
    ENVIRONMENT_PAGE = 0
    INSTALL_PAGE = 1
    APPLICATIONS_PAGE = 2
    OFFICE_SETTINGS_PAGE = 3
    TOOLS_PAGE = 4
    MAINTENANCE_PAGE = 5
    def __init__(self, state, launcher: Path, icons: Path, font_helper: Path,
                 restart_command: list[str] | None = None,
                 review_incident: Path | None = None) -> None:
        super().__init__()
        self._ui_dispatcher = _UiDispatcher()
        self.state = state
        self.launcher = launcher
        self.icons = icons
        self.font_helper = font_helper
        self.restart_command = list(restart_command or [str(launcher)])
        self.initialized = False
        self.last_log = ""
        self.last_task_state = ""
        self.pending_environment_transition = False
        self._close_when_idle = False
        self._automatic_close = False
        self.handled_offer_id = ""
        self.manual_update_check = False
        self.reported_update_error = ""
        self.restart_prompted = False
        self.task_sensitive_buttons: list[QPushButton | QCommandLinkButton] = []
        self.installed_apps: set[str] = set()
        self.pending_odt_xml: tuple[Path, bytes, str] | None = None
        self.preload_rebind: tuple[str, str] | None = None
        self.update_progress_dialog: QDialog | None = None
        self.update_progress_status: QLabel | None = None
        self.update_progress_bar: QProgressBar | None = None
        self.update_progress_log: QPlainTextEdit | None = None
        self.update_progress_button: QPushButton | None = None
        self.update_progress_finished = False
        self.update_progress_task_kind = "update"
        self.update_progress_fallback = "Updating Wine4Office…"
        self.update_progress_messages = {
            "completed": "Update completed.",
            "cancelled": "Update cancelled.",
            "failed": "Update failed. Review the details below.",
        }
        self.office_startup_dialog: QDialog | None = None
        self.office_startup_status: QLabel | None = None
        self.office_startup_bar: QProgressBar | None = None
        self.reporting_available = incident.reporting_available()
        self.initial_incident = review_incident
        self._startup_scheduled = False
        self.language = i18n.system_language()

        self.setWindowTitle("Wine4Office Manager")
        self.setWindowIcon(QIcon(str(icons / "wine4office-manager.png")))
        self.setMinimumSize(820, 620)
        self.resize(960, 700)
        self._build_ui()

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_state)
        self.timer.start(1200)

        self.refresh_state()
        self._translate_ui()
    def _queue_ui(self, callback) -> None:
        """Queue a state/widget update for the Qt object thread."""
        self._ui_dispatcher.invoke.emit(callback)

    def _task_completion(self, callback):
        """Adapt a worker completion to a queued UI callback."""
        def complete(result, error) -> None:
            self._queue_ui(lambda: callback(result, error))
        return complete


    def _standard_icon(self, icon: QStyle.StandardPixmap) -> QIcon:
        return self.style().standardIcon(icon)

    def showEvent(self, event: QShowEvent) -> None:
        super().showEvent(event)
        if not self._startup_scheduled:
            self._startup_scheduled = True
            QTimer.singleShot(0, self.finish_startup)

    def _tr(self, text: str) -> str:
        return i18n.translate(text, self.language)

    def _translate_tree_item(self, item: QTreeWidgetItem) -> None:
        for column in range(item.columnCount()):
            item.setText(column, self._tr(item.text(column)))
        for row in range(item.childCount()):
            self._translate_tree_item(item.child(row))

    def _translate_ui(self, root: QWidget | None = None) -> None:
        """Translate the constructed widget tree without changing backend values."""
        root = root or self
        root.setWindowTitle(self._tr(root.windowTitle()))
        for action in root.findChildren(QAction):
            action.setText(self._tr(action.text()))
            action.setToolTip(self._tr(action.toolTip()))
        for widget in root.findChildren(QWidget):
            if isinstance(widget, QAbstractButton):
                widget.setText(self._tr(widget.text()))
            elif isinstance(widget, QLabel):
                widget.setText(self._tr(widget.text()))
            elif isinstance(widget, QGroupBox):
                widget.setTitle(self._tr(widget.title()))
            if isinstance(widget, QLineEdit):
                widget.setPlaceholderText(self._tr(widget.placeholderText()))
            if isinstance(widget, QComboBox):
                for row in range(widget.count()):
                    widget.setItemText(row, self._tr(widget.itemText(row)))
            widget.setToolTip(self._tr(widget.toolTip()))
            widget.setAccessibleName(self._tr(widget.accessibleName()))
            widget.setAccessibleDescription(self._tr(widget.accessibleDescription()))
        if root is self:
            for row in range(self.navigation.count()):
                item = self.navigation.item(row)
                item.setText(self._tr(item.text()))
            header = self.app_tree.headerItem()
            if header is not None:
                self._translate_tree_item(header)
            for row in range(self.app_tree.topLevelItemCount()):
                self._translate_tree_item(self.app_tree.topLevelItem(row))
            self.statusBar().showMessage(self._tr(self.statusBar().currentMessage()))

    def _build_ui(self) -> None:
        toolbar = QToolBar("Main")
        toolbar.setMovable(False)
        toolbar.setIconSize(QSize(28, 28))
        toolbar.addAction(
            QIcon(str(self.icons / "wine4office-manager.png")),
            "Wine4Office Manager",
        )
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
            ("Install Office & Teams", QStyle.StandardPixmap.SP_ArrowDown,
             self._office_install_page()),
            ("Applications", QStyle.StandardPixmap.SP_FileDialogListView, self._applications_page()),
            ("Office settings", QStyle.StandardPixmap.SP_FileDialogContentsView,
             self._office_settings_page()),
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
            "Choose the Wine4Office runner and the isolated environment used for Microsoft Office.",
        )
        environment = QGroupBox("Environment paths")
        environment_layout = QVBoxLayout(environment)
        form = self._form()
        self.prefix_edit = QLineEdit()
        self.prefix_edit.setPlaceholderText(str(Path.home() / ".wine4office"))
        self.prefix_edit.setAccessibleName("Wine environment path")
        form.addRow("Environment:", self._path_row(self.prefix_edit, self.browse_prefix, True))
        self.wine_edit = QLineEdit()
        self.wine_edit.setAccessibleName("Wine executable path")
        form.addRow("Wine executable:", self._path_row(self.wine_edit, self.browse_wine))
        environment_layout.addLayout(form)
        self.use_x11 = QCheckBox(
            "Launch Wine4Office through X11 (uncheck for native Wayland)"
        )
        self.use_x11.setAccessibleName("Launch Wine4Office through X11")
        environment_layout.addWidget(self.use_x11)
        self.prefix_edit.textChanged.connect(self._sync_office_settings_summary)

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

        preload = self.preload_group = QGroupBox("Background services")
        preload_layout = QVBoxLayout(preload)
        preload_layout.setSpacing(2)
        self.preload_notice_label = QLabel(
            "Start Office Click-to-Run and App-V services at login. "
            "Uses about 300–600 MB of RAM and makes Office apps open 2–3× faster."
        )
        self.preload_notice_label.setAccessibleName("Background services memory notice")
        self.preload_notice_label.setWordWrap(True)
        preload_layout.addWidget(self.preload_notice_label)
        preload_form = self._form()
        preload_form.setVerticalSpacing(2)
        self.preload_selected_label = QLabel("Checking…")
        self.preload_selected_label.setAccessibleName("Selected preload environment")
        self.preload_selected_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        self.preload_binding_label = QLabel("Checking…")
        self.preload_binding_label.setAccessibleName("Bound preload environment")
        self.preload_binding_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        preload_form.addRow("Selected environment:", self.preload_selected_label)
        preload_form.addRow("Bound environment:", self.preload_binding_label)
        self.preload_memory_label = QLabel("—")
        self.preload_memory_label.setAccessibleName("Background services RAM usage")
        preload_form.addRow("RAM usage:", self.preload_memory_label)
        preload_layout.addLayout(preload_form)

        self.preload_detail_label = QLabel()
        self.preload_detail_label.setAccessibleName("Background services details")
        self.preload_detail_label.setWordWrap(True)
        self.preload_detail_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        preload_layout.addWidget(self.preload_detail_label)

        preload_buttons = QHBoxLayout()
        self.preload_enable_button = self._action_button(
            "Enable && start",
            lambda: self.preload_action("enable-start"),
            QStyle.StandardPixmap.SP_DialogApplyButton,
        )
        self.preload_enable_button.setAccessibleName("Enable and start background services")
        self.preload_enable_button.setToolTip(
            "Install if needed, enable at login, and start now."
        )
        self.preload_stop_button = self._action_button(
            "Stop && disable",
            lambda: self.preload_action("stop-disable"),
            QStyle.StandardPixmap.SP_MediaStop,
        )
        self.preload_stop_button.setAccessibleName("Stop and disable background services")
        self.preload_stop_button.setToolTip(
            "Stop now and disable startup at login. Close Office first."
        )
        for button in (
            self.preload_enable_button,
            self.preload_stop_button,
        ):
            preload_buttons.addWidget(button)
        preload_buttons.addStretch()
        preload_layout.addLayout(preload_buttons)
        layout.addWidget(preload)

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
        self.working_directory_edit = QLineEdit()
        self.working_directory_edit.setPlaceholderText("Optional; defaults to the executable's folder")
        self.working_directory_edit.setAccessibleName("Windows executable working folder")
        installer_form.addRow(
            "Working folder:",
            self._path_row(self.working_directory_edit, self.browse_working_directory, True),
        )
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

    def preload_action(self, action: str) -> None:
        if action not in {"enable-start", "stop-disable"}:
            raise ValueError(f"Unknown preload action: {action}")
        if action == "enable-start" and self.preload_rebind is not None:
            bound, selected = self.preload_rebind
            result = QMessageBox.question(
                self,
                "Replace background service binding",
                f"Move the inactive background services from {bound} to {selected} "
                "then enable and start them?",
                QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
                QMessageBox.StandardButton.Cancel,
            )
            if result != QMessageBox.StandardButton.Yes:
                return
        messages = {
            "enable-start": "Enabling and starting background services…",
            "stop-disable": "Stopping and disabling background services…",
        }
        try:
            self.state.start_preload_action(action)
            self.notify(messages[action])
            self.refresh_state()
        except Exception as error:
            detail = str(error)
            if action == "stop-disable" and "Office is active" in detail:
                detail += "\n\nClose Office before trying again."
            self.show_error(detail)

    def _update_preload_status(self, snapshot: dict) -> None:
        preload = snapshot["preload"]
        memory = preload.get("memory_bytes")
        memory_text = (
            f"{max(1, round(int(memory) / (1024 * 1024)))} MB"
            if isinstance(memory, int) and memory > 0 else "—"
        )
        if self.preload_memory_label.text() != memory_text:
            self.preload_memory_label.setText(memory_text)
        selected = str(snapshot["config"]["prefix"])
        binding = preload.get("binding")
        bound = binding.get("prefix") if isinstance(binding, dict) else binding
        bound_text = str(bound) if bound else self._tr("Not configured")
        self.preload_selected_label.setText(selected)
        self.preload_binding_label.setText(bound_text)

        supported = bool(preload.get("supported"))
        installed = bool(preload.get("installed"))
        enabled = bool(preload.get("enabled"))
        active = bool(preload.get("active"))
        selected_matches = bool(preload.get("selected_matches"))
        mismatch = bool(binding) and not selected_matches
        detail = str(preload.get("detail") or "").strip()
        if not supported:
            detail = "Systemd user services are unavailable."
        elif not installed:
            detail = "Click Enable & start to use background services."
        elif mismatch:
            if enabled or active:
                detail = (
                    f"A different environment is bound. Selected: {selected}. "
                    f"Bound: {bound_text}. Stop & disable applies to the bound "
                    "environment before this one can be enabled."
                )
            else:
                detail = (
                    f"An inactive environment is bound. Selected: {selected}. "
                    f"Bound: {bound_text}. Enable & start can replace it after "
                    "confirmation."
                )
        elif active and not enabled:
            detail = detail or "Running, but disabled at login."
        elif enabled and not active:
            detail = detail or "Enabled at login, but not running."
        else:
            detail = detail or ""
        self.preload_detail_label.setText(self._tr(detail))
        self.preload_rebind = (
            (bound_text, selected)
            if mismatch and installed and not enabled and not active
            else None
        )

        task_running = bool(snapshot["task"]["running"])
        available = supported and not task_running
        self.preload_enable_button.setEnabled(
            available and (not enabled or not active)
            and (not installed or not mismatch or (not enabled and not active))
        )
        self.preload_stop_button.setEnabled(
            available and installed and (enabled or active)
        )

    def _office_install_page(self) -> QWidget:
        page, layout = self._new_page(
            "Install Office & Teams",
            "Install Office with Microsoft's deployment tool or install Microsoft Teams "
            "with its standalone bootstrapper.",
        )
        installer = QGroupBox("Office Deployment Tool")
        installer_layout = QVBoxLayout(installer)
        explanation = QLabel(
            "Choose one product and one or more Office languages. The deployment tool and Office "
            "files download only after you click an install button."
        )
        explanation.setWordWrap(True)
        installer_layout.addWidget(explanation)

        form = self._form()
        self.office_product_combo = QComboBox()
        self.office_product_combo.setAccessibleName("Office product")
        for product in backend.OFFICE_PRODUCTS:
            self.office_product_combo.addItem(product["label"], product)
        self.office_product_combo.currentIndexChanged.connect(self.update_office_product_details)
        form.addRow("Product:", self.office_product_combo)

        self.office_languages_edit = QLineEdit()
        self.office_languages_edit.setText("en-US")
        self.office_languages_edit.setPlaceholderText("Example: en-US, de-DE")
        self.office_languages_edit.setAccessibleName("Office installation languages")
        form.addRow("Languages:", self.office_languages_edit)
        installer_layout.addLayout(form)

        self.office_product_details = QLabel()
        self.office_product_details.setWordWrap(True)
        self.office_product_details.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        installer_layout.addWidget(self.office_product_details)
        self.update_office_product_details()

        language_hint = QLabel(
            "Enter supported language tags separated by commas, semicolons, or spaces. "
            "The first language becomes the primary Office language."
        )
        language_hint.setWordWrap(True)
        installer_layout.addWidget(language_hint)

        customization = QCommandLinkButton(
            "Create a custom configuration",
            "Open Microsoft's Office Customization Tool, then export its deployment XML.",
        )
        customization.setAccessibleName("Open Microsoft Office Customization Tool")
        customization.clicked.connect(self.open_office_customization)
        installer_layout.addWidget(customization)

        buttons = QHBoxLayout()
        buttons.addStretch()
        buttons.addWidget(self._action_button(
            "Install from custom XML…", self.install_office_from_custom_xml,
            QStyle.StandardPixmap.SP_DialogOpenButton,
        ))
        buttons.addWidget(self._action_button(
            "Install Office", self.install_office_from_generated_xml,
            QStyle.StandardPixmap.SP_ArrowDown,
        ))
        installer_layout.addLayout(buttons)
        layout.addWidget(installer)

        teams = QGroupBox("Microsoft Teams")
        teams_layout = QVBoxLayout(teams)
        teams_explanation = QLabel(
            "Install Teams separately from Office with Microsoft's standalone bootstrapper. "
            "The installer is downloaded automatically; Teams support in Wine is experimental."
        )
        teams_explanation.setWordWrap(True)
        teams_layout.addWidget(teams_explanation)
        teams_buttons = QHBoxLayout()
        teams_buttons.addStretch()
        self.teams_install_button = self._action_button(
            "Install Teams", self.install_teams,
            QStyle.StandardPixmap.SP_ArrowDown,
        )
        self.teams_install_button.setAccessibleName(
            "Install Microsoft Teams with the standalone installer"
        )
        teams_buttons.addWidget(self.teams_install_button)
        teams_layout.addLayout(teams_buttons)
        layout.addWidget(teams)
        layout.addStretch()
        return page

    def install_teams(self) -> None:
        if not self.ensure_idle():
            return
        config = self.save_config()
        if not config:
            return
        try:
            self.state.start_task(
                "teams-install",
                lambda: backend.install_teams_with_bootstrapper(
                    config["prefix"],
                    config["wine"],
                    self.state.output,
                    cancel_event=self.state.cancel_event,
                    process_callback=self.state.set_process,
                    progress_callback=self.state.set_progress,
                    use_x11=config.get("use_x11", True),
                ),
            )
            self._show_task_progress(
                "teams-install",
                "Installing Microsoft Teams",
                "Microsoft Teams",
                "Preparing Microsoft Teams installation…",
                {
                    "completed": "Microsoft Teams installation completed.",
                    "cancelled": "Microsoft Teams installation cancelled.",
                    "failed": "Microsoft Teams installation failed. Review the details below.",
                },
            )
            self.notify("Microsoft Teams installation started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def update_office_product_details(self) -> None:
        product = self.office_product_combo.currentData()
        if not product:
            self.office_product_details.setText("No supported Office products are available.")
            return
        self.office_product_details.setText(
            f"Deployment product: {product['product_id']} · Update channel: {product['channel']}"
        )

    def open_office_customization(self) -> None:
        if not QDesktopServices.openUrl(QUrl(backend.OFFICE_CUSTOMIZATION_URL)):
            self.show_error(
                f"Could not open the Office Customization Tool:\n{backend.OFFICE_CUSTOMIZATION_URL}"
            )

    def install_office_from_generated_xml(self) -> None:
        if not self.ensure_idle():
            return
        product = self.office_product_combo.currentData()
        if not product:
            self.show_error("Choose a supported Office product first.")
            return
        try:
            languages = backend.validate_office_languages(self.office_languages_edit.text())
            xml = backend.build_office_configuration(product["product_id"], languages)
        except Exception as error:
            self.show_error(error)
            return
        self._start_office_install_payload(xml.encode("utf-8"))

    def install_office_from_custom_xml(self) -> None:
        if not self.ensure_idle():
            return
        filename, _ = QFileDialog.getOpenFileName(
            self,
            "Choose an Office deployment configuration",
            str(Path.home()),
            "Office deployment XML (*.xml);;All files (*)",
        )
        if filename:
            self._start_office_install(Path(filename))

    def _start_office_install(self, config_path: Path) -> None:
        try:
            validated_path, configuration_payload, config_digest = (
                backend.load_office_configuration(config_path)
            )
        except Exception as error:
            self.show_error(error)
            return
        self._start_office_install_payload(
            configuration_payload,
            validated_path,
            config_digest,
        )

    def _start_office_install_payload(
            self, configuration_payload: bytes,
            config_path: Path | None = None,
            config_digest: str | None = None) -> None:
        config = self.save_config()
        if not config:
            return
        try:
            self.state.start_task(
                "odt-install",
                lambda payload=configuration_payload: backend.install_office_with_odt(
                    config["prefix"],
                    config["wine"],
                    config_path,
                    self.state.output,
                    cancel_event=self.state.cancel_event,
                    process_callback=self.state.set_process,
                    configuration_payload=payload,
                    installer_started_callback=self.state.mark_foreground_ready,
                ),
            )
            self.pending_odt_xml = (
                (config_path, configuration_payload, config_digest)
                if config_path is not None and config_digest is not None
                else None
            )
            self.last_task_state = "True:running"
            self.pages.setCurrentIndex(self.MAINTENANCE_PAGE)
            self.navigation.setCurrentRow(self.MAINTENANCE_PAGE)
            self._show_office_startup_progress()
            self.notify("Preparing Office installer…", 0)
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def _show_office_startup_progress(self) -> None:
        if self.office_startup_dialog is not None:
            self.office_startup_dialog.close()
        dialog = QDialog(self)
        dialog.setWindowTitle("Starting Office installation")
        dialog.setWindowModality(Qt.WindowModality.WindowModal)
        dialog.setWindowFlag(Qt.WindowType.WindowCloseButtonHint, False)
        dialog.setMinimumWidth(440)
        layout = QVBoxLayout(dialog)
        status = QLabel(
            "Preparing the Office Deployment Tool. The installer will open shortly."
        )
        status.setWordWrap(True)
        layout.addWidget(status)
        progress = QProgressBar()
        progress.setRange(0, 0)
        progress.setAccessibleName("Office installer startup progress")
        layout.addWidget(progress)
        buttons = QHBoxLayout()
        buttons.addStretch()
        cancel = QPushButton("Cancel installation")
        cancel.clicked.connect(self.cancel_task)
        buttons.addWidget(cancel)
        layout.addLayout(buttons)
        self.office_startup_dialog = dialog
        self.office_startup_status = status
        self.office_startup_bar = progress
        dialog.finished.connect(
            lambda _result, current=dialog: self._clear_office_startup_progress(current)
        )
        self._translate_ui(dialog)
        dialog.show()

    def _clear_office_startup_progress(self, dialog: QDialog) -> None:
        if self.office_startup_dialog is not dialog:
            return
        self.office_startup_dialog = None
        self.office_startup_status = None
        self.office_startup_bar = None

    def _refresh_office_startup_progress(self, task: dict) -> None:
        dialog = self.office_startup_dialog
        if dialog is None or task.get("kind") != "odt-install":
            return
        ready = bool(task.get("foreground_ready"))
        if task.get("running") and not ready:
            return
        dialog.accept()
        if ready:
            self.notify("Office installer opened.")

    def prompt_office_xml_cleanup(self, config_path: Path, expected_digest: str) -> None:
        def configuration_is_unchanged() -> bool:
            try:
                return backend.office_configuration_digest(config_path) == expected_digest
            except Exception:
                return False

        if not configuration_is_unchanged():
            QMessageBox.information(
                self,
                "Deployment configuration changed",
                f"The original deployment configuration is missing, invalid, or changed:\n"
                f"{config_path}\n\nIt will not be deleted.",
            )
            return

        dialog = QMessageBox(self)
        dialog.setIcon(QMessageBox.Icon.Question)
        dialog.setWindowTitle("Office installation completed")
        dialog.setText(f"Keep the deployment configuration?\n{config_path}")
        dialog.setInformativeText(
            "Keeping the XML is the safe choice and allows you to reuse or inspect the exact configuration."
        )
        keep_button = dialog.addButton("Keep", QMessageBox.ButtonRole.AcceptRole)
        delete_button = dialog.addButton("Delete", QMessageBox.ButtonRole.DestructiveRole)
        dialog.setDefaultButton(keep_button)
        dialog.setEscapeButton(keep_button)
        self._translate_ui(dialog)
        dialog.exec()
        if dialog.clickedButton() is not delete_button:
            self.notify("Office deployment XML kept.")
            return
        try:
            deleted, preserved_path = backend.delete_office_configuration_if_unchanged(
                config_path, expected_digest
            )
        except Exception as error:
            self.show_error(f"Could not safely delete the Office deployment XML:\n{error}")
            return
        if deleted:
            self.notify("Office deployment XML deleted.")
            return
        if preserved_path is None:
            location = f"No recoverable configuration remains at:\n{config_path}"
        else:
            location = f"The configuration was safely preserved at:\n{preserved_path}"
        QMessageBox.warning(
            self,
            "Deployment configuration not deleted",
            f"The XML changed or could not be safely removed, so no further deletion was attempted.\n\n"
            f"{location}",
        )

    def _applications_page(self) -> QWidget:
        page, layout = self._new_page(
            "Office applications",
            "Create desktop integration for installed Office applications or launch one directly.",
        )
        self.apps_environment_label = QLabel()
        self.apps_environment_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(self.apps_environment_label)
        self.app_tree = QTreeWidget()
        self.app_tree.setHeaderLabels(
            ["Application", "Installation status", "Compatibility"]
        )
        self.app_tree.setRootIsDecorated(False)
        self.app_tree.setAlternatingRowColors(True)
        self.app_tree.setUniformRowHeights(True)
        self.app_tree.setAccessibleName("Office applications")
        self.app_tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        header = self.app_tree.header()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.app_items: dict[str, QTreeWidgetItem] = {}
        names = {
            "word": "Microsoft Word",
            "excel": "Microsoft Excel",
            "powerpoint": "Microsoft PowerPoint",
            "outlook": "Microsoft Outlook",
            "access": "Microsoft Access",
            "onenote": "Microsoft OneNote",
            "publisher": "Microsoft Publisher",
            "visio": "Microsoft Visio",
            "project": "Microsoft Project",
            "teams": "Microsoft Teams",
            "setlang": "Microsoft Office Language Preferences",
        }
        for app, name in names.items():
            item = QTreeWidgetItem([
                name, "Checking…", backend.APP_META[app]["compatibility"]
            ])
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(0, Qt.CheckState.Unchecked)
            item.setIcon(0, QIcon(str(self.icons / backend.APP_META[app]["icon"])))
            self.app_tree.addTopLevelItem(item)
            self.app_items[app] = item
        self.app_tree.itemDoubleClicked.connect(self.launch_tree_item)
        layout.addWidget(self.app_tree, 1)
        selection_hint = QLabel(
            "Check or select one or more applications. With nothing selected, shortcut creation uses every "
            "installed application in this environment."
        )
        selection_hint.setWordWrap(True)
        layout.addWidget(selection_hint)

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

    def _office_settings_page(self) -> QWidget:
        page, layout = self._new_page(
            "Office settings",
            "Apply curated Microsoft Office policies to the selected Wine environment "
            "without exposing thousands of Windows-only Group Policy entries.",
        )
        selected = QGroupBox("Selected environment")
        selected_layout = QVBoxLayout(selected)
        self.office_settings_environment_label = QLabel("Checking…")
        self.office_settings_environment_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        selected_layout.addWidget(self.office_settings_environment_label)
        layout.addWidget(selected)

        self.compatibility_settings_button = QCommandLinkButton(
            "Compatibility", "Default Office rendering and startup behavior"
        )
        self.compatibility_settings_button.setIcon(
            self._standard_icon(QStyle.StandardPixmap.SP_ComputerIcon)
        )
        self.compatibility_settings_button.clicked.connect(
            self.configure_compatibility_settings
        )
        self.task_sensitive_buttons.append(self.compatibility_settings_button)
        layout.addWidget(self.compatibility_settings_button)

        self.security_settings_button = QCommandLinkButton(
            "Security",
            "Standard Office behavior · Internet-download protection needs Linux integration",
        )
        self.security_settings_button.setIcon(
            self._standard_icon(QStyle.StandardPixmap.SP_MessageBoxWarning)
        )
        self.security_settings_button.clicked.connect(self.show_security_settings)
        layout.addWidget(self.security_settings_button)

        self.privacy_settings_button = QCommandLinkButton(
            "Privacy & cloud", "Telemetry uses the Office default"
        )
        self.privacy_settings_button.setIcon(
            self._standard_icon(QStyle.StandardPixmap.SP_DialogHelpButton)
        )
        self.privacy_settings_button.clicked.connect(self.configure_privacy_settings)
        self.task_sensitive_buttons.append(self.privacy_settings_button)
        layout.addWidget(self.privacy_settings_button)

        hint = QLabel(
            "These choices are stored per Wine environment. Wine4Office removes only "
            "registry values it owns and leaves externally changed values untouched."
        )
        hint.setWordWrap(True)
        layout.addWidget(hint)
        layout.addStretch()
        return page

    def _office_settings_config(self) -> dict | None:
        with self.state.lock:
            config = dict(self.state.config)
        entered_prefix = self.prefix_edit.text()
        try:
            matches = backend.paths_equivalent(config["prefix"], entered_prefix)
        except (TypeError, ValueError):
            matches = False
        if not matches:
            self.show_error(
                "Save or switch to the entered Wine environment before changing "
                "its Office settings."
            )
            return None
        if backend.classify_prefix(config["prefix"]) != "valid":
            self.show_error(
                "Create the selected Wine environment before changing Office settings."
            )
            return None
        return config

    def _compatibility_settings_dialog(
            self, current: dict[str, bool]) -> tuple[QDialog, dict[str, QCheckBox]]:
        dialog = QDialog(self)
        dialog.setWindowTitle("Office compatibility")
        dialog.setMinimumWidth(520)
        dialog_layout = QVBoxLayout(dialog)
        intro = QLabel(
            "Choose targeted workarounds for this Wine environment. Hardware "
            "acceleration remains enabled unless you explicitly disable it."
        )
        intro.setWordWrap(True)
        dialog_layout.addWidget(intro)
        checkboxes: dict[str, QCheckBox] = {}
        descriptions = {
            "disable_animations": "Reduce compositor and rendering glitches.",
            "disable_hardware_acceleration": (
                "Troubleshooting option for display corruption or GPU-driver problems."
            ),
            "skip_first_run": "Avoid the Office First Run experience.",
            "skip_start_screen": "Open directly to a document instead of the Start screen.",
        }
        for policy_id, spec in backend.OFFICE_COMPATIBILITY_POLICIES.items():
            checkbox = QCheckBox(spec["label"])
            checkbox.setChecked(current[policy_id])
            checkbox.setToolTip(descriptions[policy_id])
            checkbox.setAccessibleDescription(descriptions[policy_id])
            dialog_layout.addWidget(checkbox)
            detail = QLabel(descriptions[policy_id])
            detail.setWordWrap(True)
            detail.setContentsMargins(24, 0, 0, 6)
            dialog_layout.addWidget(detail)
            checkboxes[policy_id] = checkbox
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        dialog_layout.addWidget(buttons)
        return dialog, checkboxes

    def configure_compatibility_settings(self) -> None:
        if not self.ensure_idle():
            return
        config = self._office_settings_config()
        if config is None:
            return
        current = backend.office_compatibility_settings(config)
        dialog, checkboxes = self._compatibility_settings_dialog(current)
        self._translate_ui(dialog)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        desired = {
            policy_id: checkbox.isChecked()
            for policy_id, checkbox in checkboxes.items()
        }
        self._start_config_update(
            {
                "prefix": config["prefix"],
                "office_compatibility_settings": desired,
            },
            on_success=lambda _saved: (
                self._sync_office_settings_summary(config["prefix"]),
                self.notify("Office compatibility settings applied."),
            ),
            error_prefix="Could not apply Office compatibility settings",
        )

    def _privacy_settings_dialog(
            self, config: dict) -> tuple[QDialog, QCheckBox]:
        dialog = QDialog(self)
        dialog.setWindowTitle("Office privacy & cloud")
        dialog.setMinimumWidth(520)
        dialog_layout = QVBoxLayout(dialog)
        telemetry = QCheckBox("Disable Microsoft Office telemetry (policy)")
        telemetry.setChecked(backend.office_telemetry_disabled(config))
        telemetry_help = (
            "Sets Microsoft's Office diagnostic-data policy to Neither. Required "
            "service data is not disabled."
        )
        telemetry.setAccessibleName("Disable Microsoft Office telemetry policy")
        telemetry.setAccessibleDescription(telemetry_help)
        telemetry.setToolTip(telemetry_help)
        dialog_layout.addWidget(telemetry)
        detail = QLabel(
            f"{telemetry_help}\n\nConnected experiences and online content remain "
            "enabled so sign-in, cloud fonts, templates, and add-ins keep working."
        )
        detail.setWordWrap(True)
        dialog_layout.addWidget(detail)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        dialog_layout.addWidget(buttons)
        return dialog, telemetry

    def configure_privacy_settings(self) -> None:
        if not self.ensure_idle():
            return
        config = self._office_settings_config()
        if config is None:
            return
        dialog, telemetry = self._privacy_settings_dialog(config)
        self._translate_ui(dialog)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        self._start_config_update(
            {
                "prefix": config["prefix"],
                "disable_office_telemetry": telemetry.isChecked(),
            },
            on_success=lambda _saved: (
                self._sync_office_settings_summary(config["prefix"]),
                self.notify("Office privacy settings applied."),
            ),
            error_prefix="Could not apply Office privacy settings",
        )

    def show_security_settings(self) -> None:
        QMessageBox.information(
            self,
            "Office security",
            "Wine4Office does not yet claim that Internet macro blocking works on "
            "Linux.\n\nOffice normally relies on Windows Zone.Identifier metadata, "
            "which Linux downloads do not consistently provide. Security controls "
            "will be added after that provenance bridge is implemented and tested; "
            "broad Trusted Locations will not be used as a workaround.",
        )

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
            "Update Wine4Office Manager and its Wine runner from verified release metadata.",
        )
        if self.reporting_available:
            reliability = QGroupBox("Reliability reporting")
            reliability_layout = QVBoxLayout(reliability)
            reliability_intro = QLabel(
                "Wine4Office does not collect private information. Reports stay "
                "local unless you choose Send."
            )
            reliability_intro.setWordWrap(True)
            reliability_layout.addWidget(reliability_intro)
            self.incident_mode_group = QButtonGroup(self)
            self.incident_ask = QRadioButton(
                "Catch issues and let me choose whether to report"
            )
            self.incident_disabled = QRadioButton(
                "Don't catch or report anything"
            )
            self.incident_mode_group.addButton(self.incident_ask)
            self.incident_mode_group.addButton(self.incident_disabled)
            reliability_layout.addWidget(self.incident_ask)
            reliability_layout.addWidget(self.incident_disabled)
            self.incident_ask.toggled.connect(self.set_incident_reporting_mode)
            layout.addWidget(reliability)

        update = QGroupBox("Updates")
        update_layout = QVBoxLayout(update)
        update_form = self._form()
        self.update_edit = QLineEdit()
        self.update_edit.setPlaceholderText("HTTPS release metadata URL")
        self.update_edit.setAccessibleName("Release metadata address")
        self.version_label = QLabel("Manager: development; Wine: development")
        update_form.addRow("Installed versions:", self.version_label)
        update_form.addRow("Metadata URL:", self.update_edit)
        update_layout.addLayout(update_form)
        self.automatic_update_checks = QCheckBox(
            "Check in the background at login and every 24 hours"
        )
        self.automatic_update_checks.setAccessibleName(
            "Automatic background update checks"
        )
        automatic_help = (
            "Opt in to a per-user systemd timer. The Manager still checks whenever "
            "you open it, even when this background schedule is disabled."
        )
        self.automatic_update_checks.setToolTip(automatic_help)
        self.automatic_update_checks.setAccessibleDescription(automatic_help)
        self.automatic_update_checks.toggled.connect(
            self.set_automatic_update_checks
        )
        update_layout.addWidget(self.automatic_update_checks)
        self.include_prereleases = QCheckBox("Include prerelease updates")
        prerelease_help = (
            "Offer the newest published GitHub release, including test versions."
        )
        self.include_prereleases.setToolTip(prerelease_help)
        self.include_prereleases.setAccessibleDescription(prerelease_help)
        update_layout.addWidget(self.include_prereleases)
        update_buttons = QHBoxLayout()
        update_buttons.addStretch()
        self.update_button = self._action_button(
            "Check for updates…", self.start_update, QStyle.StandardPixmap.SP_BrowserReload
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
            "Remove Wine4Office…", self.remove_wine4office, QStyle.StandardPixmap.SP_TrashIcon
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
            "use_x11": self.use_x11.isChecked(),
            "update_url": self.update_edit.text(),
            "include_prereleases": self.include_prereleases.isChecked(),
        }

    def _sync_office_settings_summary(self, prefix: str) -> None:
        if not hasattr(self, "office_settings_environment_label"):
            return
        with self.state.lock:
            config = dict(self.state.config)
        self.office_settings_environment_label.setText(
            str(prefix).strip() or "No Wine environment selected"
        )
        compatibility = backend.office_compatibility_settings(config, prefix)
        enabled = [
            backend.OFFICE_COMPATIBILITY_POLICIES[policy_id]["label"]
            for policy_id, value in compatibility.items() if value
        ]
        self.compatibility_settings_button.setDescription(
            "Default Office rendering and startup behavior"
            if not enabled else f"{len(enabled)} managed: " + ", ".join(enabled)
        )
        self.privacy_settings_button.setDescription(
            "Telemetry disabled · connected experiences unchanged"
            if backend.office_telemetry_disabled(config, prefix)
            else "Telemetry uses the Office default · connected experiences unchanged"
        )


    def _set_config_fields(self, config: dict) -> None:
        self.prefix_edit.setText(config["prefix"])
        self.wine_edit.setText(config["wine"])
        self.update_edit.setText(config["update_url"])
        self.desktop_copy.setChecked(config["desktop_copy"])
        self.use_x11.setChecked(config["use_x11"])
        self.include_prereleases.setChecked(
            config.get("include_prereleases") is True
        )
        if self.reporting_available:
            ask = config.get("incident_reporting_mode", incident.REPORT_MODE_ASK) \
                == incident.REPORT_MODE_ASK
            self.incident_ask.blockSignals(True)
            self.incident_disabled.blockSignals(True)
            self.incident_ask.setChecked(ask)
            self.incident_disabled.setChecked(not ask)
            self.incident_ask.blockSignals(False)
            self.incident_disabled.blockSignals(False)
        self._sync_office_settings_summary(config["prefix"])


    def _restore_config_fields(self) -> None:
        with self.state.lock:
            config = dict(self.state.config)
        self._set_config_fields(config)

    def _prompt_old_environment_disposition(self, old_prefix: str) -> bool | None:
        dialog = QMessageBox(self)
        dialog.setIcon(QMessageBox.Icon.Warning)
        dialog.setWindowTitle("Old Wine environment")
        dialog.setText(
            f"The currently configured Wine environment is:\n{old_prefix}\n\n"
            "Preserve it, or delete it permanently after the replacement is ready?"
        )
        preserve_button = dialog.addButton(
            "Preserve", QMessageBox.ButtonRole.AcceptRole
        )
        delete_button = dialog.addButton(
            "Delete permanently", QMessageBox.ButtonRole.DestructiveRole
        )
        dialog.addButton(QMessageBox.StandardButton.Cancel)
        dialog.setDefaultButton(preserve_button)
        self._translate_ui(dialog)
        dialog.exec()
        clicked = dialog.clickedButton()
        if clicked is delete_button:
            return True
        if clicked is preserve_button:
            return False
        return None


    def save_config(self, show: bool = False) -> dict | None:
        values = self.config_values()
        old_prefix = self.state.configured_prefix()
        if backend.paths_equivalent(old_prefix, values["prefix"]):
            try:
                config = self.state.update_config(values)
                if show:
                    self.notify("Settings saved.")
                return config
            except Exception as error:
                self._restore_config_fields()
                self.show_error(error)
                return None

        if not self.ensure_idle():
            self._restore_config_fields()
            return None

        try:
            target_kind = backend.classify_prefix(values["prefix"])
        except Exception as error:
            self._restore_config_fields()
            self.show_error(error)
            return None
        if target_kind == "unsafe":
            target = backend.validate_prefix(values["prefix"])
            self._restore_config_fields()
            self.show_error(
                f"Refusing to initialize or use a nonempty directory that is not a valid "
                f"Wine prefix:\n{target}"
            )
            return None

        initialize = False
        if target_kind in ("missing", "empty"):
            target = backend.validate_prefix(values["prefix"])
            result = QMessageBox.question(
                self,
                "Initialize Wine environment",
                f"The selected Wine environment is {target_kind}:\n{target}\n\n"
                "Initialize it before switching?",
                QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
                QMessageBox.StandardButton.Cancel,
            )
            if result != QMessageBox.StandardButton.Yes:
                self._restore_config_fields()
                return None
            initialize = True

        delete_old = False
        try:
            old_is_valid = backend.classify_prefix(old_prefix) == "valid"
        except (OSError, ValueError):
            old_is_valid = False
        if old_is_valid:
            delete_old = self._prompt_old_environment_disposition(old_prefix)
            if delete_old is None:
                self._restore_config_fields()
                return None

        try:
            if delete_old:
                backend.validate_environment_deletion(old_prefix, values["prefix"])
            self.pending_environment_transition = True
            self.state.start_environment_transition(values, initialize, delete_old)
            self.pages.setCurrentIndex(self.MAINTENANCE_PAGE)
            self.navigation.setCurrentRow(self.MAINTENANCE_PAGE)
            self.notify("Wine environment transition started.")
            self.refresh_state()
        except Exception as error:
            self.pending_environment_transition = False
            self._restore_config_fields()
            self.show_error(error)
        return None

    def selected_apps(self) -> list[str]:
        selected_items = set(self.app_tree.selectedItems())
        return [app for app, item in self.app_items.items()
                if item.checkState(0) == Qt.CheckState.Checked or item in selected_items]

    def ensure_idle(self) -> bool:
        with self.state.lock:
            running = self.state.task["running"]
        if running:
            self.show_error("Wait for the current operation to finish or cancel it first.")
            return False
        return True

    def _start_config_update(self, payload: dict, on_success=None,
                             error_prefix: str = "Could not save settings") -> None:
        """Apply config and policy changes in a worker, then update widgets queued."""
        def finished(saved, error) -> None:
            if error is not None:
                self._restore_config_fields()
                self.show_error(f"{error_prefix}: {error}")
                return
            self._set_config_fields(saved)
            if on_success is not None:
                on_success(saved)

        try:
            self.state.start_config_update(payload, self._task_completion(finished))
            self.refresh_state()
        except Exception as error:
            self.show_error(f"{error_prefix}: {error}")

    def require_selected_apps(self, exactly_one: bool = False) -> list[str] | None:
        apps = self.selected_apps()
        if (exactly_one and len(apps) != 1) or (not exactly_one and not apps):
            message = "Select exactly one Office application." if exactly_one else "Select at least one Office application."
            self.show_error(message)
            return None
        return apps

    def _create_environment(self, config: dict, recreate: bool) -> str:
        """Compatibility wrapper for tests and non-Qt callers."""
        return _create_environment_worker(self.state, config, recreate)


    def environment_action(self, recreate: bool) -> None:
        if not self.ensure_idle():
            return
        values = self.config_values()
        old_prefix = self.state.configured_prefix()
        if not backend.paths_equivalent(old_prefix, values["prefix"]):
            config = self.save_config()
            if not config:
                return
            values = config
        if recreate:
            result = QMessageBox.warning(
                self,
                "Recreate Wine environment",
                f"Permanently replace {values['prefix']}?\n\n"
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
                lambda: _create_environment_worker(
                    self.state, self.state.update_config(values), recreate
                ),
            )
            self.pages.setCurrentIndex(self.MAINTENANCE_PAGE)
            self.navigation.setCurrentRow(self.MAINTENANCE_PAGE)
            self.notify("Environment operation started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def shortcut_action(self, create: bool) -> None:
        if not self.ensure_idle():
            return
        apps = self.selected_apps()
        if create and not apps:
            apps = [app for app in self.app_items if app in self.installed_apps]
        if not apps:
            self.show_error("Select at least one Office application.")
            return
        config = self.save_config()
        if not apps or not config:
            return
        try:
            if create:
                files = backend.create_app_shortcuts(
                    apps, config["prefix"], config["wine"], config["desktop_copy"],
                    helper=self.font_helper,
                )
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
            process = subprocess.Popen(
                [str(self.launcher), apps[0]],
                env=os.environ.copy(), stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            return f"Application supervisor started (PID {process.pid})."

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
            pid = backend.launch_tool(
                config["prefix"], config["wine"], tool, use_x11=config["use_x11"]
            )
            return "Wine processes stopped." if pid is None else f"Tool started (PID {pid})."

        try:
            self.state.start_task("tool", launch)
            self.last_task_state = "True:running"
            self.notify(
                "Stopping Wine environment…" if tool == "stop"
                else "Opening Wine tool…",
                0 if tool == "stop" else 5000,
            )
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
            pid = backend.launch_executable(
                config["prefix"], config["wine"], executable, self.arguments_edit.text(),
                working_directory=self.working_directory_edit.text().strip() or None,
                use_x11=config["use_x11"],
            )
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

    def browse_working_directory(self) -> None:
        directory = QFileDialog.getExistingDirectory(
            self,
            "Choose executable working folder",
            self.working_directory_edit.text() or str(Path.home()),
        )
        if directory:
            self.working_directory_edit.setText(directory)

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

    def start_background_update_check(self) -> None:
        self.state.start_update_check()

    def finish_startup(self) -> None:
        """Finish first-open choices before showing update or incident prompts."""
        def continue_startup() -> None:
            self.start_background_update_check()
            if self.initial_incident is not None:
                self.review_incident(self.initial_incident)

        if self.prompt_reliability_on_first_launch(continue_startup):
            return
        continue_startup()

    def _route_after_first_open(self) -> None:
        try:
            snapshot = self.state.snapshot()
            applications = snapshot["status"]["apps"]
            office_installed = any(
                applications.get(name) is True
                for name in ("word", "excel", "powerpoint", "outlook")
            )
        except Exception:
            office_installed = False
        self.navigation.setCurrentRow(
            self.ENVIRONMENT_PAGE if office_installed else self.INSTALL_PAGE
        )

    def _reliability_dialog(self) -> tuple[QDialog, QRadioButton, QCheckBox]:
        dialog = QDialog(self)
        dialog.setWindowTitle("Reliability & updates")
        dialog.setMinimumWidth(560)
        layout = QVBoxLayout(dialog)
        heading = QLabel("How should Wine4Office handle crashes and hangs?")
        heading_font = QFont(heading.font())
        heading_font.setBold(True)
        heading.setFont(heading_font)
        layout.addWidget(heading)
        privacy = QLabel(
            "You can change this later in Maintenance. We never collect private "
            "information automatically."
        )
        privacy.setWordWrap(True)
        layout.addWidget(privacy)
        group = QButtonGroup(dialog)
        ask = QRadioButton("Catch issues and let me choose whether to report")
        disabled = QRadioButton("Don't catch or report anything")
        group.addButton(ask)
        group.addButton(disabled)
        ask.setChecked(True)
        layout.addWidget(ask)
        ask_detail = QLabel(
            "Show the complete editable report, let me optionally add context or "
            "an attachment, and send only if I choose Send."
        )
        ask_detail.setWordWrap(True)
        ask_detail.setContentsMargins(24, 0, 0, 8)
        layout.addWidget(ask_detail)
        layout.addWidget(disabled)
        disabled_detail = QLabel(
            "Disable monitoring, local incident capture, notifications and reporting."
        )
        disabled_detail.setWordWrap(True)
        disabled_detail.setContentsMargins(24, 0, 0, 8)
        layout.addWidget(disabled_detail)
        updates = QCheckBox("Check for Wine4Office updates every 24 hours")
        updates.setChecked(True)
        updates.setToolTip(
            "Only checks and notifies you when an update is available. Updates are "
            "never downloaded or installed automatically."
        )
        updates.setAccessibleDescription(updates.toolTip())
        layout.addWidget(updates)
        update_detail = QLabel(updates.toolTip())
        update_detail.setWordWrap(True)
        update_detail.setContentsMargins(24, 0, 0, 8)
        layout.addWidget(update_detail)
        buttons = QDialogButtonBox()
        skip = buttons.addButton("Skip", QDialogButtonBox.ButtonRole.RejectRole)
        save = buttons.addButton(
            "Save and continue", QDialogButtonBox.ButtonRole.AcceptRole
        )
        save.setDefault(True)
        skip.clicked.connect(dialog.reject)
        save.clicked.connect(dialog.accept)
        layout.addWidget(buttons)
        return dialog, ask, updates

    def prompt_reliability_on_first_launch(self, on_complete=None) -> bool:
        with self.state.lock:
            config = dict(self.state.config)
        if config.get("reliability_prompted") is True:
            return False
        if self.reporting_available:
            dialog, ask, updates = self._reliability_dialog()
            accepted = dialog.exec() == QDialog.DialogCode.Accepted
            reporting_mode = (
                incident.REPORT_MODE_ASK
                if accepted and ask.isChecked() else
                incident.REPORT_MODE_DISABLED
                if accepted else incident.REPORT_MODE_ASK
            )
            automatic_checks = updates.isChecked() if accepted else True
        else:
            reporting_mode = incident.REPORT_MODE_DISABLED
            automatic_checks = config.get("automatic_update_checks") is True

        def completed(saved, error) -> None:
            if error is not None:
                self.show_error(f"Could not save reliability settings: {error}")
                return
            self._set_config_fields(saved)
            self._route_after_first_open()
            if on_complete is not None:
                on_complete()

        try:
            self.state.start_reliability_update(
                reporting_mode, automatic_checks, self._task_completion(completed)
            )
            self.refresh_state()
        except Exception as error:
            self.show_error(f"Could not save reliability settings: {error}")
            return False
        return True

    def set_incident_reporting_mode(self, ask_enabled: bool) -> None:
        if not self.initialized or not self.reporting_available:
            return
        with self.state.lock:
            automatic_checks = (
                self.state.config.get("automatic_update_checks") is True
            )

        def completed(saved, error) -> None:
            if error is not None:
                self._restore_config_fields()
                self.show_error(f"Could not save incident reporting mode: {error}")
                return
            self._set_config_fields(saved)
            self.notify(
                "Issue detection and review prompts enabled."
                if ask_enabled else "Issue detection and reporting disabled."
            )

        try:
            self.state.start_reliability_update(
                incident.REPORT_MODE_ASK if ask_enabled else incident.REPORT_MODE_DISABLED,
                automatic_checks, self._task_completion(completed),
            )
            self.refresh_state()
        except Exception as error:
            self._restore_config_fields()
            self.show_error(f"Could not save incident reporting mode: {error}")

    def review_incident(self, path: Path, loaded=None) -> None:
        if not self.reporting_available:
            return
        if loaded is None:
            def finished(result, error) -> None:
                if error is not None:
                    self.show_error(f"Could not open the local incident report: {error}")
                    return
                self.review_incident(path, result)

            try:
                self.state.start_task(
                    "incident-load",
                    lambda: incident.load_incident(path),
                    self._task_completion(finished),
                )
                self.refresh_state()
            except Exception as error:
                self.show_error(f"Could not open the local incident report: {error}")
            return
        incident_path, record, stored_trace = loaded
        dialog = QDialog(self)
        dialog.setWindowTitle("Review Wine4Office report")
        dialog.setMinimumSize(720, 460)
        layout = QVBoxLayout(dialog)
        summary = QLabel(
            f"{record['summary']}\nNothing is sent until you click Send."
        )
        summary.setWordWrap(True)
        summary.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        layout.addWidget(summary)

        context_label = QLabel("What were you doing when it crashed or froze? (optional)")
        context_label.setSizePolicy(
            QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed
        )
        layout.addWidget(context_label)
        context = QPlainTextEdit(str(record.get("context") or ""))
        context.setPlaceholderText(
            "What did you do? What happened? Can you reproduce it?"
        )
        context.setMaximumHeight(120)
        layout.addWidget(context)

        attachment_row = QHBoxLayout()
        attachment_edit = QLineEdit()
        attachment_edit.setReadOnly(True)
        attachment_edit.setPlaceholderText("No attachment selected")
        choose_attachment = QPushButton("Add attachment…")
        remove_attachment = QPushButton("Remove")
        remove_attachment.setEnabled(False)
        attachment_row.addWidget(choose_attachment)
        attachment_row.addWidget(attachment_edit, 1)
        attachment_row.addWidget(remove_attachment)
        layout.addLayout(attachment_row)

        details_toggle = QToolButton()
        details_toggle.setText("Review or edit full report (optional)")
        details_toggle.setCheckable(True)
        details_toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        details_toggle.setArrowType(Qt.ArrowType.RightArrow)
        layout.addWidget(details_toggle)

        details = QWidget()
        details_layout = QVBoxLayout(details)
        details_layout.setContentsMargins(0, 0, 0, 0)

        details_layout.addWidget(QLabel("Technical data"))
        technical = QPlainTextEdit(
            json.dumps(record["technical"], indent=2, sort_keys=True)
        )
        technical.setAccessibleName("Editable technical report data")
        technical.setMinimumHeight(130)
        details_layout.addWidget(technical)

        details_layout.addWidget(QLabel("Diagnostic trace"))
        trace = QPlainTextEdit(stored_trace)
        trace.setAccessibleName("Editable diagnostic trace")
        trace.setMinimumHeight(130)
        details_layout.addWidget(trace)

        details_layout.addWidget(QLabel("Exact report that will be sent"))
        exact = QPlainTextEdit()
        exact.setReadOnly(True)
        exact.setAccessibleName("Exact report preview")
        exact.setMinimumHeight(160)
        details_layout.addWidget(exact)
        details.setVisible(False)
        layout.addWidget(details, 1)

        def set_detail_visibility(visible: bool) -> None:
            details.setVisible(visible)
            details_toggle.setArrowType(
                Qt.ArrowType.DownArrow if visible else Qt.ArrowType.RightArrow
            )
            if visible:
                dialog.resize(max(dialog.width(), 760), 760)

        details_toggle.toggled.connect(set_detail_visibility)

        def choose_file() -> None:
            filename, _selected_filter = QFileDialog.getOpenFileName(
                dialog, "Choose an affected Office file", str(Path.home()),
                "Office and document files (*.doc *.docx *.xls *.xlsx *.ppt *.pptx "
                "*.pdf *.rtf *.csv *.txt)",
            )
            if filename:
                attachment_edit.setText(filename)

        choose_attachment.clicked.connect(choose_file)
        remove_attachment.clicked.connect(attachment_edit.clear)
        attachment_edit.textChanged.connect(
            lambda value: remove_attachment.setEnabled(bool(value))
        )

        def current_technical() -> dict:
            value = json.loads(technical.toPlainText())
            if not isinstance(value, dict):
                raise ValueError("Technical data must remain a JSON object.")
            return value

        def refresh_exact(_index: int = -1) -> bool:
            try:
                preview = incident.report_preview(
                    record, context=context.toPlainText(),
                    technical=current_technical(), trace=trace.toPlainText(),
                    attachment=attachment_edit.text() or None,
                )
            except Exception as error:
                exact.setPlainText(f"Report preview is invalid:\n{error}")
                return False
            exact.setPlainText(json.dumps(preview, indent=2, sort_keys=True))
            return True

        details_toggle.toggled.connect(lambda visible: refresh_exact() if visible else None)
        context.textChanged.connect(refresh_exact)
        technical.textChanged.connect(refresh_exact)
        trace.textChanged.connect(refresh_exact)
        attachment_edit.textChanged.connect(refresh_exact)
        refresh_exact()

        buttons = QDialogButtonBox()
        delete_button = buttons.addButton(
            "Delete local report", QDialogButtonBox.ButtonRole.DestructiveRole
        )
        cancel_button = buttons.addButton(QDialogButtonBox.StandardButton.Cancel)
        report_button = buttons.addButton(
            "Send", QDialogButtonBox.ButtonRole.AcceptRole
        )
        report_button.setDefault(True)
        layout.addWidget(buttons)

        def delete_local() -> None:
            delete_button.setEnabled(False)

            def delete_operation() -> None:
                if self.state.cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")
                incident.delete_incident(incident_path)
                if self.state.cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")

            try:
                self.state.start_task(
                    "incident-delete",
                    delete_operation,
                    self._task_completion(
                        lambda _result, error: (
                            self.show_error(f"Could not delete the local report: {error}")
                            if error is not None else dialog.accept()
                        )
                    ),
                )
                self.refresh_state()
            except Exception as error:
                delete_button.setEnabled(True)
                self.show_error(f"Could not delete the local report: {error}")

        def send_report() -> None:
            if not refresh_exact():
                details_toggle.setChecked(True)
                return
            try:
                report_context = context.toPlainText()
                report_technical = current_technical()
                report_trace = trace.toPlainText()
                report_attachment = attachment_edit.text() or None
            except Exception as error:
                details_toggle.setChecked(True)
                self.show_error(f"The report was not sent: {error}")
                return
            report_button.setEnabled(False)

            def finished(result, error) -> None:
                if error is not None:
                    report_button.setEnabled(True)
                    self.show_error(f"The report was not sent: {error}")
                    return
                QMessageBox.information(
                    dialog, "Report sent",
                    f"Report {result['incident_id']} was sent successfully.",
                )
                dialog.accept()

            def submit_operation():
                if self.state.cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")
                result = incident.submit_incident(
                    incident_path, context=report_context,
                    technical=report_technical, trace=report_trace,
                    attachment=report_attachment,
                )
                if self.state.cancel_event.is_set():
                    raise RuntimeError("Operation cancelled.")
                return result

            try:
                self.state.start_task(
                    "incident-submit", submit_operation,
                    self._task_completion(finished),
                )
                self.refresh_state()
            except Exception as error:
                report_button.setEnabled(True)
                self.show_error(f"The report was not sent: {error}")

        def cancel_report() -> None:
            with self.state.lock:
                task = dict(self.state.task)
            if task["running"] and task["kind"] in {
                    "incident-delete", "incident-submit"}:
                self.state.cancel()
                cancel_button.setEnabled(False)
                return
            dialog.reject()

        delete_button.clicked.connect(delete_local)
        cancel_button.clicked.connect(cancel_report)
        report_button.clicked.connect(send_report)
        dialog.exec()

    def set_automatic_update_checks(self, enabled: bool) -> None:
        if not self.initialized:
            return
        self._apply_automatic_update_checks(bool(enabled), prompted=True)

    def _apply_automatic_update_checks(self, enabled: bool,
                                       *, prompted: bool) -> None:
        def completed(config, error) -> None:
            if error is not None:
                self.automatic_update_checks.blockSignals(True)
                with self.state.lock:
                    current = self.state.config.get("automatic_update_checks") is True
                self.automatic_update_checks.setChecked(current)
                self.automatic_update_checks.blockSignals(False)
                self.show_error(
                    f"Could not {'enable' if enabled else 'disable'} automatic "
                    f"update checks: {error}"
                )
                return
            self.automatic_update_checks.blockSignals(True)
            self.automatic_update_checks.setChecked(
                config.get("automatic_update_checks") is True
            )
            self.automatic_update_checks.blockSignals(False)
            self.notify(
                "Background update checks enabled."
                if enabled else
                "Background update checks disabled; checks on Manager open remain enabled."
            )

        try:
            self.state.start_automatic_update_update(
                enabled, prompted=prompted,
                completion=self._task_completion(completed),
            )
            self.refresh_state()
        except Exception as error:
            completed(None, error)

    def start_update(self) -> None:
        config = self.save_config()
        if not config:
            return
        if not config["update_url"]:
            self.show_error("Configure an HTTPS release metadata address first.")
            return
        self.handled_offer_id = ""
        self.manual_update_check = True
        try:
            started = self.state.start_update_check()
            self.notify("Checking for updates…" if started else "An update check is already running.")
        except Exception as error:
            self.show_error(error)

    def prompt_update_offer(self, offer: dict) -> None:
        if self._close_when_idle or self._automatic_close:
            return
        labels = {
            "manager": self._tr("Wine4Office Manager"),
            "wine": self._tr("Wine runner"),
        }
        selected = [
            name for name in ("manager", "wine")
            if offer["updates"].get(name)
        ]
        if not selected:
            return
        versions = "\n".join(
            f"{labels[name]}: {offer['updates'][name]['version']}"
            for name in selected
        )
        dialog = QMessageBox(self)
        dialog.setIcon(QMessageBox.Icon.Question)
        dialog.setWindowTitle(self._tr("Wine4Office update available"))
        dialog.setText(self._tr("Updates are available."))
        dialog.setInformativeText(
            f"{versions}\n\n{self._tr('Nothing downloads until you approve this update.')}"
        )
        install_button = dialog.addButton(
            self._tr("Download and install"), QMessageBox.ButtonRole.AcceptRole
        )
        later_button = dialog.addButton(self._tr("Later"), QMessageBox.ButtonRole.RejectRole)
        skip_button = dialog.addButton(
            self._tr("Skip these versions"), QMessageBox.ButtonRole.DestructiveRole
        )
        with self.state.lock:
            automatic_enabled = (
                self.state.config.get("automatic_update_checks") is True
            )
        disable_button = (
            dialog.addButton(
                self._tr("Disable automatic checks"), QMessageBox.ButtonRole.ActionRole
            )
            if automatic_enabled else None
        )
        dialog.setDefaultButton(later_button)
        dialog.exec()
        clicked = dialog.clickedButton()
        if clicked is skip_button:
            self.state.skip_offered_updates(selected)
        elif disable_button is not None and clicked is disable_button:
            self._apply_automatic_update_checks(False, prompted=True)
        elif clicked is install_button:
            try:
                if "manager" in selected:
                    self.restart_prompted = False
                self.navigation.setCurrentRow(self.MAINTENANCE_PAGE)
                self.state.start_offered_update(selected)
                self._show_update_progress(offer, selected)
                self.notify("Downloading the approved updates…")
                self.refresh_state()
            except Exception as error:
                self.show_error(error)

    def _show_update_progress(self, offer: dict, selected: list[str]) -> None:
        labels = {"manager": "Wine4Office Manager", "wine": "Wine runner"}
        versions = " · ".join(
            f"{labels[name]} {offer['updates'][name]['version']}"
            for name in selected
        )
        self._show_task_progress(
            "update",
            "Updating Wine4Office",
            versions,
            "Preparing update…",
            {
                "completed": "Update completed.",
                "cancelled": "Update cancelled.",
                "failed": "Update failed. Review the details below.",
            },
        )

    def _show_task_progress(self, task_kind: str, title: str, heading_text: str,
                            preparing_text: str,
                            messages: dict[str, str]) -> None:
        if self.update_progress_dialog is not None:
            self.update_progress_dialog.close()
        dialog = QDialog(self)
        dialog.setWindowTitle(title)
        dialog.setWindowModality(Qt.WindowModality.WindowModal)
        dialog.setWindowFlag(Qt.WindowType.WindowCloseButtonHint, False)
        dialog.setMinimumWidth(520)
        layout = QVBoxLayout(dialog)
        heading = QLabel(heading_text)
        heading.setWordWrap(True)
        layout.addWidget(heading)
        status = QLabel(preparing_text)
        status.setAccessibleName("Operation progress status")
        layout.addWidget(status)
        progress = QProgressBar()
        progress.setRange(0, 0)
        progress.setAccessibleName("Operation progress")
        layout.addWidget(progress)
        details = QPlainTextEdit()
        details.setReadOnly(True)
        details.setMaximumHeight(130)
        details.setPlaceholderText("Operation details will appear here.")
        layout.addWidget(details)
        buttons = QHBoxLayout()
        buttons.addStretch()
        cancel = QPushButton("Cancel update" if task_kind == "update" else "Cancel")
        cancel.clicked.connect(self.cancel_task)
        buttons.addWidget(cancel)
        layout.addLayout(buttons)
        self.update_progress_dialog = dialog
        self.update_progress_status = status
        self.update_progress_bar = progress
        self.update_progress_log = details
        self.update_progress_button = cancel
        self.update_progress_finished = False
        self.update_progress_task_kind = task_kind
        self.update_progress_fallback = preparing_text
        self.update_progress_messages = dict(messages)
        dialog.finished.connect(
            lambda _result, current=dialog: self._clear_update_progress(current)
        )
        dialog.show()

    def _clear_update_progress(self, dialog: QDialog) -> None:
        if self.update_progress_dialog is not dialog:
            return
        self.update_progress_dialog = None
        self.update_progress_status = None
        self.update_progress_bar = None
        self.update_progress_log = None
        self.update_progress_button = None
        self.update_progress_finished = False
        self.update_progress_task_kind = "update"
        self.update_progress_fallback = "Updating Wine4Office…"
        self.update_progress_messages = {}

    def _refresh_update_progress(self, task: dict) -> None:
        dialog = self.update_progress_dialog
        if dialog is None or task.get("kind") != self.update_progress_task_kind:
            return
        label = str(task.get("progress_label") or self.update_progress_fallback)
        if self.update_progress_status is not None:
            self.update_progress_status.setText(label)
        value = task.get("progress_value")
        if self.update_progress_bar is not None:
            if value is None:
                self.update_progress_bar.setRange(0, 0)
            else:
                self.update_progress_bar.setRange(0, 100)
                self.update_progress_bar.setValue(int(value))
        if self.update_progress_log is not None:
            log = str(task.get("log") or "")
            if self.update_progress_log.toPlainText() != log:
                self.update_progress_log.setPlainText(log)
                self.update_progress_log.moveCursor(QTextCursor.MoveOperation.End)
        if task.get("running") or self.update_progress_finished:
            return
        self.update_progress_finished = True
        if self.update_progress_status is not None:
            self.update_progress_status.setText(
                self.update_progress_messages.get(
                    str(task.get("status")), "Operation finished."
                )
            )
        if self.update_progress_bar is not None:
            self.update_progress_bar.setRange(0, 100)
            self.update_progress_bar.setValue(
                100 if task.get("status") == "completed" else 0
            )
        if self.update_progress_button is not None:
            try:
                self.update_progress_button.clicked.disconnect()
            except RuntimeError:
                pass
            self.update_progress_button.setText("Close")
            self.update_progress_button.clicked.connect(dialog.accept)
        if task.get("kind") == "update" and task.get("restart_required"):
            QTimer.singleShot(500, dialog.accept)

    def prompt_manager_restart(self, update_succeeded: bool) -> None:
        detail = (
            "The Wine4Office Manager update and post-install steps completed."
            if update_succeeded else
            "The updated manager was installed, but a post-install step failed. "
            "Restarting will retry pending post-install work."
        )
        answer = QMessageBox.question(
            self,
            "Restart Wine4Office Manager",
            f"{detail}\n\nRestart Wine4Office Manager now?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.Yes,
        )
        if answer == QMessageBox.StandardButton.Yes:
            self.restart_manager()

    def restart_manager(self) -> None:
        command = list(self.restart_command)
        if not command:
            self.show_error("Could not restart Wine4Office Manager: restart command is empty.")
            return
        self.timer.stop()
        try:
            os.execvpe(command[0], command, os.environ.copy())
        except OSError as error:
            self.timer.start(100)
            self.show_error(f"Could not restart Wine4Office Manager: {error}")

    def finish_successful_removal(self) -> None:
        QMessageBox.information(
            self,
            "Wine4Office Uninstalled",
            "Wine4Office was uninstalled successfully.",
        )
        self.close()

    def remove_wine4office(self) -> None:
        config = self.save_config()
        if not config:
            return
        remove_prefix = self.remove_prefix.isChecked()
        detail = (f"This also permanently deletes {config['prefix']} and everything inside it."
                  if remove_prefix else "The Wine environment and Office files will be preserved.")
        result = QMessageBox.warning(
            self, "Remove Wine4Office", f"Remove the Wine4Office runner, manager and shortcuts?\n\n{detail}",
            QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
            QMessageBox.StandardButton.Cancel,
        )
        if result != QMessageBox.StandardButton.Yes:
            return
        try:
            self.state.start_task(
                "remove",
                lambda: backend.remove_wine4office(config["prefix"], remove_prefix, self.state.output),
            )
            self.notify("Removal started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

    def cancel_task(self) -> None:
        self.state.cancel()
        self.notify("Cancellation requested.")

    def notify(self, message: str, timeout: int = 5000) -> None:
        self.statusBar().showMessage(self._tr(message), timeout)

    def show_error(self, error) -> None:
        QMessageBox.critical(self, self._tr("Wine4Office Manager"), self._tr(str(error)))

    def refresh_state(self) -> None:
        try:
            snapshot = self.state.snapshot()
        except Exception as error:
            self.health.setText(self._tr("Status unavailable"))
            self.health_icon.setPixmap(self._standard_icon(
                QStyle.StandardPixmap.SP_MessageBoxWarning).pixmap(20, 20))
            self.statusBar().showMessage(str(error), 4000)
            return

        if not self.initialized:
            self._set_config_fields(snapshot["config"])
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
        self.health.setText(self._tr(health_text))
        self.health_icon.setPixmap(self._standard_icon(health_icon).pixmap(20, 20))

        self.apps_environment_label.setText(
            f"{self._tr('Selected environment:')} {snapshot['config']['prefix']}"
        )
        self.installed_apps = {app for app, installed in status["apps"].items() if installed}
        for app, installed in status["apps"].items():
            item = self.app_items[app]
            item.setText(1, self._tr(
                "Installed" if installed else "Not installed"
            ))
            item.setText(2, self._tr(backend.APP_META[app]["compatibility"]))
            item.setIcon(1, self._standard_icon(
                QStyle.StandardPixmap.SP_DialogApplyButton if installed
                else QStyle.StandardPixmap.SP_MessageBoxInformation
            ))
            foreground = (
                QBrush()
                if installed else self.palette().brush(
                    QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text
                )
            )
            for column in range(item.columnCount()):
                item.setForeground(column, foreground)

        self.version_label.setText(
            f"{self._tr('Manager:')} {snapshot['version']}"
            f"{self._tr('; Wine:')} {snapshot['wine_version']}"
        )
        updater = snapshot["updater"]
        automatic_enabled = (
            snapshot["config"].get("automatic_update_checks") is True
        )
        if self.automatic_update_checks.isChecked() != automatic_enabled:
            self.automatic_update_checks.blockSignals(True)
            self.automatic_update_checks.setChecked(automatic_enabled)
            self.automatic_update_checks.blockSignals(False)
        if (updater["checked"] and not updater["checking"]
                and not self.update_edit.hasFocus()):
            self.update_edit.setText(snapshot["config"]["update_url"])
        if updater["checked"] and not updater["checking"] and self.manual_update_check:
            if updater["error"]:
                self.notify(f"Update check unavailable: {updater['error']}")
            elif not updater["offer"]:
                self.notify("Wine4Office Manager and Wine are up to date.")
            self.manual_update_check = False
        offer = updater.get("offer")
        if (offer and offer["id"] != self.handled_offer_id
                and not snapshot["task"]["running"]
                and not self._close_when_idle and not self._automatic_close):
            self.handled_offer_id = offer["id"]
            QTimer.singleShot(0, lambda current=offer: self.prompt_update_offer(current))
        task = snapshot["task"]
        self._refresh_update_progress(task)
        self._refresh_office_startup_progress(task)
        if (self.pending_odt_xml is not None
                and task["kind"] == "odt-install" and not task["running"]):
            config_path, _configuration_payload, expected_digest = self.pending_odt_xml
            self.pending_odt_xml = None
            if task["status"] == "completed":
                QTimer.singleShot(
                    700 if self.update_progress_dialog is not None else 0,
                    lambda current=config_path, digest=expected_digest:
                    self.prompt_office_xml_cleanup(current, digest),
                )
        task_text = (
            f"{self._tr(task['kind'])}: {self._tr('running')}"
            if task["running"] else self._tr(task["status"].capitalize())
        )
        self.task_label.setText(task_text)
        for button in self.task_sensitive_buttons:
            button.setDisabled(task["running"])
        self.cancel_button.setEnabled(task["running"])
        self._update_preload_status(snapshot)
        if (self.pending_environment_transition
                and task["kind"] == "environment-switch" and not task["running"]):
            self._set_config_fields(snapshot["config"])
            self.pending_environment_transition = False
        if task["log"] != self.last_log:
            self.log.setPlainText(task["log"] or self._tr("No operation running."))
            self.log.moveCursor(QTextCursor.MoveOperation.End)
            self.last_log = task["log"]
        task_state = f"{task['running']}:{task['status']}"
        if self.last_task_state.endswith(":running") and not task["running"]:
            if task["status"] == "completed":
                self.notify("Operation completed.")
                if task.get("kind") == "remove":
                    self._automatic_close = True
                    self.timer.stop()
                    QTimer.singleShot(0, self.finish_successful_removal)
            elif task["status"] == "cancelled":
                self.notify("Operation cancelled; settings restored.")
            else:
                self.notify("Operation failed; settings restored. See the log.")
                task_kind = str(task.get("kind") or "")
                if "preload" in task_kind:
                    failure = (str(task.get("log") or "").strip()
                               or "The background preload operation failed.")
                    if "stop" in task_kind and "Office is active" in failure:
                        failure += "\n\nClose Office before trying again."
                    QTimer.singleShot(
                        0, lambda current=failure: self.show_error(current)
                    )
            if (task.get("kind") == "update"
                    and task.get("restart_required")
                    and not self.restart_prompted):
                self.restart_prompted = True
                QTimer.singleShot(
                    0,
                    lambda succeeded=task["status"] == "completed":
                    self.prompt_manager_restart(succeeded),
                )
        self.last_task_state = task_state
        if self._close_when_idle and not task["running"] and not self._automatic_close:
            self._close_when_idle = False
            self._automatic_close = True
            self.timer.stop()
            QTimer.singleShot(0, self.close)

    def closeEvent(self, event: QCloseEvent) -> None:
        with self.state.lock:
            running = bool(self.state.task["running"])
        if running:
            self._close_when_idle = True
            self._automatic_close = False
            self.state.cancel()
            self.timer.start(100)
            self.notify("Cancellation requested; closing after the operation rolls back.")
            event.ignore()
            return
        self.timer.stop()
        self._close_when_idle = False
        self._automatic_close = False
        event.accept()


def run_manager(state, launcher: Path, icons: Path, font_helper: Path,
                restart_command: list[str] | None = None,
                smoke_test: bool = False, screenshot: Path | None = None,
                open_maintenance: bool = False,
                review_incident: Path | None = None) -> int:
    plugin_bridge = desktop.prepare_kde_plugin_bridge()
    if plugin_bridge is not None:
        QApplication.addLibraryPath(str(plugin_bridge))
    app = QApplication.instance() or QApplication(sys.argv[:1])
    language = i18n.system_language()
    app.setLayoutDirection(
        Qt.LayoutDirection.RightToLeft
        if language in i18n.RTL_LANGUAGES
        else Qt.LayoutDirection.LeftToRight
    )
    app.setApplicationName("Wine4Office Manager")
    app.setOrganizationName("Wine4Office")
    app.setDesktopFileName("wine4office-manager")
    window = ManagerWindow(
        state, launcher, icons, font_helper, restart_command=restart_command,
        review_incident=review_incident,
    )
    if open_maintenance:
        window.navigation.setCurrentRow(window.MAINTENANCE_PAGE)
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
