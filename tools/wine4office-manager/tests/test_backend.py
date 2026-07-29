#!/usr/bin/env python3

import hashlib
import os
import subprocess
import stat
import sys
import tempfile
import unittest
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


    def test_rejects_dangerous_prefixes(self):
        for value in ("/", "/usr", "/var", str(self.home), ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                backend.validate_prefix(value)

    def test_create_environment(self):
        prefix = self.home / ".wine4office"
        message = backend.create_environment(str(prefix), str(self.wine), False, lambda line: None)
        self.assertTrue(backend.has_wine_prefix_layout(prefix))
        self.assertIn(str(prefix), message)

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
        with mock.patch.object(backend.subprocess, "Popen", return_value=process) as popen:
            pid = backend.launch_executable(str(prefix), str(self.wine), str(installer),
                                            '/configure "/home/user/office.xml"')
        self.assertEqual(pid, 4321)
        command = popen.call_args.args[0]
        self.assertEqual(command, [str(self.wine), str(installer), "/configure", "/home/user/office.xml"])
        self.assertEqual(popen.call_args.kwargs["env"]["WINEPREFIX"], str(prefix))

    def test_executable_launcher_forwards_valid_working_directory(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        installer = self.home / "Downloads/Office Setup.exe"
        installer.parent.mkdir()
        installer.write_bytes(b"MZ")
        working_directory = self.home / "installer files"
        working_directory.mkdir()

        with mock.patch.object(
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

        with mock.patch.object(
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

        with mock.patch.object(backend.subprocess, "Popen") as popen, \
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

    def test_existing_prefix_office_launches_enable_smooth_monochrome_text_last(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        executable = prefix / "drive_c/Office/APP.EXE"
        process = mock.Mock(pid=4321)
        events = []
        registry_command = [
            str(self.wine), "reg", "add", r"HKCU\Software\Wine\Fonts",
            "/v", "SmoothMonochromeText", "/t", "REG_SZ", "/d", "Y", "/f",
        ]

        with mock.patch.object(
            backend, "find_office_app", return_value=executable,
        ), mock.patch.object(
            backend, "prepare_office_building_blocks",
            side_effect=lambda prefix: events.append("building-blocks"),
        ), mock.patch.object(
            backend, "register_cloud_fonts",
            side_effect=lambda prefix, wine, helper: events.append("font-registration"),
        ), mock.patch.object(
            backend, "prepare_outlook_first_run",
            side_effect=lambda prefix, wine, env: events.append("outlook-preparation"),
        ), mock.patch.object(
            backend.subprocess, "run",
            side_effect=lambda *args, **kwargs: (
                events.append("registry-update") or mock.Mock(returncode=0)
            ),
        ) as run, mock.patch.object(
            backend.subprocess, "Popen",
            side_effect=lambda *args, **kwargs: events.append("launch") or process,
        ) as popen:
            for app in backend.APP_META:
                with self.subTest(app=app):
                    events.clear()
                    run.reset_mock()
                    popen.reset_mock()

                    self.assertEqual(
                        backend.launch_app(str(prefix), str(self.wine), app),
                        process.pid,
                    )

                    expected_events = ["font-registration"]
                    if app == "word":
                        expected_events.insert(0, "building-blocks")
                    if app == "outlook":
                        expected_events.append("outlook-preparation")
                    expected_events.extend(["registry-update", "launch"])
                    self.assertEqual(events, expected_events)
                    run.assert_called_once()
                    self.assertEqual(run.call_args.args[0], registry_command)
                    self.assertEqual(run.call_args.kwargs["stdout"], subprocess.DEVNULL)
                    self.assertEqual(run.call_args.kwargs["stderr"], subprocess.DEVNULL)
                    self.assertEqual(run.call_args.kwargs["timeout"], 30)
                    self.assertIs(run.call_args.kwargs["check"], True)
                    self.assertEqual(
                        run.call_args.kwargs["env"]["WINEPREFIX"], str(prefix),
                    )
                    self.assertIs(
                        run.call_args.kwargs["env"], popen.call_args.kwargs["env"],
                    )

    def test_office_launch_propagates_smoothing_registry_failure(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        executable = prefix / "drive_c/Office/EXCEL.EXE"
        failure = subprocess.CalledProcessError(5, [str(self.wine), "reg", "add"])

        with mock.patch.object(
            backend, "find_office_app", return_value=executable,
        ), mock.patch.object(
            backend, "register_cloud_fonts",
        ), mock.patch.object(
            backend.subprocess, "run", side_effect=failure,
        ), mock.patch.object(
            backend.subprocess, "Popen",
        ) as popen, self.assertRaises(subprocess.CalledProcessError) as raised:
            backend.launch_app(str(prefix), str(self.wine), "excel")

        self.assertIs(raised.exception, failure)
        popen.assert_not_called()

    def test_arbitrary_executable_and_tool_launches_do_not_update_smoothing_registry(self):
        prefix = self.home / ".wine4office"
        self._make_prefix(prefix)
        installer = self.home / "Downloads/Office Setup.exe"
        installer.parent.mkdir()
        installer.write_bytes(b"MZ")

        with mock.patch.object(backend.subprocess, "run") as run, \
             mock.patch.object(
                 backend.subprocess, "Popen", return_value=mock.Mock(pid=4321),
             ):
            backend.launch_executable(str(prefix), str(self.wine), str(installer))
            backend.launch_tool(str(prefix), str(self.wine), "winecfg")

        run.assert_not_called()

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
        launcher = self.home / ".local/share/wine4office/bin/wine4office-launcher"
        launcher.parent.mkdir(parents=True)
        launcher.write_text("launcher")

        def fake_extract(executable, destination):
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(b"icon")
            return destination

        with mock.patch.object(backend, "extract_office_icon", side_effect=fake_extract) as extract:
            created = backend.create_app_shortcuts(
                ["word"], str(prefix), str(self.wine), launcher, True,
            )
        self.assertEqual(len(created), 2)
        extract.assert_called_once_with(
            office / "WINWORD.EXE", backend.data_home() / "icons/wine4office/word.ico",
        )
        menu = backend.data_home() / "applications/wine4office-word.desktop"
        text = menu.read_text()
        self.assertIn("X-Wine4Office-Managed=true", text)
        self.assertIn("%F", text)
        self.assertIn('"' + str(prefix) + '"', text)
        self.assertIn(f"Icon={backend.data_home() / 'icons/wine4office/word.ico'}", text)
        removed = backend.remove_app_shortcuts(["word"])
        self.assertEqual(len(removed), 2)
        self.assertFalse(menu.exists())

    def test_shortcut_creation_accepts_path_environment_and_wine(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "WINWORD.EXE").write_bytes(b"exe")
        launcher = self.home / ".local/bin/wine4office-launcher"
        launcher.parent.mkdir(parents=True)
        launcher.write_text("launcher")
        icon = backend.data_home() / "icons/wine4office/word.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        created = backend.create_app_shortcuts(
            ["word"], prefix, self.wine, launcher, False,
        )

        menu = backend.data_home() / "applications/wine4office-word.desktop"
        self.assertEqual(created, [str(menu)])
        self.assertIn(f'"--prefix" "{prefix}"', menu.read_text())

    def test_setlang_detection_shortcut_and_launch(self):
        prefix = self.home / ".wine4office"
        office = prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        setlang = office / "SETLANG.EXE"
        setlang.write_bytes(b"exe")
        launcher = self.home / ".local/bin/wine4office-launcher"
        launcher.parent.mkdir(parents=True)
        launcher.write_text("launcher")
        icon = backend.data_home() / "icons/wine4office/setlang.ico"
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b"cached icon")

        status = backend.environment_status(str(prefix), str(self.wine))
        self.assertTrue(status["apps"]["setlang"])
        created = backend.create_app_shortcuts(
            ["setlang"], prefix, self.wine, launcher, False,
        )

        menu = backend.data_home() / "applications/wine4office-setlang.desktop"
        self.assertEqual(created, [str(menu)])
        shortcut = menu.read_text()
        self.assertIn('"setlang"', shortcut)
        self.assertNotIn("%F", shortcut)
        self.assertNotIn("MimeType=", shortcut)

        process = mock.Mock(pid=3210)
        with mock.patch.object(backend.subprocess, "run"), \
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
            backend.data_home() / "icons/wine4office/wine4office-manager.png"
        )
        text = shortcut.read_text()
        self.assertEqual(installed_icon.read_bytes(), b"\x89PNG\r\n\x1a\nmanager")
        self.assertIn(f"Icon={installed_icon}", text)
        self.assertNotIn(str(bundled_icons), text)

    def test_cloud_font_registration_skips_unchanged_registry_import(self):
        prefix = self.home / ".wine4office"
        cloud_font = (
            prefix / "drive_c/users/tester/AppData/Local/Microsoft/FontCache/4"
            / "CloudFonts/Aptos/aptos.ttf"
        )
        cloud_font.parent.mkdir(parents=True)
        cloud_font.write_bytes(b"first font revision")
        log = self.root / "wine.log"
        self._script("fc-scan", "#!/bin/sh\nprintf 'Aptos\\n'\n")
        self._script("winepath", "#!/bin/sh\nprintf 'Z:\\\\tmp\\\\fonts.reg\\n'\n")
        self._script("wine", "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$WINE4OFFICE_TEST_LOG\"\n")
        helper = Path(__file__).resolve().parents[1] / "register-office-cloud-fonts.sh"
        env = os.environ.copy()
        env.update({
            "PATH": f"{self.runner}:{env['PATH']}",
            "WINEPREFIX": str(prefix),
            "WINE4OFFICE_TEST_LOG": str(log),
        })

        subprocess.run([str(helper)], env=env, check=True, stdout=subprocess.PIPE, text=True)
        subprocess.run([str(helper)], env=env, check=True, stdout=subprocess.PIPE, text=True)
        self.assertEqual(log.read_text().splitlines(), ["regedit /S Z:\\tmp\\fonts.reg"])

        cloud_font.write_bytes(b"second font revision")
        subprocess.run([str(helper)], env=env, check=True, stdout=subprocess.PIPE, text=True)
        self.assertEqual(
            log.read_text().splitlines(),
            ["regedit /S Z:\\tmp\\fonts.reg", "regedit /S Z:\\tmp\\fonts.reg"],
        )

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
        with mock.patch.object(
            backend.subprocess, "run",
            side_effect=[
                *[mock.Mock(stdout=value) for value in converted],
                mock.Mock(returncode=0),
            ],
        ) as run, mock.patch.object(
            backend.subprocess, "Popen", return_value=process,
        ) as popen:
            pid = backend.launch_app(
                str(prefix), str(self.wine), "word", documents=map(str, documents),
            )

        self.assertEqual(pid, 7654)
        self.assertEqual(
            [call.args[0] for call in run.call_args_list[:len(documents)]],
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
            mock.Mock(returncode=0, stdout=""),
        ]
        with mock.patch.object(backend.subprocess, "run", side_effect=run_results) as run, \
             mock.patch.object(backend.subprocess, "Popen", return_value=process) as popen:
            pid = backend.launch_app(str(prefix), str(self.wine), "outlook")
        self.assertEqual(pid, 7654)
        self.assertEqual(run.call_args_list[2].args[0][-5:],
                         ["/t", "REG_DWORD", "/d", "1037", "/f"])
        command = popen.call_args.args[0]
        self.assertEqual(command, [str(self.wine), str(outlook)])
        self.assertIn("mshtml=", popen.call_args.kwargs["env"]["WINEDLLOVERRIDES"].split(";"))
        self.assertNotIn("mshtml=b", popen.call_args.kwargs["env"]["WINEDLLOVERRIDES"].split(";"))

    def test_shortcut_creation_rejects_missing_application_launcher(self):
        with self.assertRaisesRegex(FileNotFoundError, "application launcher is missing"):
            backend.create_app_shortcuts(
                ["word"], str(self.home / ".wine4office"), str(self.wine),
                self.home / "missing-launcher", False,
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
        self.assertEqual(converted_paths[0], extraction_directory)
        self.assertEqual(converted_paths[1].name, "configuration.xml")
        self.assertEqual(converted_paths[1].parent, extraction_directory.parent)
        self.assertNotEqual(converted_paths[1], configuration.resolve())
        self.assertEqual(snapshot_payloads, [original_payload])
        self.assertTrue(configuration.is_file())
        self.assertEqual(configuration.read_bytes(), changed_payload)
        self.assertFalse(odt.exists())

if __name__ == "__main__":
    unittest.main()
