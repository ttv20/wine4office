#!/usr/bin/env python3

import hashlib
import io
import os
import shlex
import subprocess
import signal
import stat
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock
from xml.etree import ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import wine4office_backend as backend


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.home = self.root / "home with spaces"
        self.home.mkdir()
        self.env = mock.patch.dict(os.environ, {
            "HOME": str(self.home),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_CACHE_HOME": str(self.home / ".cache"),
            "XDG_DESKTOP_DIR": str(self.home / "Desktop Folder"),
        })
        self.env.start()
        self.runner = self.root / "runner/bin"
        self.runner.mkdir(parents=True)
        self._script("wine", "#!/bin/sh\nexit 0\n")
        self._script("wineserver", "#!/bin/sh\nexit 0\n")
        self._script("wineboot", """#!/bin/sh
if [ "${WINE4OFFICE_TEST_FAIL:-}" = 1 ]; then exit 9; fi
mkdir -p "$WINEPREFIX/drive_c" "$WINEPREFIX/dosdevices"
touch "$WINEPREFIX/system.reg" "$WINEPREFIX/user.reg"
""")
        self.wine = self.runner / "wine"

    def tearDown(self):
        self.env.stop()
        self.temp.cleanup()

    def _script(self, name, content):
        path = self.runner / name
        path.write_text(content)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def _make_prefix(self, path, registry="registry"):
        path.mkdir(parents=True, exist_ok=True)
        (path / "system.reg").write_text(registry)
        (path / "user.reg").write_text(registry)
        (path / "drive_c").mkdir(exist_ok=True)
        (path / "dosdevices").mkdir(exist_ok=True)
        return path

    def test_default_prefix_is_home_wine4office(self):
        config = backend.default_config()
        self.assertEqual(config["prefix"], str(self.home / ".wine4office"))
        self.assertEqual(config["update_url"], backend.DEFAULT_METADATA_URL)
        self.assertTrue(config["use_x11"])

    def test_config_without_use_x11_loads_x11_default(self):
        path = backend.config_path()
        path.parent.mkdir(parents=True)
        path.write_text('{"desktop_copy": true}\n')

        config = backend.load_config()

        self.assertTrue(config["use_x11"])
        self.assertTrue(config["desktop_copy"])

    def test_native_wayland_choice_is_persisted(self):
        config = backend.default_config()
        config["use_x11"] = False

        backend.save_config(config)

        self.assertFalse(backend.load_config()["use_x11"])

    def test_automatic_update_checks_are_opt_in_by_default(self):
        config = backend.default_config()
        self.assertFalse(config["automatic_update_checks"])
        self.assertFalse(config["automatic_update_checks_prompted"])
        self.assertFalse(config["include_prereleases"])

    def test_automatic_update_timer_checks_at_login_and_every_24_hours(self):
        commands = []

        def systemctl(command, check=True):
            commands.append(list(command))
            return mock.Mock(returncode=0, stdout="", stderr="")

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_user", side_effect=systemctl
        ):
            backend.install_automatic_update_schedule()

        service = backend.automatic_update_service_path().read_text()
        timer = backend.automatic_update_timer_path().read_text()
        self.assertIn("--scheduled-update-check", service)
        self.assertIn("OnBootSec=2min", timer)
        self.assertIn("OnUnitActiveSec=24h", timer)
        self.assertIn("Persistent=true", timer)
        self.assertEqual(commands, [
            ["daemon-reload"],
            ["enable", "--now", backend.AUTOMATIC_UPDATE_TIMER],
        ])

    def test_scheduled_update_notification_can_disable_future_checks(self):
        config = backend.default_config()
        config["automatic_update_checks"] = True
        config["automatic_update_checks_prompted"] = True
        backend.save_config(config)
        result = {
            "metadata": {"metadata_url": config["update_url"]},
            "updates": {"manager": {"version": "9.0.0"}},
        }

        with mock.patch.object(
            backend, "check_for_updates", return_value=result
        ), mock.patch.object(
            backend, "persist_metadata_url"
        ), mock.patch.object(
            backend, "show_automatic_update_notification", return_value="disable"
        ), mock.patch.object(
            backend, "disable_automatic_update_schedule"
        ) as disable:
            checked = backend.run_scheduled_update_check()

        self.assertEqual(checked["action"], "disable")
        self.assertFalse(backend.load_config()["automatic_update_checks"])
        disable.assert_called_once_with()

    def test_background_notification_has_update_and_disable_actions(self):
        completed = mock.Mock(returncode=0, stdout="update\n", stderr="")
        with mock.patch.object(
            backend.shutil, "which", return_value="/usr/bin/notify-send"
        ), mock.patch.object(
            backend.subprocess, "run", return_value=completed
        ) as run:
            action = backend.show_automatic_update_notification({
                "manager": {"version": "9.0.0"},
            })

        self.assertEqual(action, "update")
        command = run.call_args.args[0]
        self.assertIn("--action=update=Update", command)
        self.assertIn(
            "--action=disable=Disable automatic checks", command
        )

    def test_notification_update_action_opens_manager_on_maintenance(self):
        config = backend.default_config()
        config["automatic_update_checks"] = True
        config["automatic_update_checks_prompted"] = True
        backend.save_config(config)
        result = {
            "metadata": {"metadata_url": config["update_url"]},
            "updates": {"manager": {"version": "9.0.0"}},
        }
        executable = self.root / "Wine4OfficeManager"
        executable.write_text("#!/bin/sh\n")
        executable.chmod(0o755)

        with mock.patch.object(
            backend, "check_for_updates", return_value=result
        ), mock.patch.object(
            backend, "persist_metadata_url"
        ), mock.patch.object(
            backend, "show_automatic_update_notification", return_value="update"
        ), mock.patch.object(
            backend, "_preload_manager_executable", return_value=executable
        ), mock.patch.object(
            backend.subprocess, "Popen"
        ) as popen:
            checked = backend.run_scheduled_update_check()

        self.assertEqual(checked["action"], "update")
        self.assertEqual(
            popen.call_args.args[0],
            [str(executable), "--open-maintenance"],
        )

    def test_scheduled_update_check_is_noop_without_opt_in(self):
        backend.save_config(backend.default_config())
        with mock.patch.object(backend, "check_for_updates") as check:
            result = backend.run_scheduled_update_check()
        self.assertFalse(result["checked"])
        check.assert_not_called()

    def test_office_telemetry_policy_defaults_off_and_persists_per_environment(self):
        first = self.home / "first-prefix"
        second = self.home / "second-prefix"
        config = backend.default_config()
        self.assertFalse(backend.office_telemetry_disabled(config, first))

        config = backend.set_office_telemetry_disabled(config, first, True)
        backend.save_config(config)
        loaded = backend.load_config()

        self.assertTrue(backend.office_telemetry_disabled(loaded, first))
        self.assertFalse(backend.office_telemetry_disabled(loaded, second))

    def test_disabling_office_telemetry_uses_exact_official_policy_value(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        completed = mock.Mock(returncode=0, stdout="", stderr="")
        with mock.patch.object(backend.subprocess, "run", return_value=completed) as run:
            changed = backend.apply_office_telemetry_policy(
                str(prefix), str(self.wine), True, use_x11=False
            )

        self.assertTrue(changed)
        self.assertEqual(
            run.call_args.args[0],
            [
                str(self.wine), "reg", "add",
                r"HKCU\Software\Policies\Microsoft\Office\Common\ClientTelemetry",
                "/v", "SendTelemetry", "/t", "REG_DWORD", "/d", "3", "/f",
            ],
        )
        self.assertEqual(run.call_args.kwargs["env"]["WINEPREFIX"], str(prefix.resolve()))
        self.assertTrue(run.call_args.kwargs["check"])

    def test_unchecking_removes_only_managed_value_and_tolerates_absence(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        present = mock.Mock(returncode=0, stdout="SendTelemetry REG_DWORD 0x3\n", stderr="")
        deleted = mock.Mock(returncode=0, stdout="", stderr="")
        with mock.patch.object(
            backend.subprocess, "run", side_effect=[present, deleted]
        ) as run:
            changed = backend.apply_office_telemetry_policy(
                str(prefix), str(self.wine), False, remove_managed=True
            )

        self.assertTrue(changed)
        self.assertEqual(
            [call.args[0][2] for call in run.call_args_list], ["query", "delete"]
        )
        self.assertEqual(
            run.call_args_list[1].args[0],
            [
                str(self.wine), "reg", "delete",
                backend.OFFICE_TELEMETRY_POLICY_KEY,
                "/v", backend.OFFICE_TELEMETRY_POLICY_VALUE, "/f",
            ],
        )

        absent = mock.Mock(returncode=1, stdout="", stderr="not found")
        with mock.patch.object(backend.subprocess, "run", return_value=absent) as run:
            changed = backend.apply_office_telemetry_policy(
                str(prefix), str(self.wine), False, remove_managed=True
            )
        self.assertFalse(changed)
        run.assert_called_once()

        externally_changed = mock.Mock(
            returncode=0, stdout="SendTelemetry REG_DWORD 0x2\n", stderr=""
        )
        with mock.patch.object(
            backend.subprocess, "run", return_value=externally_changed
        ) as run:
            changed = backend.apply_office_telemetry_policy(
                str(prefix), str(self.wine), False, remove_managed=True
            )
        self.assertFalse(changed)
        run.assert_called_once()

    def test_office_telemetry_registry_failures_propagate(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        failure = subprocess.CalledProcessError(5, [str(self.wine), "reg", "add"])
        with mock.patch.object(backend.subprocess, "run", side_effect=failure):
            with self.assertRaises(subprocess.CalledProcessError):
                backend.apply_office_telemetry_policy(
                    str(prefix), str(self.wine), True
                )

    def test_compatibility_policies_persist_per_environment(self):
        first = self.home / "first-prefix"
        second = self.home / "second-prefix"
        config = backend.set_office_compatibility_settings(
            backend.default_config(), first, {
                "disable_animations": True,
                "skip_start_screen": True,
            },
        )
        backend.save_config(config)
        loaded = backend.load_config()

        first_settings = backend.office_compatibility_settings(loaded, first)
        second_settings = backend.office_compatibility_settings(loaded, second)
        self.assertTrue(first_settings["disable_animations"])
        self.assertTrue(first_settings["skip_start_screen"])
        self.assertFalse(first_settings["disable_hardware_acceleration"])
        self.assertFalse(any(second_settings.values()))

    def test_compatibility_policy_uses_exact_official_registry_value(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        desired = {
            policy_id: policy_id == "disable_animations"
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }
        previous = {policy_id: False for policy_id in desired}
        completed = mock.Mock(returncode=0, stdout="", stderr="")
        with mock.patch.object(
            backend.subprocess, "run", return_value=completed
        ) as run:
            changed = backend.apply_office_compatibility_policies(
                str(prefix), str(self.wine), desired, previous,
            )

        self.assertEqual(changed, ["disable_animations"])
        self.assertEqual(run.call_args.args[0], [
            str(self.wine), "reg", "add",
            r"HKCU\Software\Policies\Microsoft\Office\16.0\Common\Graphics",
            "/v", "DisableAnimations", "/t", "REG_DWORD", "/d", "1", "/f",
        ])

    def test_compatibility_policy_preserves_externally_changed_value(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        previous = {
            policy_id: policy_id == "disable_animations"
            for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }
        desired = {policy_id: False for policy_id in previous}
        external = mock.Mock(
            returncode=0, stdout="DisableAnimations REG_DWORD 0x2\n", stderr=""
        )
        with mock.patch.object(
            backend.subprocess, "run", return_value=external
        ) as run:
            changed = backend.apply_office_compatibility_policies(
                str(prefix), str(self.wine), desired, previous,
            )

        self.assertEqual(changed, [])
        run.assert_called_once()
        self.assertEqual(run.call_args.args[0][2], "query")

    def test_compatibility_policy_batch_rolls_back_completed_changes(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        previous = {
            policy_id: False for policy_id in backend.OFFICE_COMPATIBILITY_POLICIES
        }
        desired = dict(previous)
        desired["disable_animations"] = True
        desired["disable_hardware_acceleration"] = True
        failure = subprocess.CalledProcessError(5, ["wine", "reg", "add"])
        with mock.patch.object(
            backend, "_apply_managed_office_dword",
            side_effect=[True, failure, True],
        ) as apply:
            with self.assertRaises(subprocess.CalledProcessError):
                backend.apply_office_compatibility_policies(
                    str(prefix), str(self.wine), desired, previous,
                )

        self.assertEqual(apply.call_count, 3)
        rollback = apply.call_args_list[2]
        self.assertFalse(rollback.args[5])
        self.assertTrue(rollback.kwargs["remove_managed"])

    def test_wine_environment_applies_display_precedence_for_both_modes(self):
        with mock.patch.dict(
            os.environ, {"DISPLAY": ":7", "WAYLAND_DISPLAY": "wayland-7"}
        ):
            x11 = backend.wine_environment("/tmp/prefix", self.wine, True)
            wayland = backend.wine_environment("/tmp/prefix", self.wine, False)

        self.assertEqual(x11["DISPLAY"], ":7")
        self.assertNotIn("WAYLAND_DISPLAY", x11)
        self.assertEqual(wayland["WAYLAND_DISPLAY"], "wayland-7")
        self.assertNotIn("DISPLAY", wayland)

    def test_existing_x11_prefix_gets_safe_xvidmode_default(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        missing = mock.Mock(returncode=1, stdout="", stderr="not found")
        added = mock.Mock(returncode=0, stdout="", stderr="")
        with mock.patch.object(
            backend.subprocess, "run", side_effect=[missing, added]
        ) as run:
            changed = backend.ensure_safe_x11_defaults(
                str(prefix), str(self.wine), True
            )

        self.assertTrue(changed)
        self.assertEqual(run.call_args_list[1].args[0], [
            str(self.wine), "reg", "add", backend.WINE_X11_DRIVER_KEY,
            "/v", backend.WINE_XVIDMODE_VALUE, "/t", "REG_SZ", "/d", "N", "/f",
        ])

    def test_existing_x11_prefix_preserves_explicit_xvidmode_choice(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        present = mock.Mock(returncode=0, stdout="UseXVidMode REG_SZ Y\n", stderr="")
        with mock.patch.object(
            backend.subprocess, "run", return_value=present
        ) as run:
            changed = backend.ensure_safe_x11_defaults(
                str(prefix), str(self.wine), True
            )

        self.assertFalse(changed)
        run.assert_called_once()

    def test_native_wayland_does_not_change_xvidmode(self):
        prefix = self._make_prefix(self.home / "selected-prefix")
        with mock.patch.object(backend.subprocess, "run") as run:
            changed = backend.ensure_safe_x11_defaults(
                str(prefix), str(self.wine), False
            )

        self.assertFalse(changed)
        run.assert_not_called()

    def test_native_mode_keeps_display_when_wayland_is_unavailable(self):
        with mock.patch.dict(os.environ, {"DISPLAY": ":7"}, clear=False):
            os.environ.pop("WAYLAND_DISPLAY", None)
            environment = backend.wine_environment("/tmp/prefix", self.wine, False)

        self.assertEqual(environment["DISPLAY"], ":7")


    def test_rejects_dangerous_prefixes(self):
        for value in ("/", "/usr", "/var", str(self.home), ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                backend.validate_prefix(value)

    def test_create_environment(self):
        prefix = self.home / ".wine4office"
        message = backend.create_environment(str(prefix), str(self.wine), False, lambda line: None)
        self.assertTrue(backend.has_wine_prefix_layout(prefix))
        self.assertIn(str(prefix), message)

    def test_create_environment_installs_both_bundled_gecko_architectures(self):
        prefix = self.home / ".wine4office"
        gecko = self.runner.parent / "share/wine/gecko"
        gecko.mkdir(parents=True)
        packages = [
            gecko / f"wine-gecko-{backend.WINE_GECKO_VERSION}-{architecture}.msi"
            for architecture in backend.WINE_GECKO_ARCHITECTURES
        ]
        for package in packages:
            package.write_bytes(b"msi")
        commands = []
        stream_command = backend._stream_command

        def capture(command, *args, **kwargs):
            commands.append(command)
            return stream_command(command, *args, **kwargs)

        with mock.patch.object(backend, "_stream_command", side_effect=capture):
            backend.create_environment(str(prefix), str(self.wine), False, lambda line: None)

        self.assertEqual(commands[1:3], [
            [str(self.wine), "msiexec", "/i", str(packages[0]), "/qn"],
            [str(self.wine), "msiexec", "/i", str(packages[1]), "/qn"],
        ])

    def test_create_environment_rejects_partial_bundled_gecko_set(self):
        prefix = self.home / ".wine4office"
        gecko = self.runner.parent / "share/wine/gecko"
        gecko.mkdir(parents=True)
        (gecko / f"wine-gecko-{backend.WINE_GECKO_VERSION}-x86.msi").write_bytes(b"msi")

        with self.assertRaisesRegex(FileNotFoundError, "Gecko package is missing"):
            backend.create_environment(str(prefix), str(self.wine), False, lambda line: None)

        self.assertFalse(prefix.exists())


    def test_stop_wine_gracefully_closes_windows_before_server(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        with mock.patch.object(backend.subprocess, "run") as run:
            backend.stop_wine(str(prefix), str(self.wine))

        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [
                [str(self.wine), "wine4officeclose.exe"],
                [str(self.runner / "wineserver"), "-w"],
            ],
        )
        self.assertTrue(run.call_args_list[0].kwargs["check"])
        self.assertEqual(run.call_args_list[0].kwargs["timeout"], 20)

    def test_stop_wine_hard_kills_when_graceful_close_fails(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        with mock.patch.object(
            backend.subprocess, "run",
            side_effect=[
                subprocess.CalledProcessError(2, "wine4officeclose.exe"),
                mock.DEFAULT,
                mock.DEFAULT,
            ],
        ) as run:
            backend.stop_wine(str(prefix), str(self.wine))

        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [
                [str(self.wine), "wine4officeclose.exe"],
                [str(self.runner / "wineserver"), "-k"],
                [str(self.runner / "wineserver"), "-w"],
            ],
        )

    def test_stop_wine_retries_sigkill_when_server_wait_times_out(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        timeout = subprocess.TimeoutExpired("wineserver -w", 15)
        with mock.patch.object(
            backend.subprocess, "run",
            side_effect=[
                mock.DEFAULT, timeout, mock.DEFAULT, timeout,
                mock.DEFAULT, mock.DEFAULT,
            ],
        ) as run:
            backend.stop_wine(str(prefix), str(self.wine))

        self.assertEqual(
            [call.args[0] for call in run.call_args_list][-2:],
            [
                [str(self.runner / "wineserver"), "-k9"],
                [str(self.runner / "wineserver"), "-w"],
            ],
        )

    def test_stop_wine_reports_missing_wineserver(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        (self.runner / "wineserver").unlink()

        with self.assertRaisesRegex(FileNotFoundError, "wineserver is missing"):
            backend.stop_wine(str(prefix), str(self.wine))

    def test_update_wine_prefix_runs_new_runner_wineboot_update(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        with mock.patch.object(backend, "_stream_command") as stream:
            result = backend.update_wine_prefix(
                str(prefix), str(self.wine), False, lambda _line: None
            )

        command, environment, output = stream.call_args.args
        self.assertEqual(command, [str(self.runner / "wineboot"), "-u"])
        self.assertEqual(environment["WINEPREFIX"], str(prefix.resolve()))
        self.assertNotIn("DISPLAY", environment)
        self.assertTrue(callable(output))
        self.assertIn("updated and restarted", result)

    def test_recreate_restores_old_environment_when_wineboot_fails(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix, "old")
        (prefix / "keep-me").write_text("important")
        with mock.patch.dict(os.environ, {"WINE4OFFICE_TEST_FAIL": "1"}):
            with self.assertRaises(Exception):
                backend.create_environment(str(prefix), str(self.wine), True, lambda line: None)
        self.assertEqual((prefix / "keep-me").read_text(), "important")
        self.assertFalse(list(prefix.parent.glob(".*.wine4office-backup-*")))

    def test_launches_exe_in_selected_environment_with_arguments(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        installer = self.home / "Downloads/Office Setup.exe"
        installer.parent.mkdir()
        installer.write_bytes(b"MZ")
        process = mock.Mock(pid=4321)
        with mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend.subprocess, "Popen", return_value=process) as popen:
            pid = backend.launch_executable(str(prefix), str(self.wine), str(installer),
                                            '/configure "/home/user/office.xml"')
        self.assertEqual(pid, 4321)
        command = popen.call_args.args[0]
        self.assertEqual(command, [str(self.wine), str(installer), "/configure", "/home/user/office.xml"])
        self.assertEqual(popen.call_args.kwargs["env"]["WINEPREFIX"], str(prefix))

    def test_all_backend_launch_paths_apply_selected_display_mode(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "EXCEL.EXE").write_bytes(b"exe")
        executable = self.home / "Downloads/Setup.exe"
        executable.parent.mkdir()
        executable.write_bytes(b"MZ")

        with mock.patch.dict(
            os.environ, {"DISPLAY": ":8", "WAYLAND_DISPLAY": "wayland-8"}
        ), mock.patch.object(
            backend.subprocess, "Popen", return_value=mock.Mock(pid=4321)
        ) as popen:
            backend.launch_app(str(prefix), str(self.wine), "excel", use_x11=False)
            backend.launch_tool(str(prefix), str(self.wine), "winecfg", use_x11=False)
            backend.launch_executable(
                str(prefix), str(self.wine), str(executable), use_x11=False
            )

        self.assertEqual(len(popen.call_args_list), 3)
        for call in popen.call_args_list:
            environment = call.kwargs["env"]
            self.assertEqual(environment["WAYLAND_DISPLAY"], "wayland-8")
            self.assertNotIn("DISPLAY", environment)

    def test_executable_launcher_forwards_valid_working_directory(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        installer = self.home / "Downloads/Office Setup.exe"
        installer.parent.mkdir()
        installer.write_bytes(b"MZ")
        working_directory = self.home / "installer files"
        working_directory.mkdir()

        with mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(
                 backend.subprocess, "Popen", return_value=mock.Mock(pid=4321)
             ) as popen:
            backend.launch_executable(
                str(prefix),
                str(self.wine),
                str(installer),
                working_directory=str(working_directory),
            )

        self.assertEqual(popen.call_args.kwargs["cwd"], working_directory.resolve())

    def test_executable_launcher_empty_working_directory_defaults_to_executable_folder(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        installer = self.home / "Downloads/Office Setup.exe"
        installer.parent.mkdir()
        installer.write_bytes(b"MZ")

        with mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(
                 backend.subprocess, "Popen", return_value=mock.Mock(pid=4321)
             ) as popen:
            backend.launch_executable(
                str(prefix),
                str(self.wine),
                str(installer),
                working_directory="   ",
            )

        self.assertEqual(popen.call_args.kwargs["cwd"], installer.parent.resolve())

    def test_executable_launcher_rejects_missing_working_directory(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        installer = self.home / "Downloads/Office Setup.exe"
        installer.parent.mkdir()
        installer.write_bytes(b"MZ")
        missing = self.home / "missing working folder"

        with mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend.subprocess, "Popen") as popen, \
             self.assertRaisesRegex(NotADirectoryError, "Working directory was not found"):
            backend.launch_executable(
                str(prefix),
                str(self.wine),
                str(installer),
                working_directory=str(missing),
            )

        popen.assert_not_called()

    def test_executable_launcher_rejects_non_exe(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        document = self.home / "Downloads/readme.txt"
        document.parent.mkdir()
        document.write_text("not an exe")
        with self.assertRaisesRegex(ValueError, "Only .exe"):
            backend.launch_executable(str(prefix), str(self.wine), str(document))

    def test_command_prompt_opens_in_system_terminal(self):
        prefix = self.home / ".wine4office"
        process = mock.Mock(pid=8765)

        def which(name, path=None):
            return "/usr/bin/konsole" if name == "konsole" else None

        with mock.patch.object(backend.shutil, "which", side_effect=which), \
             mock.patch.object(backend.subprocess, "Popen", return_value=process) as popen:
            pid = backend.launch_tool(str(prefix), str(self.wine), "cmd")

        self.assertEqual(pid, 8765)
        self.assertEqual(
            popen.call_args.args[0],
            ["/usr/bin/konsole", "--hold", "-e", str(self.wine), "cmd.exe"],
        )
        self.assertEqual(popen.call_args.kwargs["env"]["WINEPREFIX"], str(prefix))
        self.assertEqual(popen.call_args.kwargs["env"]["WINEDEBUG"], "-all")

    def test_command_prompt_reports_missing_system_terminal(self):
        with mock.patch.object(backend.shutil, "which", return_value=None):
            with self.assertRaisesRegex(FileNotFoundError, "No supported system terminal"):
                backend.launch_tool(str(self.home / ".wine4office"), str(self.wine), "cmd")

    def test_office_detection_and_shortcut_lifecycle(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "WINWORD.EXE").write_bytes(b"exe")
        (office / "EXCEL.EXE").write_bytes(b"exe")
        (office / "POWERPNT.EXE").write_bytes(b"exe")
        status = backend.environment_status(str(prefix), str(self.wine))
        self.assertTrue(status["apps"]["word"])
        self.assertTrue(status["apps"]["excel"])
        self.assertTrue(status["apps"]["powerpoint"])
        self.assertEqual(backend.find_office_app(str(prefix), "word"), office / "WINWORD.EXE")
        font_helper = self.root / "transient-bundle/register-office-cloud-fonts.sh"
        font_helper.parent.mkdir(parents=True)
        font_helper.write_text("#!/bin/sh\nexit 0\n")
        font_helper.chmod(0o755)

        def fake_extract(executable, destination):
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(b"icon")
            return destination

        with mock.patch.object(backend, "extract_office_icon", side_effect=fake_extract) as extract:
            created = backend.create_app_shortcuts(
                ["word"], str(prefix), str(self.wine), True, helper=font_helper,
            )
        self.assertEqual(len(created), 2)
        extract.assert_called_once_with(
            office / "WINWORD.EXE", backend.data_home() / "icons/wine4office/word.ico",
        )
        menu = backend.data_home() / "applications/wine4office-word.desktop"
        launcher = backend.shortcut_launcher_path("word")
        text = menu.read_text()
        self.assertIn("X-Wine4Office-Managed=true", text)
        self.assertIn("%U", text)
        self.assertIn(f'Exec="{launcher}" %U', text)
        self.assertIn(f"Icon={backend.data_home() / 'icons/wine4office/word.ico'}", text)
        self.assertTrue(os.access(launcher, os.X_OK))
        launcher_text = launcher.read_text()
        self.assertIn("# X-Wine4Office-Managed=true", launcher_text)
        self.assertIn(f"prefix={shlex.quote(str(prefix))}", launcher_text)
        self.assertNotIn("wine4office_manager.py", launcher_text)
        installed_helper = (
            backend.shortcut_launcher_directory() / "register-office-cloud-fonts.sh"
        )
        self.assertEqual(installed_helper.read_text(), font_helper.read_text())
        removed = backend.remove_app_shortcuts(["word"])
        self.assertEqual(len(removed), 2)
        self.assertFalse(menu.exists())
        self.assertFalse(launcher.exists())
        self.assertFalse(installed_helper.exists())

    def test_teams_detection_prefers_newest_windowsapps_package_and_creates_shortcut(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        windows_apps = prefix / "drive_c/Program Files/WindowsApps"
        older = windows_apps / "MSTeams_24193.1805.3040.8975_x64__8wekyb3d8bbwe"
        newer = windows_apps / "MSTeams_24295.605.3225.8804_x64__8wekyb3d8bbwe"
        older.mkdir(parents=True)
        newer.mkdir()
        (older / "ms-teams.exe").write_bytes(b"old")
        teams = newer / "ms-teams.exe"
        teams.write_bytes(b"new")

        status = backend.environment_status(str(prefix), str(self.wine))
        self.assertTrue(status["apps"]["teams"])
        self.assertEqual(backend.find_office_app(str(prefix), "teams"), teams)

        with mock.patch.object(
            backend, "extract_office_icon", side_effect=lambda _exe, icon: (
                icon.parent.mkdir(parents=True, exist_ok=True), icon.write_bytes(b"icon"), icon
            )[-1]
        ):
            created = backend.create_app_shortcuts(
                ["teams"], str(prefix), str(self.wine), False,
            )

        self.assertEqual(len(created), 1)
        desktop_file = backend.data_home() / "applications/wine4office-teams.desktop"
        launcher = backend.shortcut_launcher_path("teams")
        self.assertIn("Name=Microsoft Teams (Wine4Office)", desktop_file.read_text())
        self.assertIn(f"executable={shlex.quote(str(teams))}", launcher.read_text())
        self.assertNotIn("%U", desktop_file.read_text())

    def test_additional_office_applications_are_detected(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        executables = {
            "access": "MSACCESS.EXE",
            "onenote": "ONENOTE.EXE",
            "publisher": "MSPUB.EXE",
            "visio": "VISIO.EXE",
            "project": "WINPROJ.EXE",
        }
        for executable in executables.values():
            (office / executable).write_bytes(b"exe")

        status = backend.environment_status(str(prefix), str(self.wine))

        for app, executable in executables.items():
            with self.subTest(app=app):
                self.assertTrue(status["apps"][app])
                self.assertEqual(
                    backend.find_office_app(str(prefix), app), office / executable
                )
                self.assertEqual(backend.APP_META[app]["compatibility"], "Not tested")

    def test_generated_launcher_uses_current_configured_display_mode_and_documents(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        word = office / "WINWORD.EXE"
        word.write_bytes(b"exe")
        log = self.root / "launch.log"
        self._script(
            "wine",
            "#!/bin/bash\n"
            "printf 'DISPLAY=%s\\n' \"${DISPLAY-unset}\" > \"$WINE4OFFICE_TEST_LOG\"\n"
            "printf 'WAYLAND_DISPLAY=%s\\n' \"${WAYLAND_DISPLAY-unset}\" >> \"$WINE4OFFICE_TEST_LOG\"\n"
            "printf 'ARG=%s\\n' \"$@\" >> \"$WINE4OFFICE_TEST_LOG\"\n",
        )
        self._script("winepath", "#!/bin/bash\nprintf 'Z:\\\\converted\\\\%s\\n' \"${2##*/}\"\n")
        icon = backend.data_home() / "icons/wine4office/word.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")
        config = backend.default_config()
        config["use_x11"] = False
        backend.save_config(config)

        backend.create_app_shortcuts(["word"], prefix, self.wine, False)
        launcher = backend.shortcut_launcher_path("word")
        document = self.home / "Documents/מסמך with spaces.docx"
        environment = os.environ.copy()
        environment.update({
            "DISPLAY": ":77",
            "WAYLAND_DISPLAY": "wayland-77",
            "WINE4OFFICE_TEST_LOG": str(log),
        })
        subprocess.run([str(launcher), str(document)], env=environment, check=True)

        output = log.read_text()
        self.assertIn("DISPLAY=unset", output)
        self.assertIn("WAYLAND_DISPLAY=wayland-77", output)
        self.assertIn(f"ARG={word}", output)
        self.assertIn("ARG=Z:\\converted\\מסמך with spaces.docx", output)

    def test_generated_launcher_materializes_kio_url_before_winepath(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        word = office / "WINWORD.EXE"
        word.write_bytes(b"exe")
        source = self.root / "remote inside.docx"
        source.write_bytes(b"document")
        runtime = self.root / "runtime"
        runtime.mkdir()
        log = self.root / "launch.log"
        self._script(
            "wine",
            "#!/bin/bash\n"
            "printf 'ARG=%s\\n' \"$@\" > \"$WINE4OFFICE_TEST_LOG\"\n",
        )
        self._script(
            "winepath",
            "#!/bin/bash\n"
            "printf 'Z:\\\\converted\\\\%s\\n' \"${2##*/}\"\n",
        )
        self._script(
            "kioclient",
            "#!/bin/bash\n"
            "printf 'KIO=%s\\n' \"$2\" > \"$WINE4OFFICE_TEST_KIO_LOG\"\n"
            "cp \"$WINE4OFFICE_TEST_REMOTE_DOCUMENT\" \"$3/inside.docx\"\n",
        )
        icon = backend.data_home() / "icons/wine4office/word.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        backend.create_app_shortcuts(["word"], prefix, self.wine, False)
        environment = os.environ.copy()
        environment.update({
            "XDG_RUNTIME_DIR": str(runtime),
            "WINE4OFFICE_TEST_LOG": str(log),
            "WINE4OFFICE_TEST_KIO_LOG": str(self.root / "kio.log"),
            "WINE4OFFICE_TEST_REMOTE_DOCUMENT": str(source),
        })
        archive_url = "zip:/home/user/archive.zip/inside.docx"
        subprocess.run(
            [str(backend.shortcut_launcher_path("word")), archive_url],
            env=environment, check=True,
        )

        self.assertIn("KIO=" + archive_url, (self.root / "kio.log").read_text())
        self.assertIn(f"ARG={word}", log.read_text())
        self.assertIn("ARG=Z:\\converted\\inside.docx", log.read_text())

    def test_generated_launcher_decodes_local_file_url_without_copy(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        word = office / "WINWORD.EXE"
        word.write_bytes(b"exe")
        log = self.root / "launch.log"
        self._script(
            "wine",
            "#!/bin/bash\nprintf 'ARG=%s\\n' \"$@\" > \"$WINE4OFFICE_TEST_LOG\"\n",
        )
        self._script(
            "winepath",
            "#!/bin/bash\nprintf 'Z:\\\\converted\\\\%s\\n' \"${2##*/}\"\n",
        )
        icon = backend.data_home() / "icons/wine4office/word.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        backend.create_app_shortcuts(["word"], prefix, self.wine, False)
        environment = os.environ.copy()
        environment["WINE4OFFICE_TEST_LOG"] = str(log)
        subprocess.run(
            [str(backend.shortcut_launcher_path("word")),
             "file:///home/user/My%20Document.docx"],
            env=environment, check=True,
        )

        self.assertIn("ARG=Z:\\converted\\My Document.docx", log.read_text())

    def test_generated_outlook_launcher_initializes_language_and_overrides_mshtml(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        outlook = office / "OUTLOOK.EXE"
        outlook.write_bytes(b"exe")
        runner = self.root / "runner with spaces/bin"
        runner.mkdir(parents=True)
        wine = runner / "wine"
        log = self.root / "outlook-launch.log"
        wine.write_text(
            "#!/bin/bash\n"
            "if [[ ${1:-} == reg && ${2:-} == query && ${5:-} == LastUILanguage ]]; then\n"
            "    exit 1\n"
            "fi\n"
            "if [[ ${1:-} == reg && ${2:-} == query && ${5:-} == Locale ]]; then\n"
            "    printf 'Locale    REG_SZ    0000040d\\n'\n"
            "    exit 0\n"
            "fi\n"
            "if [[ ${1:-} == reg && ${2:-} == add ]]; then\n"
            "    printf 'REG=%s\\n' \"$*\" >> \"$WINE4OFFICE_TEST_LOG\"\n"
            "    exit 0\n"
            "fi\n"
            "printf 'OVERRIDES=%s\\n' \"$WINEDLLOVERRIDES\" >> \"$WINE4OFFICE_TEST_LOG\"\n"
            "printf 'ARG=%s\\n' \"$@\" >> \"$WINE4OFFICE_TEST_LOG\"\n"
        )
        wine.chmod(0o755)
        icon = backend.data_home() / "icons/wine4office/outlook.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        backend.create_app_shortcuts(["outlook"], prefix, wine, False)
        environment = os.environ.copy()
        environment.update({
            "WINE4OFFICE_TEST_LOG": str(log),
            "WINEDLLOVERRIDES": "riched20=n;mshtml=b;custom=n",
        })
        subprocess.run(
            [str(backend.shortcut_launcher_path("outlook"))],
            env=environment,
            check=True,
        )

        output = log.read_text()
        self.assertIn("/d 1037 /f", output)
        self.assertIn("OVERRIDES=riched20=n;custom=n;mshtml=", output)
        self.assertNotIn("mshtml=b", output)
        self.assertIn(f"ARG={outlook}", output)

    def test_shortcut_creation_accepts_path_environment_and_wine(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "WINWORD.EXE").write_bytes(b"exe")
        icon = backend.data_home() / "icons/wine4office/word.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        created = backend.create_app_shortcuts(
            ["word"], prefix, self.wine, False,
        )

        menu = backend.data_home() / "applications/wine4office-word.desktop"
        self.assertEqual(created, [str(menu)])
        self.assertIn(
            f'Exec="{backend.shortcut_launcher_path("word")}" %U', menu.read_text()
        )
        self.assertIn(
            f"wine={shlex.quote(str(self.wine))}",
            backend.shortcut_launcher_path("word").read_text(),
        )

    def test_excel_shortcut_registers_csv_mime_type(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "EXCEL.EXE").write_bytes(b"exe")
        icon = backend.data_home() / "icons/wine4office/excel.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        backend.create_app_shortcuts(["excel"], prefix, self.wine, False)

        menu = backend.data_home() / "applications/wine4office-excel.desktop"
        self.assertIn("MimeType=text/csv;", menu.read_text())

    def test_post_install_refreshes_only_existing_managed_shortcuts(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        word = office / "WINWORD.EXE"
        word.write_bytes(b"exe")
        icon = backend.data_home() / "icons/wine4office/word.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")
        legacy_manager = self.root / "Wine4OfficeManager"
        menu = backend.data_home() / "applications/wine4office-word.desktop"
        desktop = backend.desktop_directory() / "wine4office-word.desktop"
        for path in (menu, desktop):
            backend.write_desktop_file(
                path, "Microsoft Word (Wine4Office)", "Legacy shortcut",
                [str(legacy_manager), "--prefix", str(prefix), "word"],
                icon, "Office;WordProcessor;", backend.APP_META["word"]["mime"],
            )
        unrelated = (
            backend.data_home() / "applications/wine4office-powerpoint.desktop"
        )
        unrelated.parent.mkdir(parents=True, exist_ok=True)
        unrelated.write_text("[Desktop Entry]\nName=User shortcut\n")

        result = backend.refresh_managed_app_shortcuts(prefix, self.wine)

        launcher = backend.shortcut_launcher_path("word")
        self.assertEqual(set(result["updated"]), {str(menu), str(desktop)})
        self.assertEqual(result["skipped"], {})
        self.assertIn(f'Exec="{launcher}" %U', menu.read_text())
        self.assertIn(f'Exec="{launcher}" %U', desktop.read_text())
        self.assertTrue(launcher.is_file())
        self.assertFalse(
            (backend.data_home() / "applications/wine4office-excel.desktop").exists()
        )
        self.assertEqual(
            unrelated.read_text(), "[Desktop Entry]\nName=User shortcut\n"
        )

    def test_post_install_keeps_managed_shortcut_when_office_app_is_missing(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        icon = self.root / "outlook.ico"
        icon.write_bytes(b"cached icon")
        menu = backend.data_home() / "applications/wine4office-outlook.desktop"
        backend.write_desktop_file(
            menu, "Microsoft Outlook (Wine4Office)", "Legacy shortcut",
            ["/old/Wine4OfficeManager", "outlook"],
            icon, "Office;Email;Network;", backend.APP_META["outlook"]["mime"],
        )
        original = menu.read_text()

        result = backend.refresh_managed_app_shortcuts(prefix, self.wine)

        self.assertEqual(result["updated"], [])
        self.assertIn("outlook", result["skipped"])
        self.assertEqual(menu.read_text(), original)
        self.assertFalse(backend.shortcut_launcher_path("outlook").exists())

    def test_setlang_detection_shortcut_and_launch(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        setlang = office / "SETLANG.EXE"
        setlang.write_bytes(b"exe")
        icon = backend.data_home() / "icons/wine4office/setlang.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        status = backend.environment_status(str(prefix), str(self.wine))
        self.assertTrue(status["apps"]["setlang"])
        created = backend.create_app_shortcuts(
            ["setlang"], prefix, self.wine, False,
        )

        menu = backend.data_home() / "applications/wine4office-setlang.desktop"
        self.assertEqual(created, [str(menu)])
        shortcut = menu.read_text()
        self.assertIn(str(backend.shortcut_launcher_path("setlang")), shortcut)
        self.assertNotIn("%F", shortcut)
        self.assertNotIn("MimeType=", shortcut)

        process = mock.Mock(pid=3210)
        with mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend.subprocess, "Popen", return_value=process) as popen:
            pid = backend.launch_app(str(prefix), str(self.wine), "setlang")
        self.assertEqual(pid, 3210)
        self.assertEqual(popen.call_args.args[0], [str(self.wine), str(setlang)])

    def test_manager_shortcut_installs_a_persistent_icon(self):
        launcher = self.root / "Wine4OfficeManager"
        launcher.write_bytes(b"manager")
        launcher.chmod(0o755)
        bundled_icons = self.root / "transient-bundle/icons"
        bundled_icons.mkdir(parents=True)
        source_icon = bundled_icons / "wine4office-manager.png"
        source_icon.write_bytes(b"\x89PNG\r\n\x1a\nmanager")

        shortcut = backend.install_manager_shortcut(launcher, bundled_icons)
        installed_icon = (
            backend.data_home() / "icons/wine4office"
            / "wine4office-manager-003dc05195a67b77.png"
        )
        text = shortcut.read_text()
        self.assertEqual(installed_icon.read_bytes(), b"\x89PNG\r\n\x1a\nmanager")
        self.assertIn("Name=Wine4Office Manager", text)
        self.assertIn(f"Icon={installed_icon}", text)
        self.assertNotIn(str(bundled_icons), text)

    def test_manager_shortcut_refresh_updates_name_and_busts_icon_cache(self):
        launcher = self.root / "Wine4OfficeManager"
        launcher.write_bytes(b"manager")
        launcher.chmod(0o755)
        bundled_icons = self.root / "bundle/icons"
        bundled_icons.mkdir(parents=True)
        source_icon = bundled_icons / "wine4office-manager.png"
        source_icon.write_bytes(b"old icon")
        shortcut = backend.install_manager_shortcut(launcher, bundled_icons)
        old_icon = Path(next(
            line.removeprefix("Icon=") for line in shortcut.read_text().splitlines()
            if line.startswith("Icon=")
        ))

        source_icon.write_bytes(b"new icon")
        result = backend.refresh_manager_shortcut(
            [str(launcher), "--manager"], bundled_icons
        )

        text = shortcut.read_text()
        new_icon = Path(next(
            line.removeprefix("Icon=") for line in text.splitlines()
            if line.startswith("Icon=")
        ))
        self.assertTrue(result["updated"])
        self.assertNotEqual(new_icon, old_icon)
        self.assertFalse(old_icon.exists())
        self.assertEqual(new_icon.read_bytes(), b"new icon")
        self.assertIn("Name=Wine4Office Manager", text)
        self.assertIn(
            f'Exec="{launcher}" "--manager"',
            text,
        )

    def test_cloud_font_registration_skips_unchanged_registry_import(self):
        prefix = self.home / ".wine4office"
        cloud_font = (
            prefix / "drive_c/users/tester/AppData/Local/Microsoft/FontCache/4"
            / "CloudFonts/Aptos/aptos.ttf"
        )
        cloud_font.parent.mkdir(parents=True)
        cloud_font.write_bytes(b"first font revision")
        log = self.root / "wine.log"
        fc_log = self.root / "fc-scan.log"
        self._script(
            "fc-scan",
            "#!/bin/sh\n"
            "printf 'scan\\n' >> \"$WINE4OFFICE_FC_LOG\"\n"
            "printf 'Aptos\\n'\n",
        )
        self._script("winepath", "#!/bin/sh\nprintf 'Z:\\\\tmp\\\\fonts.reg\\n'\n")
        self._script("wine", "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$WINE4OFFICE_TEST_LOG\"\n")
        helper = Path(__file__).resolve().parents[1] / "register-office-cloud-fonts.sh"
        env = os.environ.copy()
        env.update({
            "PATH": f"{self.runner}:{env['PATH']}",
            "WINEPREFIX": str(prefix),
            "WINE4OFFICE_TEST_LOG": str(log),
            "WINE4OFFICE_FC_LOG": str(fc_log),
        })

        subprocess.run([str(helper)], env=env, check=True, stdout=subprocess.PIPE, text=True)
        subprocess.run([str(helper)], env=env, check=True, stdout=subprocess.PIPE, text=True)
        self.assertEqual(log.read_text().splitlines(), ["regedit /S Z:\\tmp\\fonts.reg"])
        self.assertEqual(fc_log.read_text().splitlines(), ["scan"])

        cloud_font.write_bytes(b"other font revision")
        subprocess.run([str(helper)], env=env, check=True, stdout=subprocess.PIPE, text=True)
        self.assertEqual(
            log.read_text().splitlines(),
            ["regedit /S Z:\\tmp\\fonts.reg", "regedit /S Z:\\tmp\\fonts.reg"],
        )
        self.assertEqual(fc_log.read_text().splitlines(), ["scan", "scan"])

    def test_launch_app_converts_host_document_paths_for_office(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        word = office / "WINWORD.EXE"
        word.write_bytes(b"exe")
        winepath = self._script("winepath", "#!/bin/sh\nexit 0\n")
        documents = [
            self.home / "Documents/Report with spaces.docx",
            self.home / "Documents/מסמך שני.docx",
        ]
        converted = [
            "Z:\\home\\Documents\\Report with spaces.docx\n",
            "Z:\\home\\Documents\\מסמך שני.docx\n",
        ]
        process = mock.Mock(pid=7654)
        with mock.patch.object(backend, "ensure_safe_x11_defaults"), mock.patch.object(
            backend.subprocess, "run",
            side_effect=[mock.Mock(stdout=value) for value in converted],
        ) as run, mock.patch.object(
            backend.subprocess, "Popen", return_value=process,
        ) as popen:
            pid = backend.launch_app(
                str(prefix), str(self.wine), "word", documents=map(str, documents),
            )

        self.assertEqual(pid, 7654)
        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [[str(winepath), "-w", str(document)] for document in documents],
        )
        self.assertEqual(
            popen.call_args.args[0],
            [str(self.wine), str(word), *(value.rstrip("\n") for value in converted)],
        )

    def test_outlook_first_run_sets_language_and_safe_launch_options(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        outlook = office / "OUTLOOK.EXE"
        outlook.write_bytes(b"exe")
        process = mock.Mock(pid=7654)
        run_results = [
            mock.Mock(returncode=1, stdout=""),
            mock.Mock(returncode=0, stdout="Locale    REG_SZ    0000040d\n"),
            mock.Mock(returncode=0, stdout=""),
        ]
        with mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend.subprocess, "run", side_effect=run_results) as run, \
             mock.patch.object(backend.subprocess, "Popen", return_value=process) as popen:
            pid = backend.launch_app(str(prefix), str(self.wine), "outlook")
        self.assertEqual(pid, 7654)
        self.assertEqual(run.call_args_list[2].args[0][-5:],
                         ["/t", "REG_DWORD", "/d", "1037", "/f"])
        command = popen.call_args.args[0]
        self.assertEqual(command, [str(self.wine), str(outlook)])
        self.assertIn("mshtml=", popen.call_args.kwargs["env"]["WINEDLLOVERRIDES"].split(";"))
        self.assertNotIn("mshtml=b", popen.call_args.kwargs["env"]["WINEDLLOVERRIDES"].split(";"))

    def test_shortcut_creation_rejects_missing_wine(self):
        with self.assertRaisesRegex(FileNotFoundError, "Wine executable is missing"):
            backend.create_app_shortcuts(
                ["word"], str(self.home / ".wine4office"),
                str(self.home / "missing-wine"), False,
            )

    def test_shortcut_removal_does_not_delete_unowned_file(self):
        path = backend.data_home() / "applications/wine4office-excel.desktop"
        path.parent.mkdir(parents=True)
        path.write_text("[Desktop Entry]\nName=Someone else\n")
        self.assertEqual(backend.remove_app_shortcuts(["excel"]), [])
        self.assertTrue(path.exists())

    def test_classifies_every_environment_target(self):
        missing = self.home / "missing"
        empty = self.home / "empty"
        valid = self.home / "valid"
        unsafe = self.home / "unsafe"
        empty.mkdir()
        self._make_prefix(valid)
        unsafe.mkdir()
        (unsafe / "personal-file").write_text("do not delete")

        self.assertEqual(backend.classify_prefix(str(missing)), "missing")
        self.assertEqual(backend.classify_prefix(str(empty)), "empty")
        self.assertEqual(backend.classify_prefix(str(valid)), "valid")
        self.assertEqual(backend.classify_prefix(str(unsafe)), "unsafe")

    def test_path_equivalence_resolves_symlinks_and_same_inode(self):
        prefix = self.home / "real-prefix"
        prefix.mkdir()
        alias = self.home / "prefix-alias"
        alias.symlink_to(prefix, target_is_directory=True)

        self.assertTrue(backend.paths_equivalent(str(prefix), str(alias)))
        self.assertTrue(backend.paths_equivalent(str(prefix), f"  {prefix}  "))
        with mock.patch.object(os.path, "samefile", return_value=True) as samefile:
            self.assertTrue(backend.paths_equivalent(str(prefix), str(self.home / "./real-prefix")))
        samefile.assert_called_once()

    def test_deletion_rejects_aliases_nested_paths_and_unsafe_roots(self):
        old = self.home / "container/old-prefix"
        self._make_prefix(old)
        alias = self.home / "old-alias"
        alias.symlink_to(old, target_is_directory=True)

        for target in (alias, old / "nested", old.parent):
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, "overlapping"):
                backend.validate_environment_deletion(str(old), str(target))
        with self.assertRaisesRegex(ValueError, "unsafe"):
            backend.validate_environment_deletion(str(old), str(self.home))

    def test_staged_deletion_stops_only_the_old_prefix(self):
        old = self.home / "old-prefix"
        new = self.home / "new-prefix"
        for prefix in (old, new):
            self._make_prefix(prefix)

        with mock.patch.object(backend, "stop_wine") as stop:
            staged = backend.stage_environment_deletion(
                str(old), str(new), str(self.wine), lambda line: None
            )
        stop.assert_called_once_with(str(old.resolve()), str(self.wine))
        self.assertFalse(old.exists())
        self.assertTrue(staged.exists())
        backend.restore_staged_environment(staged, str(old))
        self.assertTrue((old / "system.reg").is_file())

    def test_prefix_classification_requires_every_layout_marker(self):
        markers = {
            "system.reg": False,
            "user.reg": False,
            "drive_c": True,
            "dosdevices": True,
        }
        for marker, is_directory in markers.items():
            with self.subTest(marker=marker):
                prefix = self._make_prefix(self.home / f"missing-{marker}")
                path = prefix / marker
                path.rmdir() if is_directory else path.unlink()
                self.assertEqual(backend.classify_prefix(str(prefix)), "unsafe")

    def test_recursive_deletion_rejects_directory_with_only_system_registry(self):
        ordinary = self.home / "ordinary-directory"
        ordinary.mkdir()
        (ordinary / "system.reg").write_text("not enough")
        important = ordinary / "important"
        important.write_text("preserve")

        self.assertEqual(backend.classify_prefix(str(ordinary)), "unsafe")
        with mock.patch.object(backend.shutil, "rmtree") as rmtree:
            with self.assertRaisesRegex(ValueError, "not a Wine prefix"):
                backend.discard_initialized_environment(str(ordinary), lambda line: None)
            with self.assertRaisesRegex(ValueError, "not a Wine prefix"):
                backend.finish_staged_environment_deletion(ordinary, lambda line: None)
        rmtree.assert_not_called()
        self.assertEqual(important.read_text(), "preserve")

    def test_uninstaller_authorizes_prefix_removal_only_for_complete_layout(self):
        root = self.home / "installed"
        uninstaller = root / "bin/wine4office-uninstall"
        uninstaller.parent.mkdir(parents=True)
        uninstaller.write_text("#!/bin/sh\n")
        uninstaller.chmod(uninstaller.stat().st_mode | stat.S_IXUSR)
        ordinary = self.home / "ordinary-removal-target"
        ordinary.mkdir()
        (ordinary / "system.reg").write_text("not enough")

        with mock.patch.object(backend, "installed_root", return_value=root), \
             mock.patch.object(backend, "_stream_command") as stream:
            with self.assertRaisesRegex(ValueError, "not a Wine prefix"):
                backend.remove_wine4office(str(ordinary), True, lambda line: None)
            stream.assert_not_called()

            self._make_prefix(ordinary)
            backend.remove_wine4office(str(ordinary), True, lambda line: None)

        self.assertEqual(
            stream.call_args.args[0],
            [str(uninstaller), "--purge-runner", "--remove-prefix", str(ordinary.resolve())],
        )

    def test_initialization_cancellation_removes_only_the_new_target(self):
        prefix = self.home / "cancelled-prefix"
        cancel = mock.Mock()
        cancel.is_set.return_value = True

        with self.assertRaisesRegex(RuntimeError, "cancelled"):
            backend.create_environment(
                str(prefix), str(self.wine), False, lambda line: None, cancel
            )
        self.assertFalse(prefix.exists())

    def test_nonempty_invalid_target_is_never_initialized(self):
        target = self.home / "documents"
        target.mkdir()
        important = target / "important"
        important.write_text("keep")

        with self.assertRaisesRegex(FileExistsError, "not a Wine prefix"):
            backend.create_environment(str(target), str(self.wine), False, lambda line: None)
        self.assertEqual(important.read_text(), "keep")

    def test_successful_commands_must_still_produce_a_valid_prefix(self):
        target = self.home / "empty-target"
        target.mkdir()

        with mock.patch.object(backend, "_stream_command"), \
             self.assertRaisesRegex(RuntimeError, "did not create a valid prefix"):
            backend.create_environment(str(target), str(self.wine), False, lambda line: None)

        self.assertFalse(target.exists())

    def test_office_language_separators_are_normalized(self):
        self.assertEqual(
            backend.validate_office_languages(" EN-us,\tde-DE; fr-FR\npt-BR "),
            ["en-us", "de-de", "fr-fr", "pt-br"],
        )

    def test_office_languages_reject_unknown_and_duplicate_identifiers(self):
        for languages, message in (
            ("en-US, xx-XX", "Unsupported"),
            ("en-US; EN-us", "Duplicate"),
        ):
            with self.subTest(languages=languages), self.assertRaisesRegex(ValueError, message):
                backend.validate_office_languages(languages)

    def test_generated_office_configuration_uses_product_channel_and_languages(self):
        cases = (
            ("O365ProPlusRetail", "Current", ["en-us", "de-de"]),
            ("ProPlus2024Volume", "PerpetualVL2024", ["fr-fr"]),
            ("VisioProRetail", "Current", ["en-us"]),
            ("ProjectProRetail", "Current", ["en-us"]),
            ("VisioPro2024Volume", "PerpetualVL2024", ["en-us"]),
            ("ProjectPro2024Volume", "PerpetualVL2024", ["en-us"]),
        )
        for product_id, channel, languages in cases:
            with self.subTest(product_id=product_id):
                root = ET.fromstring(
                    backend.build_office_configuration(product_id, languages)
                )
                add = root.find("Add")
                self.assertIsNotNone(add)
                self.assertEqual(
                    add.attrib,
                    {"OfficeClientEdition": "64", "Channel": channel},
                )
                product = add.find("Product")
                self.assertIsNotNone(product)
                self.assertEqual(product.attrib, {"ID": product_id})
                self.assertEqual(
                    [language.attrib for language in product.findall("Language")],
                    [{"ID": language} for language in languages],
                )
                display = root.find("Display")
                self.assertIsNotNone(display)
                self.assertEqual(display.attrib, {"Level": "Full"})
                self.assertNotIn("AcceptEULA", display.attrib)

    def test_custom_office_configuration_is_preserved_byte_for_byte(self):
        configuration = self.home / "custom deployment.xml"
        payload = (
            b'\xef\xbb\xbf<Configuration><Add SourcePath="Z:\\\\Office">'
            b'<Product ID="ProjectPro2024Volume" /></Add></Configuration>'
        )
        configuration.write_bytes(payload)

        validated = backend.validate_office_configuration(configuration)

        self.assertEqual(validated, configuration.resolve())
        self.assertEqual(configuration.read_bytes(), payload)

    def test_load_office_configuration_returns_exact_payload_and_digest(self):
        configuration = self.home / "loaded deployment.xml"
        payload = (
            b'\xef\xbb\xbf<Configuration><Add Channel="Current" />'
            b"<Display Level=\"Full\" /></Configuration>"
        )
        configuration.write_bytes(payload)

        loaded_path, loaded_payload, digest = backend.load_office_configuration(
            configuration
        )

        self.assertEqual(loaded_path, configuration.resolve())
        self.assertIsInstance(loaded_payload, bytes)
        self.assertEqual(loaded_payload, payload)
        self.assertEqual(digest, hashlib.sha256(payload).hexdigest())

    def test_office_configuration_rejects_unsafe_root_and_doctype(self):
        configuration = self.home / "unsafe.xml"
        for payload, message in (
            ("<NotConfiguration />", "root must be Configuration"),
            (
                '<!DOCTYPE Configuration [<!ENTITY secret SYSTEM "file:///etc/passwd">]>'
                "<Configuration />",
                "document type or entities",
            ),
        ):
            with self.subTest(message=message):
                configuration.write_text(payload, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    backend.validate_office_configuration(configuration)

    def test_office_configuration_rejects_destructive_directives(self):
        configuration = self.home / "destructive.xml"
        cases = (
            (
                '<Configuration xmlns="urn:office"><Remove /></Configuration>',
                "destructive Remove directives",
            ),
            (
                "<Configuration><RemoveMSI /></Configuration>",
                "destructive RemoveMSI directives",
            ),
            (
                '<Configuration xmlns:odt="urn:office">'
                '<Add odt:MigrateArch="TrUe" /></Configuration>',
                "destructive architecture migration",
            ),
            (
                "<Configuration>"
                '<Property NAME="forceappshutdown" Value="TRUE" />'
                "</Configuration>",
                "force running Office applications to close",
            ),
        )
        for payload, message in cases:
            with self.subTest(message=message):
                configuration.write_text(payload, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    backend.validate_office_configuration(configuration)

    def test_office_configuration_atomic_delete_removes_unchanged_file(self):
        configuration = self.home / "unchanged deployment.xml"
        payload = b"<Configuration />"
        configuration.write_bytes(payload)

        deleted, preserved = backend.delete_office_configuration_if_unchanged(
            configuration, hashlib.sha256(payload).hexdigest()
        )

        self.assertTrue(deleted)
        self.assertIsNone(preserved)
        self.assertFalse(configuration.exists())
        self.assertFalse(
            list(configuration.parent.glob(
                f".{configuration.name}.wine4office-preserved-*"
            ))
        )

    def test_office_configuration_atomic_delete_restores_changed_file_when_path_is_free(self):
        configuration = self.home / "changed deployment.xml"
        approved_payload = b"<Configuration />"
        changed_payload = b"<Configuration><Display Level=\"Full\" /></Configuration>"
        configuration.write_bytes(changed_payload)

        deleted, preserved = backend.delete_office_configuration_if_unchanged(
            configuration, hashlib.sha256(approved_payload).hexdigest()
        )

        self.assertFalse(deleted)
        self.assertEqual(preserved, configuration.resolve())
        self.assertEqual(configuration.read_bytes(), changed_payload)
        self.assertFalse(
            list(configuration.parent.glob(
                f".{configuration.name}.wine4office-preserved-*"
            ))
        )

    def test_office_configuration_atomic_delete_preserves_tombstone_beside_replacement(self):
        configuration = self.home / "replaced deployment.xml"
        approved_payload = b"<Configuration />"
        changed_payload = b"<Configuration><Add /></Configuration>"
        replacement_payload = b"<Configuration><Product /></Configuration>"
        configuration.write_bytes(changed_payload)
        real_rename = backend.os.rename

        def rename_and_replace(source, destination):
            real_rename(source, destination)
            Path(source).write_bytes(replacement_payload)

        with mock.patch.object(
            backend.os, "rename", side_effect=rename_and_replace
        ):
            deleted, preserved = backend.delete_office_configuration_if_unchanged(
                configuration, hashlib.sha256(approved_payload).hexdigest()
            )

        self.assertFalse(deleted)
        self.assertIsNotNone(preserved)
        self.assertNotEqual(preserved, configuration.resolve())
        self.assertEqual(configuration.read_bytes(), replacement_payload)
        self.assertEqual(preserved.read_bytes(), changed_payload)
        self.assertEqual(
            list(configuration.parent.glob(
                f".{configuration.name}.wine4office-preserved-*"
            )),
            [preserved],
        )

    def test_webview2_bootstrapper_download_is_atomic_and_microsoft_hosted(self):
        destination = self.home / "MicrosoftEdgeWebview2Setup.exe"
        payload = b"MZ" + (b"webview2" * 200)

        class Response(io.BytesIO):
            headers = {"Content-Length": str(len(payload))}

            def geturl(self):
                return ("https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/"
                        "files/test/MicrosoftEdgeWebview2Setup.exe")

        opener = mock.Mock()
        opener.open.return_value = Response(payload)
        with mock.patch.object(
            backend, "_microsoft_opener", return_value=opener
        ) as microsoft_opener:
            result = backend.download_webview2_bootstrapper(
                destination, lambda line: None
            )

        self.assertEqual(result, str(destination.resolve()))
        self.assertEqual(destination.read_bytes(), payload)
        microsoft_opener.assert_called_once_with(backend._WEBVIEW2_DOWNLOAD_HOSTS)
        self.assertEqual(
            opener.open.call_args.args[0].full_url,
            backend.WEBVIEW2_BOOTSTRAPPER_URL,
        )
        self.assertFalse(list(destination.parent.glob(
            ".MicrosoftEdgeWebview2Setup.exe.*.part"
        )))

    def test_teams_webview2_is_installed_only_when_missing(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        environment = {"WINEPREFIX": str(prefix)}

        def download(destination, output, cancel_event):
            Path(destination).write_bytes(b"MZ" + b"webview2" * 200)
            return str(destination)

        def install(command, env, output, **kwargs):
            runtime = (prefix / "drive_c/Program Files (x86)/Microsoft/"
                       "EdgeWebView/Application/151.0.0.0/msedgewebview2.exe")
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"MZ")

        with mock.patch.object(
            backend, "download_webview2_bootstrapper", side_effect=download
        ), mock.patch.object(
            backend, "_stream_command", side_effect=install
        ) as stream:
            self.assertTrue(backend.ensure_teams_webview2(
                prefix, self.wine, environment, lambda line: None
            ))
            self.assertFalse(backend.ensure_teams_webview2(
                prefix, self.wine, environment, lambda line: None
            ))

        stream.assert_called_once()
        self.assertEqual(stream.call_args.args[0][2:], ["/silent", "/install"])

    def test_teams_wow64_registration_is_refreshed_when_missing(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        wine_inf = self.wine.parent.parent / "share/wine/wine.inf"
        wine_inf.parent.mkdir(parents=True)
        wine_inf.write_text("[Version]\n")
        environment = {"WINEPREFIX": str(prefix)}
        missing = mock.Mock(returncode=1)
        present = mock.Mock(returncode=0)

        with mock.patch.object(
            backend.subprocess, "run", side_effect=[missing, present]
        ) as run, mock.patch.object(
            backend, "_windows_document_path", return_value=r"Z:\runner\wine.inf"
        ), mock.patch.object(backend, "_stream_command") as stream:
            refreshed = backend.ensure_teams_wow64_registration(
                prefix, self.wine, environment, lambda line: None
            )

        self.assertTrue(refreshed)
        self.assertEqual(run.call_count, 2)
        self.assertEqual(stream.call_args.args[0], [
            str(self.wine), r"C:\windows\syswow64\rundll32.exe",
            "setupapi,InstallHinfSection", "Wow64Install.ntx86", "128",
            r"Z:\runner\wine.inf",
        ])

    def test_teams_bootstrapper_download_is_atomic_and_microsoft_hosted(self):
        destination = self.home / "teamsbootstrapper.exe"
        destination.write_bytes(b"previous installer")
        payload = b"MZ" + (b"teams" * 300)

        class Response(io.BytesIO):
            headers = {"Content-Length": str(len(payload))}

            def geturl(self):
                return (
                    "https://statics.teams.cdn.office.net/production-teamsprovision/"
                    "lkg/teamsbootstrapper.exe"
                )

        opener = mock.Mock()
        opener.open.return_value = Response(payload)
        output = []
        with mock.patch.object(
            backend, "_microsoft_opener", return_value=opener
        ) as microsoft_opener:
            result = backend.download_teams_bootstrapper(destination, output.append)

        self.assertEqual(destination.read_bytes(), payload)
        self.assertIn(str(destination), result)
        self.assertIn(f"Saved Microsoft Teams installer to {destination}", output)
        microsoft_opener.assert_called_once_with(backend._TEAMS_DOWNLOAD_HOSTS)
        request = opener.open.call_args.args[0]
        self.assertEqual(request.full_url, backend.TEAMS_BOOTSTRAPPER_URL)
        self.assertFalse(list(destination.parent.glob(".teamsbootstrapper.exe.*.part")))

    def test_invalid_teams_bootstrapper_preserves_existing_destination(self):
        destination = self.home / "teamsbootstrapper.exe"
        previous = b"previous installer"
        destination.write_bytes(previous)
        payload = b"not-a-windows-executable" * 100

        class Response(io.BytesIO):
            headers = {"Content-Length": str(len(payload))}

            def geturl(self):
                return (
                    "https://statics.teams.cdn.office.net/production-teamsprovision/"
                    "lkg/teamsbootstrapper.exe"
                )

        opener = mock.Mock()
        opener.open.return_value = Response(payload)
        with mock.patch.object(
            backend, "_microsoft_opener", return_value=opener
        ), self.assertRaisesRegex(ValueError, "not a valid Windows executable"):
            backend.download_teams_bootstrapper(destination, lambda line: None)

        self.assertEqual(destination.read_bytes(), previous)
        self.assertFalse(list(destination.parent.glob(".teamsbootstrapper.exe.*.part")))

    def test_teams_msix_download_is_atomic_validated_and_reports_progress(self):
        destination = self.home / "MSTeams-x64.msix"
        payload_stream = io.BytesIO()
        with zipfile.ZipFile(payload_stream, "w") as package:
            package.writestr("AppxManifest.xml", "<Package />")
            package.writestr("AppxBlockMap.xml", "<BlockMap />")
            package.writestr("AppxSignature.p7x", b"signature")
            package.writestr("app/ms-teams.exe", b"MZ" + b"teams" * 300)
        payload = payload_stream.getvalue()

        class Response(io.BytesIO):
            headers = {"Content-Length": str(len(payload))}

            def geturl(self):
                return (
                    "https://statics.teams.cdn.office.net/production-windows-x64/"
                    "enterprise/webview2/lkg/MSTeams-x64.msix"
                )

        opener = mock.Mock()
        opener.open.return_value = Response(payload)
        progress = []
        output = []
        with mock.patch.object(
            backend, "_microsoft_opener", return_value=opener
        ) as microsoft_opener:
            result = backend.download_teams_msix(
                destination, output.append, progress_callback=progress.append
            )

        self.assertEqual(result, str(destination.resolve()))
        self.assertEqual(destination.read_bytes(), payload)
        self.assertEqual(progress[0], 0)
        self.assertEqual(progress[-1], 100)
        self.assertIn(f"Saved Microsoft Teams MSIX package to {destination}", output)
        microsoft_opener.assert_called_once_with(backend._TEAMS_DOWNLOAD_HOSTS)
        self.assertEqual(opener.open.call_args.args[0].full_url, backend.TEAMS_X64_MSIX_URL)
        self.assertFalse(list(destination.parent.glob(".MSTeams-x64.msix.*.part")))

    def test_teams_install_downloads_then_runs_bootstrapper_in_selected_prefix(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        environment = {"WINEPREFIX": str(prefix)}
        downloaded_paths = []

        def download(destination, output, cancel_event, progress_callback=None):
            destination = Path(destination)
            destination.write_bytes(b"MZ" + (b"teams" * 300))
            downloaded_paths.append(destination)
            return "downloaded"

        with mock.patch.object(
            backend, "download_teams_bootstrapper", side_effect=download
        ) as download_bootstrapper, mock.patch.object(
            backend, "download_teams_msix", side_effect=download
        ) as download_msix, mock.patch.object(
            backend, "ensure_teams_wow64_registration"
        ) as ensure_wow64, mock.patch.object(
            backend, "ensure_teams_webview2"
        ) as ensure_webview2, mock.patch.object(
            backend, "wine_environment", return_value=environment
        ) as wine_environment, mock.patch.object(
            backend, "_windows_document_path", return_value=r"C:\\MSTeams-x64.msix"
        ) as windows_path, mock.patch.object(
            backend, "_stream_command"
        ) as stream, mock.patch.object(
            backend, "find_office_app", return_value=self.home / "ms-teams.exe"
        ) as find_teams:
            progress = mock.Mock()
            result = backend.install_teams_with_bootstrapper(
                str(prefix), str(self.wine), lambda line: None,
                progress_callback=progress, use_x11=False
            )

        self.assertEqual(result, "Microsoft Teams installation completed successfully.")
        download_bootstrapper.assert_called_once_with(
            mock.ANY, mock.ANY, None
        )
        download_msix.assert_called_once()
        wine_environment.assert_called_once_with(prefix.resolve(), self.wine.resolve(), False)
        ensure_wow64.assert_called_once_with(
            prefix.resolve(), self.wine.resolve(), environment, mock.ANY,
            cancel_event=None, process_callback=None,
        )
        ensure_webview2.assert_called_once_with(
            prefix.resolve(), self.wine.resolve(), environment, mock.ANY,
            cancel_event=None, process_callback=None,
        )
        self.assertEqual(len(downloaded_paths), 2)
        bootstrapper = downloaded_paths[0]
        self.assertEqual(
            stream.call_args.args[:3],
            ([str(self.wine.resolve()), str(bootstrapper), "-p", "-o",
              r"C:\\MSTeams-x64.msix"], environment, mock.ANY),
        )
        self.assertEqual(stream.call_args.kwargs["cwd"], bootstrapper.parent)
        self.assertFalse(bootstrapper.exists())
        windows_path.assert_called_once_with(
            mock.ANY, self.wine.resolve(), environment
        )
        find_teams.assert_called_once_with(str(prefix.resolve()), "teams")
        self.assertEqual(progress.call_args.args, ("Microsoft Teams installed", 100))

    def test_teams_install_rejects_false_success_without_installed_app(self):
        prefix = self._make_prefix(self.home / ".wine4office")
        with mock.patch.object(
            backend, "download_teams_bootstrapper"
        ), mock.patch.object(
            backend, "download_teams_msix"
        ), mock.patch.object(
            backend, "ensure_teams_wow64_registration"
        ), mock.patch.object(
            backend, "ensure_teams_webview2"
        ), mock.patch.object(
            backend, "_windows_document_path", return_value=r"C:\\MSTeams-x64.msix"
        ), mock.patch.object(
            backend, "_stream_command"
        ), mock.patch.object(
            backend, "find_office_app", return_value=None
        ), self.assertRaisesRegex(RuntimeError, "exited without installing Teams"):
            backend.install_teams_with_bootstrapper(
                str(prefix), str(self.wine), lambda line: None
            )

    def test_odt_install_extracts_before_configure_and_keeps_configuration(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        configuration = self.home / "office deployment.xml"
        original_payload = b"<Configuration><Add /></Configuration>"
        changed_payload = b"<Configuration><Display /></Configuration>"
        configuration.write_bytes(original_payload)
        odt = self.root / "officedeploymenttool.exe"
        odt.write_bytes(b"MZ")
        setup = self.root / "setup.exe"
        environment = {"WINEPREFIX": str(prefix)}
        converted_paths = []
        snapshot_payloads = []
        odt_url = "https://download.microsoft.com/officedeploymenttool_12345-12345.exe"
        installer_started = mock.Mock()

        def resolve_latest(cancel_event):
            configuration.write_bytes(changed_payload)
            return odt_url

        def convert(document, wine, env):
            path = Path(document)
            converted_paths.append(path)
            if path.name == "configuration.xml":
                snapshot_payloads.append(path.read_bytes())
                return r"Z:\private configuration.xml"
            return r"Z:\odt extraction"

        with mock.patch.object(
            backend, "_resolve_latest_odt_url", side_effect=resolve_latest
        ) as resolve, mock.patch.object(
            backend, "_download_odt", return_value=odt
        ) as download, mock.patch.object(
            backend, "wine_environment", return_value=environment
        ), mock.patch.object(
            backend, "_windows_document_path", side_effect=convert
        ), mock.patch.object(
            backend, "_require_extracted_setup", return_value=setup
        ) as require_setup, mock.patch.object(
            backend, "_stream_command"
        ) as stream:
            result = backend.install_office_with_odt(
                str(prefix),
                str(self.wine),
                configuration,
                lambda line: None,
                configuration_payload=original_payload,
                installer_started_callback=installer_started,
            )

        self.assertEqual(result, "Office installation completed successfully.")
        resolve.assert_called_once_with(None)
        download.assert_called_once_with(odt_url, mock.ANY, None)
        self.assertEqual(len(stream.call_args_list), 2)
        extraction_directory = stream.call_args_list[0].kwargs["cwd"]
        self.assertEqual(
            stream.call_args_list[0].args[:3],
            (
                [
                    str(self.wine),
                    str(odt),
                    "/quiet",
                    r"/extract:Z:\odt extraction",
                ],
                environment,
                mock.ANY,
            ),
        )
        require_setup.assert_called_once_with(extraction_directory)
        self.assertEqual(
            stream.call_args_list[1].args[:3],
            (
                [
                    str(self.wine),
                    str(setup),
                    "/configure",
                    r"Z:\private configuration.xml",
                ],
                environment,
                mock.ANY,
            ),
        )
        self.assertEqual(stream.call_args_list[1].kwargs["cwd"], extraction_directory)
        setup_process_callback = stream.call_args_list[1].kwargs["process_callback"]
        setup_process_callback(mock.Mock())
        installer_started.assert_called_once_with()
        setup_process_callback(None)
        installer_started.assert_called_once_with()
        self.assertEqual(converted_paths[0], extraction_directory)
        self.assertEqual(converted_paths[1].name, "configuration.xml")
        self.assertEqual(converted_paths[1].parent, extraction_directory.parent)
        self.assertNotEqual(converted_paths[1], configuration.resolve())
        self.assertEqual(snapshot_payloads, [original_payload])
        self.assertTrue(configuration.is_file())
        self.assertEqual(configuration.read_bytes(), changed_payload)
        self.assertFalse(odt.exists())

    def _preload_binding(self):
        prefix = self._make_prefix(self.home / "preload prefix")
        return backend._preload_snapshot(str(prefix), str(self.wine), True)

    def test_preload_xdg_paths_and_atomic_modes(self):
        runtime = self.root / "runtime"
        with mock.patch.dict(os.environ, {"XDG_RUNTIME_DIR": str(runtime)}):
            binding = self._preload_binding()
            backend._preload_json_write(backend.preload_binding_path(), binding, 0o600)
            backend._preload_atomic_write(
                backend.preload_unit_path(), "owned unit\n", 0o644
            )

            self.assertEqual(
                backend.preload_binding_path(),
                self.home / ".config/wine4office/preload-service.json",
            )
            self.assertEqual(
                backend.preload_unit_path(),
                self.home / ".config/systemd/user/wine4office-preload.service",
            )
            self.assertEqual(
                backend.preload_runtime_status_path(),
                runtime / "wine4office/preload-service.json",
            )
            self.assertEqual(
                stat.S_IMODE(backend.preload_binding_path().stat().st_mode), 0o600
            )
            self.assertEqual(
                stat.S_IMODE(backend.preload_unit_path().stat().st_mode), 0o644
            )

    def test_legacy_rpc_binding_is_normalized_to_clicktorun_only(self):
        legacy = {
            **self._preload_binding(),
            "components": ["ClickToRunSvc", "RpcSs"],
        }
        normalized = backend._validate_preload_binding(legacy)
        self.assertEqual(normalized["components"], ["ClickToRunSvc"])

    def test_preload_unit_is_exact_safe_and_enable_does_not_start(self):
        binding = self._preload_binding()
        commands = []

        def systemctl(command, check=True):
            commands.append(list(command))
            return mock.Mock(returncode=0, stdout="", stderr="")

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", return_value=(False, "disabled")
        ), mock.patch.object(
            backend, "_systemctl_user", side_effect=systemctl
        ), mock.patch.object(
            backend, "preload_service_status", return_value={"state": "inactive"}
        ):
            backend.install_preload_service(
                binding["prefix"], binding["wine"], binding["use_x11"]
            )

        exec_start = " ".join(
            backend._systemd_quote(value) for value in (
                backend._preload_worker_executable(),
                backend.preload_binding_path(),
                backend.preload_runtime_status_path(),
            )
        )
        expected = (
            "# Managed by Wine4OfficeManager: preload-service-v1\n"
            "[Unit]\n"
            "Description=Wine4Office background services\n\n"
            "[Service]\n"
            "Type=simple\n"
            f"ExecStart={exec_start}\n"
            "Restart=on-failure\n"
            "KillMode=process\n"
            "TimeoutStopSec=20\n\n"
            "[Install]\n"
            "WantedBy=default.target\n"
        )
        self.assertEqual(backend.preload_unit_path().read_text(), expected)
        self.assertIn("Type=simple\n", expected)
        self.assertIn("Restart=on-failure\n", expected)
        self.assertIn("KillMode=process\n", expected)
        self.assertIn("TimeoutStopSec=20\n", expected)
        self.assertIn("WantedBy=default.target\n", expected)
        self.assertNotIn("/bin/sh", expected)
        self.assertNotIn("wineserver", expected)
        self.assertNotIn("wine4office_manager.py", expected)
        self.assertIn("wine4office_preload.py", expected)
        self.assertEqual(commands, [["daemon-reload"], ["enable", backend.PRELOAD_UNIT]])

    def test_preload_unit_rejects_newline_in_argv(self):
        with self.assertRaisesRegex(ValueError, "unsafe control"):
            backend._systemd_quote("/tmp/manager\nExecStart=/bin/false")

    def test_frozen_manager_installs_bundled_lightweight_preload_worker(self):
        bundle = self.home / "bundle"
        bundle.mkdir()
        source = bundle / backend.PRELOAD_WORKER_BINARY
        source.write_bytes(b"lightweight-worker")
        source.chmod(0o755)
        with mock.patch.object(
            backend, "installed_root", return_value=None
        ), mock.patch.object(
            backend.sys, "frozen", True, create=True
        ), mock.patch.object(
            backend.sys, "_MEIPASS", str(bundle), create=True
        ):
            worker = backend._preload_worker_executable()

        self.assertEqual(worker, backend.preload_worker_path())
        self.assertEqual(worker.read_bytes(), b"lightweight-worker")
        self.assertTrue(os.access(worker, os.X_OK))

    def test_systemctl_user_timeout_is_bounded_and_uses_no_shell(self):
        with mock.patch.object(
            backend.shutil, "which", return_value="/usr/bin/systemctl"
        ), mock.patch.object(
            backend.subprocess, "run",
            side_effect=subprocess.TimeoutExpired(["systemctl"], 8),
        ) as run:
            with self.assertRaisesRegex(RuntimeError, "timed out"):
                backend._systemctl_user(["show-environment"])
        self.assertEqual(
            run.call_args.args[0],
            ["/usr/bin/systemctl", "--user", "show-environment"],
        )
        self.assertNotIn("shell", run.call_args.kwargs)
        self.assertEqual(run.call_args.kwargs["timeout"], 8)

    def test_preload_memory_reads_only_systemd_memory_property(self):
        completed = mock.Mock(returncode=0, stdout="637788160\n", stderr="")
        with mock.patch.object(
            backend, "_systemctl_user", return_value=completed
        ) as systemctl:
            memory = backend.preload_service_memory_bytes()

        self.assertEqual(memory, 637788160)
        systemctl.assert_called_once_with(
            ["show", backend.PRELOAD_UNIT, "--property=MemoryCurrent", "--value"],
            check=False,
        )

    def test_enable_failure_rolls_back_binding_and_unit(self):
        binding = self._preload_binding()

        def fail_enable(command, check=True):
            if command[0] == "enable":
                raise RuntimeError("enable failed")
            return mock.Mock(returncode=0, stdout="", stderr="")

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", return_value=(False, "disabled")
        ), mock.patch.object(
            backend, "_systemctl_user", side_effect=fail_enable
        ):
            with self.assertRaisesRegex(RuntimeError, "enable failed"):
                backend.install_preload_service(
                    binding["prefix"], binding["wine"], binding["use_x11"]
                )
        self.assertFalse(backend.preload_binding_path().exists())
        self.assertFalse(backend.preload_unit_path().exists())

    def test_preload_status_reports_exact_binding_mismatch(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property",
            side_effect=[(True, "enabled"), (False, "inactive")],
        ):
            status = backend.preload_service_status(
                str(self.home / "different"), binding["wine"], True
            )
        self.assertEqual(set(status), {
            "supported", "reason", "installed", "enabled", "active", "state",
            "binding", "selected_matches", "components", "detail",
        })
        self.assertEqual(status["state"], "mismatch")
        self.assertFalse(status["selected_matches"])

    def test_preload_status_ignores_foreground_display_mode_difference(self):
        binding = self._preload_binding()
        binding["use_x11"] = False
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property",
            side_effect=[(True, "enabled"), (False, "inactive")],
        ):
            status = backend.preload_service_status(
                binding["prefix"], binding["wine"], True
            )

        self.assertTrue(status["selected_matches"])
        self.assertEqual(status["state"], "inactive")

    def test_disable_and_start_actions_are_separate(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        commands = []
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", return_value=(True, "enabled")
        ), mock.patch.object(
            backend, "_systemctl_user",
            side_effect=lambda command, check=True: (
                commands.append(list(command))
                or mock.Mock(returncode=0, stdout="", stderr="")
            ),
        ), mock.patch.object(
            backend, "preload_service_status", return_value={"state": "inactive"}
        ):
            backend.manage_preload_service(
                "disable", binding["prefix"], binding["wine"], True
            )
            backend.manage_preload_service(
                "start", binding["prefix"], binding["wine"], True
            )
        self.assertEqual(
            commands,
            [["disable", backend.PRELOAD_UNIT], ["start", backend.PRELOAD_UNIT]],
        )

    def test_stop_uses_bound_environment_during_runner_mismatch(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        different_wine = str(self.home / "updated-runner/bin/wine")
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "preload_office_processes", return_value=[]
        ) as office, mock.patch.object(
            backend, "_systemctl_property", return_value=(False, "inactive")
        ), mock.patch.object(
            backend, "_systemctl_user"
        ) as systemctl, mock.patch.object(
            backend, "preload_service_status", return_value={"state": "mismatch"}
        ):
            backend.manage_preload_service(
                "stop", binding["prefix"], different_wine, True
            )

        office.assert_called_once_with(
            binding["prefix"], binding["wine"], binding["use_x11"]
        )
        systemctl.assert_called_once_with(
            ["stop", "--no-block", backend.PRELOAD_UNIT]
        )

    def test_stop_wine_tool_restarts_matching_active_background_service(self):
        prefix = self._make_prefix(self.home / "stop-and-restart")
        with mock.patch.object(
            backend, "_preload_active_for_environment", return_value=True
        ) as active, mock.patch.object(
            backend, "_stop_preload_unit_and_wait"
        ) as stop_service, mock.patch.object(
            backend, "stop_wine"
        ) as stop, mock.patch.object(
            backend, "_systemctl_user"
        ) as systemctl:
            result = backend.launch_tool(str(prefix), str(self.wine), "stop", False)

        self.assertIsNone(result)
        active.assert_called_once_with(str(prefix), str(self.wine), False)
        stop_service.assert_called_once_with()
        stop.assert_called_once_with(str(prefix), str(self.wine), False)
        systemctl.assert_called_once_with(["start", backend.PRELOAD_UNIT])

    def test_stop_wine_tool_does_not_start_an_inactive_background_service(self):
        prefix = self._make_prefix(self.home / "stop-without-service")
        with mock.patch.object(
            backend, "_preload_active_for_environment", return_value=False
        ), mock.patch.object(
            backend, "stop_wine"
        ), mock.patch.object(
            backend, "_systemctl_user"
        ) as systemctl:
            backend.launch_tool(str(prefix), str(self.wine), "stop")
        systemctl.assert_not_called()

    def test_preload_status_merges_fresh_and_stale_heartbeat(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        components = {
            name: {"state": "running", "owned": True, "detail": ""}
            for name in backend.PRELOAD_COMPONENTS
        }

        def property_state(command):
            return (True, "enabled" if command == "is-enabled" else "active")

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", side_effect=property_state
        ), mock.patch.object(backend.time, "time", return_value=100.0):
            backend._write_preload_heartbeat(
                backend.preload_runtime_status_path(), components, "running"
            )
            fresh = backend.preload_service_status(
                binding["prefix"], binding["wine"], True
            )
        self.assertEqual(fresh["state"], "active")
        self.assertEqual(fresh["components"], components)

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", side_effect=property_state
        ), mock.patch.object(backend.time, "time", return_value=200.0):
            stale = backend.preload_service_status(
                binding["prefix"], binding["wine"], True
            )
        self.assertEqual(stale["state"], "degraded")
        self.assertIn("stale", stale["detail"])

    def test_disable_is_idempotent_when_already_disabled(self):
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", return_value=(False, "disabled")
        ), mock.patch.object(
            backend, "_systemctl_user"
        ) as systemctl, mock.patch.object(
            backend, "preload_service_status", return_value={"state": "uninstalled"}
        ):
            result = backend.manage_preload_service("disable")
        self.assertEqual(result["state"], "uninstalled")
        systemctl.assert_not_called()

    def test_office_detection_parses_csv_and_ignores_infrastructure(self):
        binding = self._preload_binding()
        output = (
            '"WINWORD.EXE","101","Console","1","12 K"\n'
            '"services.exe","102","Services","0","8 K"\n'
            '"RpcSs.exe","103","Services","0","8 K"\n'
        )
        completed = mock.Mock(returncode=0, stdout=output, stderr="")
        with mock.patch.object(
            backend.subprocess, "run", return_value=completed
        ) as run:
            found = backend.preload_office_processes(
                binding["prefix"], binding["wine"], True
            )
        self.assertEqual(found, ["WINWORD.EXE"])
        self.assertEqual(
            run.call_args.args[0],
            [binding["wine"], "tasklist.exe", "/FO", "CSV", "/NH"],
        )
        self.assertNotIn("shell", run.call_args.kwargs)

    def test_office_detection_does_not_treat_teams_as_preloaded_office(self):
        binding = self._preload_binding()
        completed = mock.Mock(
            returncode=0,
            stdout='"ms-teams.exe","101","Console","1","12 K"\n',
            stderr="",
        )
        with mock.patch.object(backend.subprocess, "run", return_value=completed):
            found = backend.preload_office_processes(
                binding["prefix"], binding["wine"], True
            )

        self.assertEqual(found, [])

    def test_stop_refuses_active_or_unknown_office_without_systemctl_stop(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "preload_office_processes", return_value=["EXCEL.EXE"]
        ), mock.patch.object(backend, "_systemctl_user") as systemctl:
            with self.assertRaisesRegex(RuntimeError, "Office is active"):
                backend.manage_preload_service(
                    "stop", binding["prefix"], binding["wine"], True
                )
        systemctl.assert_not_called()

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "preload_office_processes",
            side_effect=RuntimeError("tasklist timeout"),
        ), mock.patch.object(backend, "_systemctl_user") as systemctl:
            with self.assertRaisesRegex(RuntimeError, "tasklist timeout"):
                backend.manage_preload_service(
                    "stop", binding["prefix"], binding["wine"], True
                )
        systemctl.assert_not_called()

    def test_clicktorun_process_detection_is_prefix_scoped(self):
        binding = self._preload_binding()
        proc_root = self.root / "proc"
        process = proc_root / "123"
        process.mkdir(parents=True)
        (process / "cmdline").write_bytes(
            b"C:\\Program Files\\Common Files\\Microsoft Shared\\ClickToRun\\"
            b"OfficeClickToRun.exe\0/service\0"
        )
        (process / "environ").write_bytes(
            os.fsencode(f"WINEPREFIX={binding['prefix']}") + b"\0"
        )

        self.assertIs(
            backend._preload_component_process_running(
                binding, "ClickToRunSvc", proc_root
            ),
            True,
        )
        (process / "environ").write_bytes(b"WINEPREFIX=/other/prefix\0")
        self.assertIs(
            backend._preload_component_process_running(
                binding, "ClickToRunSvc", proc_root
            ),
            False,
        )
        self.assertIsNone(
            backend._preload_component_process_running(binding, "unknown", proc_root)
        )

    def test_appv_helper_waits_for_ready_and_uses_quiet_wine(self):
        binding = self._preload_binding()
        helper = (
            Path(binding["wine"]).resolve().parent.parent
            / "lib/wine/x86_64-windows"
            / backend.PRELOAD_APPV_HELPER
        )
        helper.parent.mkdir(parents=True)
        helper.write_bytes(b"helper")
        process = mock.Mock()
        process.poll.return_value = None
        process.stdout = io.StringIO("READY appv_ms=3512\n")
        process.stdin = mock.Mock()

        with mock.patch.object(
            backend.subprocess, "Popen", return_value=process
        ) as popen, mock.patch.object(
            backend.select, "select", return_value=([process.stdout], [], [])
        ):
            started, detail = backend._start_preload_appv(binding)

        self.assertIs(started, process)
        self.assertEqual(detail, "READY appv_ms=3512")
        self.assertEqual(
            popen.call_args.args[0],
            [binding["wine"], str(helper)],
        )
        self.assertEqual(popen.call_args.kwargs["env"]["WINEDEBUG"], "-all")
        self.assertTrue(process.stdout.closed)

    def test_appv_helper_stops_by_closing_its_input(self):
        process = mock.Mock()
        process.poll.return_value = None
        process.stdin.closed = False
        process.stdout.closed = False

        stopped, detail = backend._stop_preload_appv(process)

        self.assertTrue(stopped)
        self.assertEqual(detail, "stopped")
        process.stdin.write.assert_called_once_with("\n")
        process.stdin.close.assert_called_once()
        process.wait.assert_called_once_with(
            timeout=backend._PRELOAD_APPV_STOP_TIMEOUT
        )
        process.terminate.assert_not_called()

    def test_worker_starts_and_stops_owned_clicktorun(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        status_path = backend.preload_runtime_status_path()
        states = {"ClickToRunSvc": "stopped"}
        actions = []
        handlers = {}
        appv_process = mock.Mock()
        appv_process.poll.return_value = None

        def component_state(_binding, component):
            return states[component], states[component]

        def component_action(_binding, action, component):
            actions.append((action, component))
            states[component] = "running" if action == "start" else "stopped"
            return True, action

        def install_signal(signum, handler):
            old = handlers.get(signum, signal.SIG_DFL)
            handlers[signum] = handler
            return old

        def stop_sleep(_seconds):
            handlers[signal.SIGTERM](signal.SIGTERM, None)

        with mock.patch.object(
            backend, "_preload_component_process_running", return_value=False
        ), mock.patch.object(
            backend, "_preload_component_state", side_effect=component_state
        ), mock.patch.object(
            backend, "_preload_component_action", side_effect=component_action
        ), mock.patch.object(
            backend, "preload_office_processes", return_value=[]
        ) as office, mock.patch.object(
            backend, "_start_preload_appv",
            return_value=(appv_process, "READY appv_ms=3512"),
        ) as appv_start, mock.patch.object(
            backend, "_stop_preload_appv", return_value=(True, "stopped")
        ) as appv_stop, mock.patch.object(
            backend.signal, "signal", side_effect=install_signal
        ), mock.patch.object(
            backend.time, "sleep", side_effect=stop_sleep
        ), mock.patch.object(
            backend.os, "kill"
        ) as broad_kill:
            result = backend.run_preload_worker(
                backend.preload_binding_path(), status_path
            )

        self.assertEqual(result, 0)
        self.assertEqual(
            actions,
            [("start", "ClickToRunSvc"), ("stop", "ClickToRunSvc")],
        )
        office.assert_called_once()
        appv_start.assert_called_once_with(binding)
        appv_stop.assert_called_once_with(appv_process)
        broad_kill.assert_not_called()
        heartbeat = __import__("json").loads(status_path.read_text())
        self.assertEqual(heartbeat["state"], "stopped")
        self.assertTrue(heartbeat["components"]["ClickToRunSvc"]["owned"])
        self.assertEqual(heartbeat["components"]["AppV"]["state"], "stopped")

    def test_worker_owns_clicktorun_auto_started_by_first_query(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        handlers = {}

        def install_signal(signum, handler):
            old = handlers.get(signum, signal.SIG_DFL)
            handlers[signum] = handler
            return old

        def stop_sleep(_seconds):
            handlers[signal.SIGTERM](signal.SIGTERM, None)

        with mock.patch.object(
            backend, "_preload_component_process_running", return_value=False
        ), mock.patch.object(
            backend, "_preload_component_state", return_value=("running", "running")
        ), mock.patch.object(
            backend, "_preload_component_action", return_value=(True, "stopped")
        ) as action, mock.patch.object(
            backend, "preload_office_processes", return_value=[]
        ), mock.patch.object(
            backend.signal, "signal", side_effect=install_signal
        ), mock.patch.object(backend.time, "sleep", side_effect=stop_sleep):
            result = backend.run_preload_worker(
                backend.preload_binding_path(), backend.preload_runtime_status_path()
            )

        self.assertEqual(result, 0)
        self.assertEqual(
            [call.args[1:] for call in action.call_args_list],
            [("stop", "ClickToRunSvc")],
        )
        heartbeat = __import__("json").loads(
            backend.preload_runtime_status_path().read_text()
        )
        self.assertTrue(heartbeat["components"]["ClickToRunSvc"]["owned"])

    def test_worker_preserves_preexisting_clicktorun(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        handlers = {}

        def install_signal(signum, handler):
            old = handlers.get(signum, signal.SIG_DFL)
            handlers[signum] = handler
            return old

        def stop_sleep(_seconds):
            handlers[signal.SIGTERM](signal.SIGTERM, None)

        with mock.patch.object(
            backend, "_preload_component_process_running", return_value=True
        ), mock.patch.object(
            backend, "_preload_component_state", return_value=("running", "running")
        ), mock.patch.object(
            backend, "_preload_component_action", return_value=(True, "stopped")
        ) as action, mock.patch.object(
            backend, "preload_office_processes", return_value=[]
        ), mock.patch.object(
            backend.signal, "signal", side_effect=install_signal
        ), mock.patch.object(backend.time, "sleep", side_effect=stop_sleep):
            result = backend.run_preload_worker(
                backend.preload_binding_path(), backend.preload_runtime_status_path()
            )

        self.assertEqual(result, 0)
        self.assertEqual(
            [call.args[1:] for call in action.call_args_list],
            [("stop", "ClickToRunSvc")],
        )
        heartbeat = __import__("json").loads(
            backend.preload_runtime_status_path().read_text()
        )
        self.assertFalse(heartbeat["components"]["ClickToRunSvc"]["owned"])

    def test_worker_refuses_signal_cleanup_when_activity_check_fails(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        handlers = {}

        def install_signal(signum, handler):
            old = handlers.get(signum, signal.SIG_DFL)
            handlers[signum] = handler
            return old

        def stop_sleep(_seconds):
            handlers[signal.SIGTERM](signal.SIGTERM, None)

        with mock.patch.object(
            backend, "_preload_component_process_running", return_value=False
        ), mock.patch.object(
            backend, "_preload_component_state", return_value=("running", "running")
        ), mock.patch.object(
            backend, "_preload_component_action"
        ) as action, mock.patch.object(
            backend, "preload_office_processes",
            side_effect=RuntimeError("unknown"),
        ), mock.patch.object(
            backend.signal, "signal", side_effect=install_signal
        ), mock.patch.object(backend.time, "sleep", side_effect=stop_sleep):
            result = backend.run_preload_worker(
                backend.preload_binding_path(), backend.preload_runtime_status_path()
            )
        self.assertEqual(result, 1)
        action.assert_not_called()
        heartbeat = __import__("json").loads(
            backend.preload_runtime_status_path().read_text()
        )
        self.assertEqual(heartbeat["state"], "stop-refused")

    def test_worker_reports_degraded_component_without_claiming_ownership(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        handlers = {}
        heartbeat_states = []

        def install_signal(signum, handler):
            old = handlers.get(signum, signal.SIG_DFL)
            handlers[signum] = handler
            return old

        def stop_sleep(_seconds):
            handlers[signal.SIGTERM](signal.SIGTERM, None)

        real_heartbeat = backend._write_preload_heartbeat

        def capture_heartbeat(path, components, state, detail=""):
            heartbeat_states.append(state)
            real_heartbeat(path, components, state, detail)

        with mock.patch.object(
            backend, "_preload_component_process_running", return_value=False
        ), mock.patch.object(
            backend, "_preload_component_state",
            return_value=("stopped", "stopped"),
        ), mock.patch.object(
            backend, "_preload_component_action",
            side_effect=[(False, "start failed"), (True, "stopped")],
        ) as action, mock.patch.object(
            backend, "preload_office_processes", return_value=[]
        ), mock.patch.object(
            backend, "_write_preload_heartbeat", side_effect=capture_heartbeat
        ), mock.patch.object(
            backend.signal, "signal", side_effect=install_signal
        ), mock.patch.object(backend.time, "sleep", side_effect=stop_sleep):
            result = backend.run_preload_worker(
                backend.preload_binding_path(), backend.preload_runtime_status_path()
            )
        self.assertEqual(result, 0)
        self.assertIn("degraded", heartbeat_states)
        self.assertEqual(action.call_args_list[0].args[1:], ("start", "ClickToRunSvc"))
        self.assertFalse(
            __import__("json").loads(
                backend.preload_runtime_status_path().read_text()
            )["components"]["ClickToRunSvc"]["owned"]
        )

    def test_uninstall_stops_then_disables_owned_service_before_removal(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(),
            backend._PRELOAD_UNIT_MARKER + "\n[Service]\n",
            0o644,
        )
        commands = []
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property",
            side_effect=[
                (True, "active"), (True, "enabled"), (False, "inactive")
            ],
        ), mock.patch.object(
            backend, "preload_office_processes", return_value=[]
        ), mock.patch.object(
            backend, "preload_service_status", return_value={"state": "inactive"}
        ), mock.patch.object(
            backend, "_systemctl_user",
            side_effect=lambda command, check=True: (
                commands.append(list(command))
                or mock.Mock(returncode=0, stdout="", stderr="")
            ),
        ):
            backend.uninstall_preload_service()
        self.assertEqual(commands, [
            ["stop", "--no-block", backend.PRELOAD_UNIT],
            ["disable", backend.PRELOAD_UNIT],
            ["daemon-reload"],
        ])
        self.assertFalse(backend.preload_unit_path().exists())
        self.assertFalse(backend.preload_binding_path().exists())

    def test_uninstall_never_deletes_or_controls_foreign_unit(self):
        backend.preload_unit_path().parent.mkdir(parents=True)
        backend.preload_unit_path().write_text("[Service]\nExecStart=/foreign\n")
        with mock.patch.object(backend, "_systemctl_user") as systemctl:
            backend.uninstall_preload_service()
        systemctl.assert_not_called()
        self.assertTrue(backend.preload_unit_path().exists())

    def test_inactive_disabled_binding_can_be_explicitly_replaced(self):
        old = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), old)
        backend._preload_atomic_write(
            backend.preload_unit_path(), backend._PRELOAD_UNIT_MARKER + "\n", 0o644
        )
        new_prefix = self._make_prefix(self.home / "new preload prefix")
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", return_value=(False, "inactive")
        ), mock.patch.object(
            backend, "_systemctl_user",
            return_value=mock.Mock(returncode=0, stdout="", stderr=""),
        ), mock.patch.object(
            backend, "preload_service_status", return_value={"state": "inactive"}
        ):
            backend.install_preload_service(str(new_prefix), str(self.wine), True)
        self.assertEqual(
            backend._read_preload_binding()["prefix"], str(new_prefix.resolve())
        )

    def test_enable_installs_when_only_a_stale_binding_file_exists(self):
        old = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), old)
        new_prefix = self._make_prefix(self.home / "selected prefix")
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property", return_value=(False, "inactive")
        ), mock.patch.object(
            backend, "_systemctl_user",
            return_value=mock.Mock(returncode=0, stdout="", stderr=""),
        ), mock.patch.object(
            backend, "preload_service_status", return_value={"state": "enabled"}
        ):
            backend.install_preload_service(str(new_prefix), str(self.wine), True)

        self.assertTrue(backend.preload_unit_path().is_file())
        self.assertEqual(
            backend._read_preload_binding()["prefix"], str(new_prefix.resolve())
        )

    def test_runner_update_rebinds_and_restarts_owned_background_service(self):
        old = self._preload_binding()
        old["use_x11"] = False
        backend._preload_json_write(backend.preload_binding_path(), old)
        backend._preload_atomic_write(
            backend.preload_unit_path(),
            backend._preload_unit_text(
                backend.preload_binding_path(), backend.preload_runtime_status_path()
            ),
            0o644,
        )
        new_runner = self.home / "new runner"
        new_wine = new_runner / "bin/wine"
        new_wine.parent.mkdir(parents=True)
        new_wine.write_text("#!/bin/sh\n")
        new_wine.chmod(0o755)
        commands = []

        def systemctl(command, check=True):
            commands.append(list(command))
            return mock.Mock(returncode=0, stdout="", stderr="")

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property",
            side_effect=[
                (True, "enabled"), (True, "active"), (False, "inactive")
            ],
        ), mock.patch.object(
            backend, "_systemctl_user", side_effect=systemctl
        ):
            state = backend.prepare_preload_runner_update(old["prefix"], True)
            self.assertIsNotNone(state)
            backend.finish_preload_runner_update(state, str(new_wine))

        self.assertEqual(commands, [
            ["stop", "--no-block", backend.PRELOAD_UNIT],
            ["daemon-reload"],
            ["start", backend.PRELOAD_UNIT],
        ])
        updated = backend._read_preload_binding()
        self.assertEqual(updated["prefix"], old["prefix"])
        self.assertEqual(updated["wine"], str(new_wine.resolve()))
        self.assertFalse(updated["use_x11"])

    def test_manager_update_replaces_legacy_preload_executor(self):
        binding = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), binding)
        backend._preload_atomic_write(
            backend.preload_unit_path(),
            backend._PRELOAD_UNIT_MARKER
            + "\n[Service]\nExecStart=/legacy/Wine4OfficeManager --preload-worker\n",
            0o644,
        )
        commands = []

        def systemctl(command, check=True):
            commands.append(list(command))
            return mock.Mock(returncode=0, stdout="", stderr="")

        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property",
            side_effect=[
                (True, "enabled"), (True, "active"), (False, "inactive")
            ],
        ), mock.patch.object(
            backend, "_systemctl_user", side_effect=systemctl
        ):
            self.assertTrue(backend.refresh_preload_worker_service())

        unit = backend.preload_unit_path().read_text()
        self.assertNotIn("ExecStart=/legacy/Wine4OfficeManager", unit)
        self.assertNotIn("--preload-worker", unit)
        self.assertIn("wine4office_preload.py", unit)
        self.assertEqual(commands, [
            ["stop", "--no-block", backend.PRELOAD_UNIT],
            ["daemon-reload"],
            ["start", backend.PRELOAD_UNIT],
        ])

    def test_enabled_mismatched_binding_cannot_be_replaced(self):
        old = self._preload_binding()
        backend._preload_json_write(backend.preload_binding_path(), old)
        new_prefix = self._make_prefix(self.home / "new preload prefix")
        with mock.patch.object(
            backend, "_systemd_user_capability", return_value=(True, "")
        ), mock.patch.object(
            backend, "_systemctl_property",
            side_effect=[(True, "enabled"), (False, "inactive")],
        ):
            with self.assertRaisesRegex(RuntimeError, "Disable it at login"):
                backend.install_preload_service(
                    str(new_prefix), str(self.wine), True
                )
        self.assertEqual(backend._read_preload_binding(), old)

if __name__ == "__main__":
    unittest.main()
