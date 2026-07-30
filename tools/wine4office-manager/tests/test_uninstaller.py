#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


MANAGER_DIR = Path(__file__).resolve().parents[1]
UNINSTALLER = MANAGER_DIR / "uninstall-user.sh"


class UninstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.home = self.base / "home"
        self.root = self.home / ".local/share/wine4office"
        (self.root / "lib").mkdir(parents=True)
        (self.root / "bin").mkdir()
        (self.root / "bin/wine4office-manager").write_text("executable")
        self.env = {
            **os.environ,
            "HOME": str(self.home),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_CACHE_HOME": str(self.home / ".cache"),
            "WINE4OFFICE_MANAGER_HOME": str(self.root),
            "WINE4OFFICE_BIN_HOME": str(self.home / ".local/bin"),
        }

    def tearDown(self):
        self.temp.cleanup()

    def _backend(self, body):
        (self.root / "lib/wine4office_backend.py").write_text(body)

    def test_preload_stop_refusal_aborts_before_executable_deletion(self):
        self._backend("""
def uninstall_preload_service():
    raise RuntimeError('Office is active; refusing to stop preload')
""")
        result = subprocess.run(
            [str(UNINSTALLER)], env=self.env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Office is active; refusing", result.stderr)
        self.assertTrue((self.root / "bin/wine4office-manager").exists())
        self.assertTrue((self.root / "lib/wine4office_backend.py").exists())

    def test_preload_cleanup_runs_before_installed_backend_is_deleted(self):
        marker = self.base / "preload-cleaned"
        self._backend(f"""
from pathlib import Path
APP_META = {{}}
def uninstall_preload_service():
    assert Path(__file__).is_file()
    Path({str(marker)!r}).write_text('cleaned')
def remove_app_shortcuts(_apps):
    assert Path({str(marker)!r}).read_text() == 'cleaned'
""")
        result = subprocess.run(
            [str(UNINSTALLER)], env=self.env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(marker.read_text(), "cleaned")
        self.assertFalse((self.root / "lib").exists())

    def test_missing_backend_refuses_to_orphan_owned_service_files(self):
        shutil.rmtree(self.root / "lib")
        unit = self.home / ".config/systemd/user/wine4office-preload.service"
        unit.parent.mkdir(parents=True)
        unit.write_text("# Managed by Wine4OfficeManager: preload-service-v1\n")
        result = subprocess.run(
            [str(UNINSTALLER)], env=self.env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("installed backend is missing", result.stderr)
        self.assertTrue(unit.exists())
        self.assertTrue((self.root / "bin/wine4office-manager").exists())


if __name__ == "__main__":
    unittest.main()
