#!/usr/bin/env python3

import hashlib
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import wine365_backend as backend


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
if [ "${WINE365_TEST_FAIL:-}" = 1 ]; then exit 9; fi
mkdir -p "$WINEPREFIX"
touch "$WINEPREFIX/system.reg"
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

    def test_default_prefix_is_home_wine365(self):
        config = backend.default_config()
        self.assertEqual(config["prefix"], str(self.home / ".wine365"))
        self.assertEqual(config["update_url"], "")

    def test_update_without_address_is_explicitly_disabled(self):
        with self.assertRaisesRegex(ValueError, "No update address"):
            backend.update_wine365("", lambda line: None)

    def test_update_downloads_and_verifies_onefile_installer(self):
        installer = b"#!/bin/sh\necho update-ran\nexit 0\n"
        manifest = json.dumps({
            "version": "2.0.0",
            "installer_url": "https://updates.example/wine365-2.0.0.run",
            "sha256": hashlib.sha256(installer).hexdigest(),
        }).encode()

        class Response(io.BytesIO):
            def __init__(self, value):
                super().__init__(value)
                self.headers = {"Content-Length": str(len(value))}
            def __enter__(self):
                return self
            def __exit__(self, *args):
                self.close()

        output = []
        with mock.patch.object(backend.urllib.request, "urlopen",
                               side_effect=[Response(manifest), Response(installer)]), \
             mock.patch.object(backend, "current_version", return_value="1.0.0"):
            result = backend.update_wine365("https://updates.example/manifest.json", output.append)
        self.assertIn("2.0.0 installed", result)
        self.assertIn("update-ran", output)
        self.assertFalse(list((backend.cache_home() / "wine365/updates").glob("*.part")))

    def test_rejects_dangerous_prefixes(self):
        for value in ("/", str(self.home), ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                backend.validate_prefix(value)

    def test_create_environment(self):
        prefix = self.home / ".wine365"
        message = backend.create_environment(str(prefix), str(self.wine), False, lambda line: None)
        self.assertTrue((prefix / "system.reg").is_file())
        self.assertIn(str(prefix), message)

    def test_recreate_restores_old_environment_when_wineboot_fails(self):
        prefix = self.home / ".wine365"
        prefix.mkdir()
        (prefix / "system.reg").write_text("old")
        (prefix / "keep-me").write_text("important")
        with mock.patch.dict(os.environ, {"WINE365_TEST_FAIL": "1"}):
            with self.assertRaises(Exception):
                backend.create_environment(str(prefix), str(self.wine), True, lambda line: None)
        self.assertEqual((prefix / "keep-me").read_text(), "important")
        self.assertFalse(list(prefix.parent.glob(".*.wine365-backup-*")))

    def test_launches_exe_in_selected_environment_with_arguments(self):
        prefix = self.home / ".wine365"
        prefix.mkdir()
        (prefix / "system.reg").write_text("registry")
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
        prefix = self.home / ".wine365"
        prefix.mkdir()
        (prefix / "system.reg").write_text("registry")
        document = self.home / "Downloads/readme.txt"
        document.parent.mkdir()
        document.write_text("not an exe")
        with self.assertRaisesRegex(ValueError, "Only .exe"):
            backend.launch_executable(str(prefix), str(self.wine), str(document))

    def test_command_prompt_opens_in_system_terminal(self):
        prefix = self.home / ".wine365"
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
                backend.launch_tool(str(self.home / ".wine365"), str(self.wine), "cmd")

    def test_office_detection_and_shortcut_lifecycle(self):
        prefix = self.home / ".wine365"
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
        launcher = self.home / ".local/share/wine365/bin/wine365-launcher"
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
            office / "WINWORD.EXE", backend.data_home() / "icons/wine365/word.ico",
        )
        menu = backend.data_home() / "applications/wine365-word.desktop"
        text = menu.read_text()
        self.assertIn("X-Wine365-Managed=true", text)
        self.assertIn("%F", text)
        self.assertIn('"' + str(prefix) + '"', text)
        self.assertIn(f"Icon={backend.data_home() / 'icons/wine365/word.ico'}", text)
        removed = backend.remove_app_shortcuts(["word"])
        self.assertEqual(len(removed), 2)
        self.assertFalse(menu.exists())

    def test_outlook_first_run_sets_language_and_safe_launch_options(self):
        prefix = self.home / ".wine365"
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
                ["word"], str(self.home / ".wine365"), str(self.wine),
                self.home / "missing-launcher", False,
            )

    def test_shortcut_removal_does_not_delete_unowned_file(self):
        path = backend.data_home() / "applications/wine365-excel.desktop"
        path.parent.mkdir(parents=True)
        path.write_text("[Desktop Entry]\nName=Someone else\n")
        self.assertEqual(backend.remove_app_shortcuts(["excel"]), [])
        self.assertTrue(path.exists())

if __name__ == "__main__":
    unittest.main()
