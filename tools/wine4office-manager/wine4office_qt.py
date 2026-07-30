#!/usr/bin/env python3
"""Native Qt Widgets interface for Wine4OfficeManager."""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QSize, QTimer, Qt, QUrl
from PySide6.QtGui import QCloseEvent, QDesktopServices, QFont, QIcon, QTextCursor
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QCheckBox,
    QCommandLinkButton,
    QComboBox,
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

import wine4office_backend as backend


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
        self.pending_environment_transition = False
        self._close_when_idle = False
        self._automatic_close = False
        self.handled_offer_id = ""
        self.manual_update_check = False
        self.reported_update_error = ""
        self.task_sensitive_buttons: list[QPushButton | QCommandLinkButton] = []
        self.installed_apps: set[str] = set()
        self.pending_odt_xml: tuple[Path, bytes, str] | None = None
        self.preload_rebind: tuple[str, str] | None = None

        self.setWindowTitle("Wine4OfficeManager")
        self.setWindowIcon(QIcon(str(icons / "wine4office-manager.png")))
        self.setMinimumSize(820, 620)
        self.resize(960, 700)
        self._build_ui()

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_state)
        self.timer.start(1200)
        self.refresh_state()
        QTimer.singleShot(0, self.start_background_update_check)

    def _standard_icon(self, icon: QStyle.StandardPixmap) -> QIcon:
        return self.style().standardIcon(icon)

    def _build_ui(self) -> None:
        toolbar = QToolBar("Main")
        toolbar.setMovable(False)
        toolbar.setIconSize(QSize(28, 28))
        toolbar.addAction(QIcon(str(self.icons / "wine4office-manager.png")), "Wine4OfficeManager")
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
            ("Install Office", QStyle.StandardPixmap.SP_ArrowDown, self._office_install_page()),
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
        self.disable_office_telemetry = QCheckBox(
            "Disable Microsoft Office telemetry (policy)"
        )
        self.disable_office_telemetry.setAccessibleName(
            "Disable Microsoft Office telemetry policy"
        )
        telemetry_help = (
            "Sets Microsoft's per-user Office diagnostic-data policy to Neither in the "
            "selected Wine environment. Required service data is not disabled."
        )
        self.disable_office_telemetry.setAccessibleDescription(telemetry_help)
        self.disable_office_telemetry.setToolTip(telemetry_help)
        self.disable_office_telemetry.setStatusTip(telemetry_help)
        environment_layout.addWidget(self.disable_office_telemetry)
        self.prefix_edit.textChanged.connect(self._sync_office_telemetry_field)

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

        preload = self.preload_group = QGroupBox("Click-to-Run preload")
        preload_layout = QVBoxLayout(preload)
        preload_layout.setSpacing(2)
        self.preload_notice_label = QLabel(
            "Optional: start Microsoft Office Click-to-Run at login for faster first "
            "launches. This keeps Wine support processes in the background and typically "
            "uses about 100–300 MB of RAM. Word is not started or kept running."
        )
        self.preload_notice_label.setAccessibleName("Click-to-Run preload memory notice")
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
        preload_layout.addLayout(preload_form)

        self.preload_state_label = QLabel("Checking preload service status…")
        self.preload_state_label.setAccessibleName("Click-to-Run preload state")
        self.preload_state_label.setWordWrap(True)
        preload_layout.addWidget(self.preload_state_label)
        self.preload_detail_label = QLabel()
        self.preload_detail_label.setAccessibleName("Click-to-Run preload details")
        self.preload_detail_label.setWordWrap(True)
        self.preload_detail_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        preload_layout.addWidget(self.preload_detail_label)

        preload_buttons = QHBoxLayout()
        self.preload_enable_button = self._action_button(
            "Enable at login",
            lambda: self.preload_action("enable"),
            QStyle.StandardPixmap.SP_DialogApplyButton,
        )
        self.preload_enable_button.setAccessibleName("Enable Click-to-Run preload at login")
        self.preload_enable_button.setToolTip(
            "Opt in for the selected environment and enable its Click-to-Run user service "
            "for future logins. It typically uses 100–300 MB of background RAM. "
            "This does not start Word or the preload worker now."
        )
        self.preload_disable_button = self._action_button(
            "Disable at login",
            lambda: self.preload_action("disable"),
            QStyle.StandardPixmap.SP_DialogCancelButton,
        )
        self.preload_disable_button.setAccessibleName("Disable Click-to-Run preload at login")
        self.preload_disable_button.setToolTip(
            "Disable automatic startup at login. This does not stop a running preload "
            "worker or Microsoft Office."
        )
        self.preload_start_button = self._action_button(
            "Start now",
            lambda: self.preload_action("start"),
            QStyle.StandardPixmap.SP_MediaPlay,
        )
        self.preload_start_button.setAccessibleName("Start Click-to-Run preload now")
        self.preload_start_button.setToolTip(
            "Start the preload worker now for the bound environment. "
            "This does not enable startup at login."
        )
        self.preload_stop_button = self._action_button(
            "Stop now",
            lambda: self.preload_action("stop"),
            QStyle.StandardPixmap.SP_MediaStop,
        )
        self.preload_stop_button.setAccessibleName("Stop Click-to-Run preload now")
        self.preload_stop_button.setToolTip(
            "Stop only the preload worker and components it owns after Office is closed. "
            "This never stops Microsoft Office."
        )
        for button in (
            self.preload_enable_button,
            self.preload_disable_button,
            self.preload_start_button,
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
        if action not in {"enable", "disable", "start", "stop"}:
            raise ValueError(f"Unknown preload action: {action}")
        if action == "enable" and self.preload_rebind is not None:
            bound, selected = self.preload_rebind
            result = QMessageBox.question(
                self,
                "Replace preload binding",
                f"Replace the inactive preload binding for {bound} with {selected} "
                "and enable it at login?\n\nThis will not start it now.",
                QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
                QMessageBox.StandardButton.Cancel,
            )
            if result != QMessageBox.StandardButton.Yes:
                return
        messages = {
            "enable": "Enabling Click-to-Run preload at login; it will not start now.",
            "disable": "Disabling Click-to-Run preload at login; a running worker will not stop.",
            "start": "Starting Click-to-Run preload for this session…",
            "stop": "Stopping Click-to-Run preload without stopping Office…",
        }
        try:
            self.state.start_preload_action(action)
            self.notify(messages[action])
            self.refresh_state()
        except Exception as error:
            detail = str(error)
            if action == "stop":
                detail += (
                    "\n\nStop now was not performed. Close Office before trying again, "
                    "or use Disable at login as the safe alternative. Disable at login "
                    "does not stop the worker or Office."
                )
            self.show_error(detail)

    @staticmethod
    def _preload_component_state(component) -> str:
        if not isinstance(component, dict):
            return "unknown"
        state = component.get("state")
        if not isinstance(state, str) or not state.strip():
            return "unknown"
        labels = {
            "inactive": "stopped",
            "not_running": "stopped",
            "not_found": "not found",
        }
        normalized = state.strip().lower()
        return labels.get(normalized, normalized.replace("_", " "))

    def _update_preload_status(self, snapshot: dict) -> None:
        preload = snapshot["preload"]
        selected = str(snapshot["config"]["prefix"])
        binding = preload.get("binding")
        bound = binding.get("prefix") if isinstance(binding, dict) else binding
        bound_text = str(bound) if bound else "Not configured"
        self.preload_selected_label.setText(selected)
        self.preload_binding_label.setText(bound_text)

        state = str(preload.get("state") or "unknown")
        supported = bool(preload.get("supported"))
        installed = bool(preload.get("installed"))
        enabled = bool(preload.get("enabled"))
        active = bool(preload.get("active"))
        checking = bool(preload.get("checking"))
        selected_matches = bool(preload.get("selected_matches"))
        mismatch = bool(binding) and not selected_matches
        state_names = {
            "unsupported": "Unavailable",
            "unbound": "Not configured",
            "disabled": "Disabled",
            "enabled": "Enabled",
            "active": "Active",
            "degraded": "Needs attention",
            "binding_mismatch": "Different environment bound",
        }
        overall = "Checking status" if checking else state_names.get(
            state, state.replace("_", " ").capitalize()
        )
        components = preload.get("components")
        if not isinstance(components, dict):
            components = {}
        click_to_run = self._preload_component_state(components.get("ClickToRunSvc"))
        self.preload_state_label.setText(
            f"{overall} — Login: {'enabled' if enabled else 'disabled'}; "
            f"Worker: {'running' if active else 'stopped'}; "
            f"ClickToRunSvc: {click_to_run}."
        )

        detail = str(preload.get("detail") or "").strip()
        reason = str(preload.get("reason") or "").strip()
        if not supported:
            cause = reason or detail or "User services are unavailable."
            detail = (
                f"{cause} Wine4Office will not use an autostart or detached fallback."
            )
        elif mismatch:
            if enabled or active:
                detail = (
                    f"A different environment is bound. Selected: {selected}. "
                    f"Bound: {bound_text}. Disable at login and Stop now apply to the "
                    "bound environment. Enable stays unavailable until that binding is "
                    "both disabled and stopped."
                )
            else:
                detail = (
                    f"An inactive environment is bound. Selected: {selected}. "
                    f"Bound: {bound_text}. Enable at login can replace it only after "
                    "explicit confirmation, and will not start the worker now."
                )
        elif checking:
            detail = "Checking the optional user service without blocking the Manager."
        elif not installed:
            detail = detail or (
                "Opt in with Enable at login. Enabling creates the Click-to-Run user "
                "service but does not start it now or run Word."
            )
        elif active and not enabled:
            detail = detail or (
                "Running for this session only. It will not start at the next login."
            )
        elif enabled and not active:
            detail = detail or (
                "Enabled for the next login but not running now. Use Start now separately."
            )
        else:
            detail = detail or (
                "Disable at login changes future logins only; it does not stop a running worker."
            )
        self.preload_detail_label.setText(detail)
        self.preload_rebind = (
            (bound_text, selected)
            if mismatch and installed and not enabled and not active
            else None
        )

        task_running = bool(snapshot["task"]["running"])
        available = supported and not checking and not task_running
        self.preload_enable_button.setEnabled(
            available and not enabled
            and (not mismatch or (installed and not active))
        )
        self.preload_disable_button.setEnabled(
            available and installed and enabled
        )
        self.preload_start_button.setEnabled(
            available and installed and selected_matches and not active
        )
        self.preload_stop_button.setEnabled(
            available and installed and active
        )

    def _office_install_page(self) -> QWidget:
        page, layout = self._new_page(
            "Install Microsoft Office",
            "Install a supported 64-bit Office product with Microsoft's Office Deployment Tool.",
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
            "Generate XML and install…", self.install_office_from_generated_xml,
            QStyle.StandardPixmap.SP_ArrowDown,
        ))
        installer_layout.addLayout(buttons)
        layout.addWidget(installer)
        layout.addStretch()
        return page

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
        filename, _ = QFileDialog.getSaveFileName(
            self,
            "Save Office deployment configuration",
            str(Path.home() / "office-deployment.xml"),
            "Office deployment XML (*.xml);;All files (*)",
        )
        if not filename:
            return
        config_path = Path(filename)
        try:
            config_path.write_text(xml, encoding="utf-8")
        except Exception as error:
            self.show_error(f"Could not save the Office deployment configuration:\n{error}")
            return
        self._start_office_install(config_path)

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
        config = self.save_config()
        if not config:
            return
        try:
            self.state.start_task(
                "odt-install",
                lambda payload=configuration_payload: backend.install_office_with_odt(
                    config["prefix"],
                    config["wine"],
                    validated_path,
                    self.state.output,
                    cancel_event=self.state.cancel_event,
                    process_callback=self.state.set_process,
                    configuration_payload=payload,
                ),
            )
            self.pending_odt_xml = (validated_path, configuration_payload, config_digest)
            self.pages.setCurrentIndex(4)
            self.navigation.setCurrentRow(4)
            self.notify("Office installation started.")
            self.refresh_state()
        except Exception as error:
            self.show_error(error)

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
        self.app_tree.setHeaderLabels(["Application", "Installation status"])
        self.app_tree.setRootIsDecorated(False)
        self.app_tree.setAlternatingRowColors(True)
        self.app_tree.setUniformRowHeights(True)
        self.app_tree.setAccessibleName("Office applications")
        self.app_tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        header = self.app_tree.header()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.app_items: dict[str, QTreeWidgetItem] = {}
        names = {"word": "Microsoft Word", "excel": "Microsoft Excel",
                 "powerpoint": "Microsoft PowerPoint", "outlook": "Microsoft Outlook",
                 "setlang": "Microsoft Office Language Preferences"}
        for app, name in names.items():
            item = QTreeWidgetItem([name, "Checking…"])
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
            "Update Wine4OfficeManager and its Wine runner from verified release metadata.",
        )
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
            "disable_office_telemetry": self.disable_office_telemetry.isChecked(),
            "update_url": self.update_edit.text(),
        }

    def _sync_office_telemetry_field(self, prefix: str) -> None:
        with self.state.lock:
            config = dict(self.state.config)
        self.disable_office_telemetry.setChecked(
            backend.office_telemetry_disabled(config, prefix)
        )


    def _set_config_fields(self, config: dict) -> None:
        self.prefix_edit.setText(config["prefix"])
        self.wine_edit.setText(config["wine"])
        self.update_edit.setText(config["update_url"])
        self.desktop_copy.setChecked(config["desktop_copy"])
        self.use_x11.setChecked(config["use_x11"])
        self.disable_office_telemetry.setChecked(
            backend.office_telemetry_disabled(config)
        )


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
            self.pages.setCurrentIndex(4)
            self.navigation.setCurrentRow(4)
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

    def require_selected_apps(self, exactly_one: bool = False) -> list[str] | None:
        apps = self.selected_apps()
        if (exactly_one and len(apps) != 1) or (not exactly_one and not apps):
            message = "Select exactly one Office application." if exactly_one else "Select at least one Office application."
            self.show_error(message)
            return None
        return apps

    def _create_environment(self, config: dict, recreate: bool) -> str:
        result = backend.create_environment(
            config["prefix"], config["wine"], recreate, self.state.output
        )
        if backend.office_telemetry_disabled(config):
            backend.apply_office_telemetry_policy(
                config["prefix"], config["wine"], True,
                use_x11=config.get("use_x11", True),
            )
        return result


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
                lambda: self._create_environment(config, recreate),
            )
            self.pages.setCurrentIndex(4)
            self.navigation.setCurrentRow(4)
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
                files = backend.create_app_shortcuts(apps, config["prefix"], config["wine"],
                                                     self.launcher, config["desktop_copy"])
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
            pid = backend.launch_app(
                config["prefix"], config["wine"], apps[0], self.font_helper,
                use_x11=config["use_x11"],
            )
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
            pid = backend.launch_tool(
                config["prefix"], config["wine"], tool, use_x11=config["use_x11"]
            )
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
        selected: list[str] = []
        skipped: list[str] = []
        labels = {
            "manager": "Wine4OfficeManager",
            "wine": "Wine runner",
        }
        for name in ("manager", "wine"):
            component = offer["updates"].get(name)
            if not component:
                continue
            dialog = QMessageBox(self)
            dialog.setIcon(QMessageBox.Icon.Question)
            dialog.setWindowTitle(f"{labels[name]} update available")
            dialog.setText(f"{labels[name]} {component['version']} is available.")
            dialog.setInformativeText(
                "The artifact will download only after you approve this component."
            )
            install_button = dialog.addButton(
                "Download and install", QMessageBox.ButtonRole.AcceptRole
            )
            later_button = dialog.addButton("Later", QMessageBox.ButtonRole.RejectRole)
            skip_button = dialog.addButton(
                f"Skip {component['version']}", QMessageBox.ButtonRole.DestructiveRole
            )
            dialog.setDefaultButton(later_button)
            dialog.exec()
            if dialog.clickedButton() is install_button:
                selected.append(name)
            elif dialog.clickedButton() is skip_button:
                skipped.append(name)
        if skipped:
            self.state.skip_offered_updates(skipped)
        if selected:
            try:
                self.state.start_offered_update(selected)
                self.notify("Downloading the approved updates…")
                self.refresh_state()
            except Exception as error:
                self.show_error(error)

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

    def notify(self, message: str) -> None:
        self.statusBar().showMessage(message, 5000)

    def show_error(self, error) -> None:
        QMessageBox.critical(self, "Wine4OfficeManager", str(error))

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
        self.health.setText(health_text)
        self.health_icon.setPixmap(self._standard_icon(health_icon).pixmap(20, 20))

        self.apps_environment_label.setText(f"Selected environment: {snapshot['config']['prefix']}")
        self.installed_apps = {app for app, installed in status["apps"].items() if installed}
        for app, installed in status["apps"].items():
            item = self.app_items[app]
            item.setText(1, "Installed" if installed else "Not installed in selected environment")
            item.setIcon(1, self._standard_icon(
                QStyle.StandardPixmap.SP_DialogApplyButton if installed
                else QStyle.StandardPixmap.SP_MessageBoxInformation
            ))

        self.version_label.setText(
            f"Manager: {snapshot['version']}; Wine: {snapshot['wine_version']}"
        )
        updater = snapshot["updater"]
        if (updater["checked"] and not updater["checking"]
                and not self.update_edit.hasFocus()):
            self.update_edit.setText(snapshot["config"]["update_url"])
        if updater["checked"] and not updater["checking"] and self.manual_update_check:
            if updater["error"]:
                self.notify(f"Update check unavailable: {updater['error']}")
            elif not updater["offer"]:
                self.notify("Wine4OfficeManager and Wine are up to date.")
            self.manual_update_check = False
        offer = updater.get("offer")
        if (offer and offer["id"] != self.handled_offer_id
                and not snapshot["task"]["running"]
                and not self._close_when_idle and not self._automatic_close):
            self.handled_offer_id = offer["id"]
            QTimer.singleShot(0, lambda current=offer: self.prompt_update_offer(current))
        task = snapshot["task"]
        if (self.pending_odt_xml is not None
                and task["kind"] == "odt-install" and not task["running"]):
            config_path, _configuration_payload, expected_digest = self.pending_odt_xml
            self.pending_odt_xml = None
            if task["status"] == "completed":
                QTimer.singleShot(
                    0,
                    lambda current=config_path, digest=expected_digest:
                    self.prompt_office_xml_cleanup(current, digest),
                )
        self.task_label.setText(f"{task['kind']}: running" if task["running"] else task["status"].capitalize())
        for button in self.task_sensitive_buttons:
            button.setDisabled(task["running"])
        self.cancel_button.setEnabled(task["running"])
        self._update_preload_status(snapshot)
        if (self.pending_environment_transition
                and task["kind"] == "environment-switch" and not task["running"]):
            self._set_config_fields(snapshot["config"])
            self.pending_environment_transition = False
        if task["log"] != self.last_log:
            self.log.setPlainText(task["log"] or "No operation running.")
            self.log.moveCursor(QTextCursor.MoveOperation.End)
            self.last_log = task["log"]
        task_state = f"{task['running']}:{task['status']}"
        if self.last_task_state.endswith(":running") and not task["running"]:
            if task["status"] == "completed":
                self.notify("Operation completed.")
            elif task["status"] == "cancelled":
                self.notify("Operation cancelled; settings restored.")
            else:
                self.notify("Operation failed; settings restored. See the log.")
                task_kind = str(task.get("kind") or "")
                if "preload" in task_kind:
                    failure = (str(task.get("log") or "").strip()
                               or "The background preload operation failed.")
                    if "stop" in task_kind:
                        failure += (
                            "\n\nStop now was not performed. Close Office before trying "
                            "again, or use Disable at login as the safe alternative. "
                            "Disable at login changes future logins only and does not stop "
                            "the worker or Office."
                        )
                    QTimer.singleShot(
                        0, lambda current=failure: self.show_error(current)
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
                smoke_test: bool = False, screenshot: Path | None = None) -> int:
    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setApplicationName("Wine4OfficeManager")
    app.setOrganizationName("Wine4Office")
    app.setDesktopFileName("wine4office-manager")
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
