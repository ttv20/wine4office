#!/usr/bin/env python3

import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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

    def test_manager_shortcut_installs_a_persistent_icon(self):
        launcher = self.root / "Wine4OfficeManager"
        launcher.write_bytes(b"manager")
        launcher.chmod(0o755)
        bundled_icons = self.root / "transient-bundle/icons"
        bundled_icons.mkdir(parents=True)
        source_icon = bundled_icons / "wine4office-manager.svg"
        source_icon.write_text("<svg/>")

        shortcut = backend.install_manager_shortcut(launcher, bundled_icons)
        installed_icon = (
            backend.data_home() / "icons/hicolor/scalable/apps/wine4office-manager.svg"
        )
        text = shortcut.read_text()
        self.assertEqual(installed_icon.read_text(), "<svg/>")
        self.assertIn(f"Icon={installed_icon}", text)
        self.assertNotIn(str(bundled_icons), text)

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

if __name__ == "__main__":
    unittest.main()
