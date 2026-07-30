#!/usr/bin/env python3

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

MANAGER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MANAGER_DIR))
import wine4office_backend as backend
import wine4office_post_install as post_install


class PostInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name) / "home"
        self.home.mkdir()
        self.environment = mock.patch.dict(os.environ, {
            "HOME": str(self.home),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
        })
        self.environment.start()
        self.config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "runner/bin/wine"),
        }
        self.helper = MANAGER_DIR / "register-office-cloud-fonts.sh"
        self.manager_command = [str(self.home / "bin/Wine4OfficeManager")]
        self.icons = MANAGER_DIR / "icons"

    def tearDown(self):
        self.environment.stop()
        self.temp.cleanup()

    def test_hooks_run_once_per_manager_version(self):
        refreshed = {"updated": ["/shortcut"], "skipped": {}}
        output = []
        with mock.patch.object(
            backend, "current_version", return_value="2.4.0"
        ), mock.patch.object(
            backend, "refresh_managed_app_shortcuts", return_value=refreshed
        ) as office_refresh, mock.patch.object(
            backend, "refresh_manager_shortcut",
            return_value={"updated": True, "path": "/manager.desktop"},
        ) as manager_refresh:
            first = post_install.run_post_install(
                self.config, self.helper, self.manager_command, self.icons,
                output.append,
            )
            second = post_install.run_post_install(
                self.config, self.helper, self.manager_command, self.icons,
                output.append,
            )

        self.assertTrue(first["ran"])
        self.assertFalse(second["ran"])
        office_refresh.assert_called_once_with(
            self.config["prefix"], self.config["wine"], helper=self.helper,
        )
        manager_refresh.assert_called_once_with(self.manager_command, self.icons)
        marker = json.loads(post_install.marker_path().read_text())
        self.assertEqual(marker, {
            "manager_version": "2.4.0",
            "schema": post_install.POST_INSTALL_SCHEMA,
        })
        self.assertTrue(any("refreshed 1" in line for line in output))

    def test_failed_hook_is_retried_and_never_marked_applied(self):
        with mock.patch.object(
            backend, "current_version", return_value="2.4.0"
        ), mock.patch.object(
            backend, "refresh_managed_app_shortcuts",
            side_effect=RuntimeError("migration failed"),
        ) as refresh:
            for _ in range(2):
                with self.assertRaisesRegex(RuntimeError, "migration failed"):
                    post_install.run_post_install(
                        self.config, self.helper, self.manager_command, self.icons,
                        lambda _line: None,
                    )

        self.assertEqual(refresh.call_count, 2)
        self.assertFalse(post_install.marker_path().exists())

    def test_forced_post_update_runs_even_when_version_is_already_applied(self):
        post_install.marker_path().parent.mkdir(parents=True)
        post_install.marker_path().write_text(
            '{"manager_version": "2.4.0", "schema": 1}\n'
        )
        with mock.patch.object(
            backend, "current_version", return_value="2.4.0"
        ), mock.patch.object(
            backend, "refresh_managed_app_shortcuts",
            return_value={"updated": [], "skipped": {}},
        ) as office_refresh, mock.patch.object(
            backend, "refresh_manager_shortcut",
            return_value={"updated": False, "path": "/manager.desktop"},
        ) as manager_refresh:
            result = post_install.run_post_install(
                self.config, self.helper, self.manager_command, self.icons,
                lambda _line: None, force=True,
            )

        self.assertTrue(result["ran"])
        office_refresh.assert_called_once()
        manager_refresh.assert_called_once()

    def test_post_install_refreshes_opted_in_update_schedule(self):
        config = {
            **self.config,
            "automatic_update_checks": True,
            "automatic_update_checks_prompted": True,
        }
        with mock.patch.object(
            backend, "current_version", return_value="2.4.0"
        ), mock.patch.object(
            backend, "refresh_managed_app_shortcuts",
            return_value={"updated": [], "skipped": {}},
        ), mock.patch.object(
            backend, "refresh_manager_shortcut",
            return_value={"updated": False, "path": "/manager.desktop"},
        ), mock.patch.object(
            backend, "install_automatic_update_schedule"
        ) as install:
            post_install.run_post_install(
                config, self.helper, self.manager_command, self.icons,
                lambda _line: None,
            )

        install.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
