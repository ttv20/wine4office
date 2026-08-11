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
        for module in ("wine4office_manager.py", "wine4office_incident.py",
                       "wine4office_post_install.py"):
            shutil.copy2(MANAGER_DIR / module, self.root / "lib" / module)
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


    def _make_prefix(self, path):
        path.mkdir(parents=True)
        (path / "system.reg").write_text("system")
        (path / "user.reg").write_text("user")
        (path / "drive_c").mkdir()
        (path / "dosdevices").mkdir()
        (path / ".wine4office-managed-prefix").write_text(
            "Wine4OfficeManager prefix v1\n"
        )
        (path / ".wine4office-managed-prefix").chmod(0o600)
        return path

    def _prefix_backend(self, stop_body):
        self._backend(f"""
import os
import stat
from pathlib import Path
PREFIX_MARKER_NAME = ".wine4office-managed-prefix"
PREFIX_MARKER_CONTENT = b"Wine4OfficeManager prefix v1\\n"
APP_META = {{}}
def validate_prefix(value):
    return Path(value).expanduser().resolve()
def validate_prefix_ownership_fd(prefix_fd, prefix):
    prefix_stat = os.fstat(prefix_fd)
    if not stat.S_ISDIR(prefix_stat.st_mode) or prefix_stat.st_uid != os.getuid():
        raise ValueError(f"Wine prefix directory is unsafe: {{prefix}}")
    try:
        marker_fd = os.open(
            PREFIX_MARKER_NAME,
            os.O_RDONLY | os.O_NOFOLLOW | getattr(os, "O_CLOEXEC", 0),
            dir_fd=prefix_fd,
        )
    except OSError as error:
        raise ValueError(
            f"Wine prefix is not owned by Wine4Office Manager: {{prefix}}"
        ) from error
    try:
        marker_stat = os.fstat(marker_fd)
        if (not stat.S_ISREG(marker_stat.st_mode)
                or marker_stat.st_uid != os.getuid()
                or stat.S_IMODE(marker_stat.st_mode) != 0o600):
            raise ValueError(f"Wine prefix ownership marker is unsafe: {{prefix}}")
        content = os.read(marker_fd, len(PREFIX_MARKER_CONTENT) + 1)
        if content != PREFIX_MARKER_CONTENT:
            raise ValueError(f"Wine prefix ownership marker is invalid: {{prefix}}")
    finally:
        os.close(marker_fd)
def load_config():
    return {{"wine": "/runner/bin/wine"}}
def stop_wine(prefix, wine, use_x11=True):
{stop_body}
def uninstall_preload_service():
    pass
def uninstall_automatic_update_schedule():
    pass
def remove_app_shortcuts(_apps):
    pass
def remove_manager_shortcut():
    pass
""")
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

    def test_prefix_stop_failure_aborts_before_recursive_delete(self):
        prefix = self._make_prefix(self.base / "managed-prefix")
        self._prefix_backend("    raise RuntimeError('wineserver still active')")
        result = subprocess.run(
            [str(UNINSTALLER), "--remove-prefix", str(prefix)],
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shutdown failed", result.stderr)
        self.assertTrue(prefix.exists())

    def test_prefix_removal_refuses_an_ordinary_user_directory(self):
        directory = self.base / "user-data"
        directory.mkdir()
        (directory / "important").write_text("keep")
        self._prefix_backend("    raise AssertionError('stop must not run')")
        result = subprocess.run(
            [str(UNINSTALLER), "--remove-prefix", str(directory)],
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Wine prefix", result.stderr)
        self.assertEqual((directory / "important").read_text(), "keep")

    def test_prefix_removal_refuses_symlink_swap(self):
        prefix = self._make_prefix(self.base / "managed-prefix")
        victim = self.base / "user-data"
        victim.mkdir()
        (victim / "important").write_text("keep")
        original = self.base / "managed-prefix-original"
        prefix.rename(original)
        prefix.symlink_to(victim, target_is_directory=True)
        self._prefix_backend("    raise AssertionError('stop must not run')")
        result = subprocess.run(
            [str(UNINSTALLER), "--remove-prefix", str(prefix)],
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(original.exists())
        self.assertEqual((victim / "important").read_text(), "keep")

    def test_valid_owned_prefix_is_removed_after_shutdown(self):
        prefix = self._make_prefix(self.base / "managed-prefix")
        self._prefix_backend("    return None")
        result = subprocess.run(
            [str(UNINSTALLER), "--remove-prefix", str(prefix)],
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(prefix.exists())
        self.assertIn("Removed Wine environment", result.stdout)


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
