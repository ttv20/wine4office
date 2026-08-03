#!/usr/bin/env python3

import hashlib
import io
import os
import sys
import tarfile
import tempfile
import time
import types
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import wine4office_backend as backend
import wine4office_manager as manager


class Response(io.BytesIO):
    def __init__(self, payload: bytes):
        super().__init__(payload)
        self.headers = {"Content-Length": str(len(payload))}

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class PassthroughZstd:
    def stream_reader(self, source):
        return source


class UpdaterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.home.mkdir()
        self.install_root = self.root / "installed"
        self.install_root.mkdir()
        self.environment = mock.patch.dict(os.environ, {
            "HOME": str(self.home),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_CACHE_HOME": str(self.home / ".cache"),
            "WINE4OFFICE_MANAGER_ROOT": str(self.install_root),
        })
        self.environment.start()

    def tearDown(self):
        self.environment.stop()
        self.temp.cleanup()

    def metadata(self):
        return {
            "schema_version": 1,
            "channel": "stable",
            "metadata_url": "https://future.example/releases/release.json",
            "manager": {
                "version": "2.0.0",
                "url": "Wine4OfficeManager-2.0.0-x86_64",
                "sha256": "1" * 64,
                "size": 123,
            },
            "wine": {
                "version": "11.2.0",
                "url": "wine4office-11.2.0-x86_64.tar.zst",
                "sha256": "2" * 64,
                "size": 456,
                "format": "tar.zst",
            },
        }

    def test_frozen_https_uses_bundled_certificate_store(self):
        context = object()
        certifi = types.SimpleNamespace(where=lambda: "/bundle/cacert.pem")
        with mock.patch.dict(sys.modules, {"certifi": certifi}), \
             mock.patch.object(backend.sys, "frozen", True, create=True), \
             mock.patch.object(
                 backend.ssl, "create_default_context", return_value=context
             ) as create:
            self.assertIs(backend._https_context(), context)
        create.assert_called_once_with(cafile="/bundle/cacert.pem")

    def test_schema_v1_resolves_artifacts_and_canonical_metadata_url(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://current.example/downloads/release.json"
        )
        self.assertEqual(parsed["schema_version"], 1)
        self.assertEqual(
            parsed["manager"]["url"],
            "https://current.example/downloads/Wine4OfficeManager-2.0.0-x86_64",
        )
        self.assertEqual(
            parsed["wine"]["url"],
            "https://current.example/downloads/wine4office-11.2.0-x86_64.tar.zst",
        )
        self.assertEqual(
            parsed["metadata_url"], "https://future.example/releases/release.json"
        )
        backend.persist_metadata_url(parsed["metadata_url"])
        self.assertEqual(
            (self.install_root / "UPDATE_URL").read_text().strip(),
            "https://future.example/releases/release.json",
        )

    def test_relative_metadata_migration_resolves_to_canonical_https_url(self):
        payload = self.metadata()
        payload["metadata_url"] = "../stable/release.json"
        parsed = backend.parse_release_metadata(
            payload, "https://updates.example/releases/current/release.json"
        )
        self.assertEqual(
            parsed["metadata_url"], "https://updates.example/releases/stable/release.json"
        )

    def test_installed_metadata_url_overrides_hosting_default(self):
        configured = "https://updates.example/channel/release.json"
        (self.install_root / "UPDATE_URL").write_text(configured + "\n")
        self.assertEqual(backend.default_config()["update_url"], configured)

    def test_prerelease_check_resolves_newest_published_github_release(self):
        releases = [
            {"draft": True, "assets": []},
            {
                "draft": False,
                "prerelease": True,
                "assets": [{
                    "name": "release.json",
                    "browser_download_url": (
                        "https://github.com/ttv20/wine4office/releases/download/"
                        "v0.2.0-rc.1/release.json"
                    ),
                }],
            },
        ]
        with mock.patch.object(
            backend.urllib.request, "urlopen",
            return_value=Response(__import__("json").dumps(releases).encode()),
        ) as urlopen:
            resolved = backend._github_latest_release_metadata_url(
                "https://github.com/ttv20/wine4office/releases/latest/download/release.json"
            )

        self.assertEqual(
            resolved,
            "https://github.com/ttv20/wine4office/releases/download/"
            "v0.2.0-rc.1/release.json",
        )
        self.assertEqual(
            urlopen.call_args.args[0].full_url,
            "https://api.github.com/repos/ttv20/wine4office/releases?per_page=20",
        )

    def test_prerelease_check_uses_discovered_metadata(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        source = "https://github.com/owner/repo/releases/download/v2/release.json"
        with mock.patch.object(
            backend, "_github_latest_release_metadata_url", return_value=source
        ) as discover, mock.patch.object(
            backend, "fetch_release_metadata", return_value=parsed
        ) as fetch, mock.patch.object(
            backend, "available_updates", return_value={}
        ):
            backend.check_for_updates(
                "https://github.com/owner/repo/releases/latest/download/release.json",
                include_prereleases=True,
            )

        discover.assert_called_once()
        fetch.assert_called_once_with(source, None, None)

    def test_prerelease_check_rejects_custom_metadata_provider(self):
        with self.assertRaisesRegex(ValueError, "standard GitHub"):
            backend._github_latest_release_metadata_url(
                "https://updates.example/release.json"
            )

    def test_frozen_manager_reads_embedded_release_channel(self):
        bundle = self.root / "frozen-bundle"
        bundle.mkdir()
        (bundle / "CHANNEL").write_text("beta\n")
        with mock.patch.object(backend, "installed_root", return_value=None), \
             mock.patch.object(backend, "__file__", str(bundle / "wine4office_backend.py")), \
             mock.patch.object(backend.sys, "frozen", True, create=True):
            self.assertEqual(backend.configured_update_channel(), "beta")

    def test_invalid_schema_and_non_https_urls_are_rejected(self):
        invalid = self.metadata()
        invalid["schema_version"] = 2
        with self.assertRaisesRegex(ValueError, "schema_version"):
            backend.parse_release_metadata(invalid, "https://updates.example/release.json")
        invalid = self.metadata()
        invalid["manager"]["url"] = "http://updates.example/manager"
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            backend.parse_release_metadata(invalid, "https://updates.example/release.json")

    def test_channel_mismatch_is_rejected_before_update_offers(self):
        payload = self.metadata()
        payload["channel"] = "beta"
        with self.assertRaisesRegex(ValueError, "does not match expected channel"):
            backend.parse_release_metadata(
                payload, "https://updates.example/release.json"
            )
        (self.install_root / "UPDATE_CHANNEL").write_text("beta\n")
        parsed = backend.parse_release_metadata(
            payload, "https://updates.example/release.json"
        )
        self.assertEqual(parsed["channel"], "beta")
        with mock.patch.object(backend, "current_version") as current:
            with self.assertRaisesRegex(ValueError, "does not match expected channel"):
                backend.available_updates(parsed, expected_channel="stable")
        current.assert_not_called()

    def test_version_ordering_and_independent_component_offers(self):
        self.assertGreater(backend.compare_versions("1.10.0", "1.9.9"), 0)
        self.assertGreater(backend.compare_versions("2.0.0", "2.0.0-rc.2"), 0)
        self.assertEqual(backend.compare_versions("2.0.0+build.2", "2.0.0+build.1"), 0)
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        with mock.patch.object(backend, "current_version", return_value="2.0.0"), \
             mock.patch.object(backend, "current_wine_version", return_value="11.1.0"):
            updates = backend.available_updates(parsed)
        self.assertEqual(set(updates), {"wine"})
        with mock.patch.object(backend, "current_version", return_value="1.0.0"), \
             mock.patch.object(backend, "current_wine_version", return_value="11.1.0"):
            updates = backend.available_updates(parsed, {"wine": "11.2.0"})
        self.assertEqual(set(updates), {"manager"})

    def test_equal_or_downgrade_is_rejected_before_download(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        with mock.patch.object(backend, "current_version", return_value="2.0.0"), \
             mock.patch.object(backend, "current_wine_version", return_value="12.0.0"), \
             mock.patch.object(backend, "_download_artifact") as download:
            with self.assertRaisesRegex(ValueError, "equal, older"):
                backend.install_release_updates(parsed, ["manager"], lambda line: None)
        download.assert_not_called()

    def test_check_does_not_download_artifacts_before_consent_hook(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "runner/bin/wine"),
            "desktop_copy": False,
            "update_url": "https://updates.example/release.json",
            "skipped_updates": {},
            "include_prereleases": True,
        }
        result = {"metadata": parsed, "updates": {"manager": parsed["manager"]}}
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(backend, "check_for_updates", return_value=result) as check, \
             mock.patch.object(backend, "save_config"), \
             mock.patch.object(backend, "persist_metadata_url"), \
             mock.patch.object(backend, "install_release_updates", return_value="installed") as install, \
             mock.patch.object(manager.ManagerState, "_run_updated_manager_post_install") as post_install:
            state = manager.ManagerState()
            self.assertTrue(state.start_update_check())
            self._wait_for(lambda: state.snapshot()["updater"]["checked"])
            check.assert_called_once_with(
                config["update_url"], {}, include_prereleases=True
            )
            self.assertEqual(
                state.snapshot()["config"]["update_url"],
                "https://future.example/releases/release.json",
            )
            install.assert_not_called()
            state.start_offered_update(["manager"])
            self._wait_for(lambda: not state.snapshot()["task"]["running"])
            install.assert_called_once()
            post_install.assert_called_once()
            post_config = post_install.call_args.args[0]
            self.assertEqual(post_config["prefix"], config["prefix"])
            self.assertEqual(post_config["wine"], config["wine"])
            self.assertEqual(
                post_config["update_url"],
                "https://future.example/releases/release.json",
            )
            task = state.snapshot()["task"]
            self.assertEqual(task["status"], "completed")
            self.assertTrue(task["restart_required"])

    def test_post_install_failure_still_offers_restart_for_retry(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "runner/bin/wine"),
            "desktop_copy": False,
            "use_x11": True,
            "update_url": "https://updates.example/release.json",
            "skipped_updates": {},
        }
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "install_release_updates", return_value="installed"
             ), mock.patch.object(
                 manager.ManagerState, "_run_updated_manager_post_install",
                 side_effect=RuntimeError("hook failed"),
             ):
            state = manager.ManagerState()
            state.updater["offer"] = {
                "id": "manager:2.0.0",
                "metadata": parsed,
                "updates": {"manager": parsed["manager"]},
            }
            state.start_offered_update(["manager"])
            self._wait_for(lambda: not state.snapshot()["task"]["running"])

        task = state.snapshot()["task"]
        self.assertEqual(task["status"], "failed")
        self.assertTrue(task["restart_required"])
        self.assertIn("hook failed", task["log"])

    def test_wine_update_rebinds_running_background_service_to_new_runner(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "old-runner/bin/wine"),
            "desktop_copy": False,
            "use_x11": True,
            "update_url": "https://updates.example/release.json",
            "skipped_updates": {},
        }
        transition = {"binding": {"prefix": config["prefix"]}, "active": True}
        new_runner = self.home / "installed-runner"
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "prepare_preload_runner_update", return_value=transition
             ) as prepare, \
             mock.patch.object(backend, "stop_wine"), \
             mock.patch.object(
                 backend, "install_release_updates", return_value="installed"
             ), \
             mock.patch.object(
                 backend, "runner_update_target", return_value=new_runner
             ), \
             mock.patch.object(
                 backend, "update_wine_prefix",
                 return_value="Wine environment updated and restarted",
             ) as wineboot, \
             mock.patch.object(backend, "finish_preload_runner_update") as finish, \
             mock.patch.object(backend, "restore_preload_after_runner_update") as restore, \
             mock.patch.object(backend, "save_config"):
            state = manager.ManagerState()
            state.updater["offer"] = {
                "id": "wine:11.2.0",
                "metadata": parsed,
                "updates": {"wine": parsed["wine"]},
            }
            state.start_offered_update(["wine"])
            self._wait_for(lambda: not state.snapshot()["task"]["running"])

        new_wine = str(new_runner / "bin/wine")
        prepare.assert_called_once_with(config["prefix"], True)
        wineboot.assert_called_once_with(
            config["prefix"], new_wine, True, state.output,
            state.cancel_event, state.set_process,
        )
        finish.assert_called_once_with(transition, new_wine)
        restore.assert_not_called()
        self.assertEqual(state.snapshot()["config"]["wine"], new_wine)
        self.assertIn(
            "Updated the background services",
            state.snapshot()["task"]["log"],
        )

    def test_failed_wine_update_resumes_paused_background_service(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "old-runner/bin/wine"),
            "desktop_copy": False,
            "use_x11": True,
            "update_url": "https://updates.example/release.json",
            "skipped_updates": {},
        }
        transition = {"binding": {"prefix": config["prefix"]}, "active": True}
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "prepare_preload_runner_update", return_value=transition
             ), \
             mock.patch.object(backend, "stop_wine"), \
             mock.patch.object(
                 backend, "install_release_updates",
                 side_effect=RuntimeError("runner install failed"),
             ), \
             mock.patch.object(
                 backend, "restore_preload_after_runner_update"
             ) as restore:
            state = manager.ManagerState()
            state.updater["offer"] = {
                "id": "wine:11.2.0",
                "metadata": parsed,
                "updates": {"wine": parsed["wine"]},
            }
            state.start_offered_update(["wine"])
            self._wait_for(lambda: not state.snapshot()["task"]["running"])

        restore.assert_called_once_with(transition)
        self.assertEqual(state.snapshot()["task"]["status"], "failed")

    def test_failed_prefix_self_update_does_not_switch_runner(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "old-runner/bin/wine"),
            "desktop_copy": False,
            "use_x11": True,
            "update_url": "https://updates.example/release.json",
            "skipped_updates": {},
        }
        transition = {"binding": {"prefix": config["prefix"]}, "active": True}
        new_runner = self.home / "installed-runner"
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(
                 backend, "prepare_preload_runner_update", return_value=transition
             ), \
             mock.patch.object(backend, "stop_wine"), \
             mock.patch.object(
                 backend, "install_release_updates", return_value="installed"
             ), \
             mock.patch.object(
                 backend, "runner_update_target", return_value=new_runner
             ), \
             mock.patch.object(
                 backend, "update_wine_prefix",
                 side_effect=RuntimeError("wineboot update failed"),
             ), \
             mock.patch.object(backend, "finish_preload_runner_update") as finish, \
             mock.patch.object(
                 backend, "restore_preload_after_runner_update"
             ) as restore, \
             mock.patch.object(backend, "save_config") as save:
            state = manager.ManagerState()
            state.updater["offer"] = {
                "id": "wine:11.2.0",
                "metadata": parsed,
                "updates": {"wine": parsed["wine"]},
            }
            state.start_offered_update(["wine"])
            self._wait_for(lambda: not state.snapshot()["task"]["running"])

        finish.assert_not_called()
        restore.assert_called_once_with(transition)
        save.assert_not_called()
        self.assertEqual(state.snapshot()["config"]["wine"], config["wine"])
        self.assertIn("wineboot update failed", state.snapshot()["task"]["log"])

    def test_skipping_offer_persists_component_version_without_download(self):
        parsed = backend.parse_release_metadata(
            self.metadata(), "https://updates.example/release.json"
        )
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "runner/bin/wine"),
            "desktop_copy": False,
            "update_url": "https://updates.example/release.json",
            "skipped_updates": {},
        }
        result = {"metadata": parsed, "updates": {"manager": parsed["manager"]}}
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(backend, "check_for_updates", return_value=result), \
             mock.patch.object(backend, "save_config") as save, \
             mock.patch.object(backend, "persist_metadata_url"), \
             mock.patch.object(backend, "install_release_updates") as install:
            state = manager.ManagerState()
            state.start_update_check()
            self._wait_for(lambda: state.snapshot()["updater"]["checked"])
            state.skip_offered_updates(["manager"])
            snapshot = state.snapshot()
        install.assert_not_called()
        save.assert_called()
        self.assertEqual(snapshot["config"]["skipped_updates"]["manager"], "2.0.0")
        self.assertIsNone(snapshot["updater"]["offer"])

    def test_offline_background_check_is_nonfatal(self):
        config = {
            "prefix": str(self.home / ".wine4office"),
            "wine": str(self.home / "runner/bin/wine"),
            "desktop_copy": False,
            "update_url": "https://offline.example/release.json",
            "skipped_updates": {},
        }
        with mock.patch.object(backend, "load_config", return_value=config), \
             mock.patch.object(backend, "check_for_updates", side_effect=OSError("offline")):
            state = manager.ManagerState()
            state.start_update_check()
            self._wait_for(lambda: state.snapshot()["updater"]["checked"])
            snapshot = state.snapshot()
        self.assertEqual(snapshot["task"]["status"], "idle")
        self.assertEqual(snapshot["updater"]["error"], "offline")

    def test_bad_digest_removes_partial_download_without_touching_target(self):
        payload = b"standalone-manager"
        component = {
            "version": "2.0.0",
            "url": "https://updates.example/manager",
            "size": len(payload),
            "sha256": "0" * 64,
        }
        target = self.install_root / "lib/wine4office-manager-qt"
        target.parent.mkdir()
        target.write_bytes(b"old-manager")
        with mock.patch.object(backend.urllib.request, "urlopen", return_value=Response(payload)):
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                backend._download_artifact("manager", component)
        self.assertEqual(target.read_bytes(), b"old-manager")
        self.assertFalse(list((backend.cache_home() / "wine4office/updates").glob("*.part")))

    def test_artifact_download_reports_numeric_progress(self):
        payload = b"updated-manager" * 1024
        component = {
            "version": "2.0.0",
            "url": "https://updates.example/manager",
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
        events = []
        with mock.patch.object(
            backend.urllib.request, "urlopen", return_value=Response(payload)
        ):
            downloaded = backend._download_artifact(
                "manager", component,
                progress=lambda label, value: events.append((label, value)),
            )
        downloaded.unlink()

        self.assertEqual(events[0], ("Downloading Wine4Office Manager 2.0.0", 0))
        self.assertIn(("Downloading Wine4Office Manager 2.0.0", 100), events)
        self.assertEqual(
            events[-1], ("Verified Wine4Office Manager 2.0.0", 100)
        )

    def test_archive_traversal_is_rejected_before_extraction(self):
        archive = self.root / "malicious.tar.zst"
        with tarfile.open(archive, "w") as bundle:
            member = tarfile.TarInfo("../escape")
            member.size = 4
            bundle.addfile(member, io.BytesIO(b"evil"))
        fake = types.SimpleNamespace(ZstdDecompressor=PassthroughZstd)
        destination = self.root / "extract"
        with mock.patch.dict(sys.modules, {"zstandard": fake}):
            with self.assertRaisesRegex(ValueError, "escapes"):
                backend.safe_extract_wine_archive(archive, destination)
        self.assertFalse(destination.exists())
        self.assertFalse((self.root / "escape").exists())

    def test_archive_link_escape_is_rejected_before_extraction(self):
        archive = self.root / "malicious-link.tar.zst"
        with tarfile.open(archive, "w") as bundle:
            member = tarfile.TarInfo("wine4office/lib/escape")
            member.type = tarfile.SYMTYPE
            member.linkname = "../../../outside"
            bundle.addfile(member)
        fake = types.SimpleNamespace(ZstdDecompressor=PassthroughZstd)
        destination = self.root / "link-extract"
        with mock.patch.dict(sys.modules, {"zstandard": fake}):
            with self.assertRaisesRegex(ValueError, "escapes"):
                backend.safe_extract_wine_archive(archive, destination)
        self.assertFalse(destination.exists())

    def test_safe_archive_extracts_one_runner_tree(self):
        archive = self.root / "runner.tar.zst"
        with tarfile.open(archive, "w") as bundle:
            member = tarfile.TarInfo("wine4office/bin/wine")
            member.mode = 0o755
            member.size = 4
            bundle.addfile(member, io.BytesIO(b"wine"))
        fake = types.SimpleNamespace(ZstdDecompressor=PassthroughZstd)
        destination = self.root / "extract"
        with mock.patch.dict(sys.modules, {"zstandard": fake}):
            runner = backend.safe_extract_wine_archive(archive, destination)
        self.assertEqual(runner, destination / "wine4office")
        self.assertTrue((runner / "bin/wine").is_file())

    def test_archive_declared_file_and_total_expansion_limits_are_rejected(self):
        archive = self.root / "expansion.tar.zst"
        with tarfile.open(archive, "w") as bundle:
            member = tarfile.TarInfo("wine4office/bin/wine")
            member.mode = 0o755
            member.size = 4
            bundle.addfile(member, io.BytesIO(b"wine"))
        fake = types.SimpleNamespace(ZstdDecompressor=PassthroughZstd)
        with mock.patch.dict(sys.modules, {"zstandard": fake}), \
             mock.patch.object(backend, "MAX_WINE_FILE_SIZE", 3):
            with self.assertRaisesRegex(ValueError, "declared-size limit"):
                backend.safe_extract_wine_archive(
                    archive, self.root / "file-limit-extract"
                )
        with mock.patch.dict(sys.modules, {"zstandard": fake}), \
             mock.patch.object(backend, "MAX_WINE_FILE_SIZE", 10), \
             mock.patch.object(backend, "MAX_WINE_EXTRACTED_SIZE", 3):
            with self.assertRaisesRegex(ValueError, "total declared-size limit"):
                backend.safe_extract_wine_archive(
                    archive, self.root / "total-limit-extract"
                )

    def test_archive_member_count_limit_is_rejected_before_extraction(self):
        archive = self.root / "too-many-members.tar.zst"
        with tarfile.open(archive, "w") as bundle:
            bundle.addfile(tarfile.TarInfo("wine4office"))
            bundle.addfile(tarfile.TarInfo("wine4office/bin"))
        fake = types.SimpleNamespace(ZstdDecompressor=PassthroughZstd)
        destination = self.root / "member-limit-extract"
        with mock.patch.dict(sys.modules, {"zstandard": fake}), \
             mock.patch.object(backend, "MAX_WINE_ARCHIVE_MEMBERS", 1):
            with self.assertRaisesRegex(ValueError, "member-count limit"):
                backend.safe_extract_wine_archive(archive, destination)
        self.assertFalse(destination.exists())

    def test_extraction_counts_bytes_instead_of_trusting_declared_size(self):
        member = tarfile.TarInfo("wine4office/bin/wine")
        member.mode = 0o755
        member.size = 1

        class ExpandingBundle:
            def __iter__(self):
                return iter([member])

            def extractfile(self, _member):
                return io.BytesIO(b"expanded")

        destination = self.root / "counted-extract"
        destination.mkdir()
        with self.assertRaisesRegex(ValueError, "exceeds its declared size"):
            backend._extract_validated_tar(ExpandingBundle(), destination)

    def test_contended_install_revalidates_version_after_acquiring_lock(self):
        version = self.install_root / "VERSION"
        version.write_text("1.0.0\n")
        target = self.install_root / "lib/wine4office-manager-qt"
        target.parent.mkdir()
        target.write_bytes(b"current-manager")
        download = self.root / "manager-download"
        download.write_bytes(b"stale-manager")

        class ContendedLock:
            def __enter__(inner_self):
                version.write_text("3.0.0\n")
                return inner_self

            def __exit__(inner_self, *_args):
                return False

        with mock.patch.object(
                backend, "_download_artifact", return_value=download
        ), mock.patch.object(
                backend, "_install_update_lock", return_value=ContendedLock()
        ):
            with self.assertRaisesRegex(ValueError, "another updater completed"):
                backend.install_release_updates(
                    self.metadata(), ["manager"], lambda _line: None
                )
        self.assertEqual(version.read_text(), "3.0.0\n")
        self.assertEqual(target.read_bytes(), b"current-manager")

    def test_manager_update_preserves_legacy_runner_version(self):
        (self.install_root / "VERSION").write_text("1.4.0\n")
        runner = self.install_root / "runner/bin"
        runner.mkdir(parents=True)
        wine = runner / "wine"
        wine.write_bytes(b"legacy-runner")
        wine.chmod(0o755)
        target = self.install_root / "lib/wine4office-manager-qt"
        target.parent.mkdir()
        target.write_bytes(b"old-manager")
        download = self.root / "manager-download"
        download.write_bytes(b"new-manager")

        with mock.patch.object(
                backend, "_download_artifact", return_value=download
        ):
            backend.install_release_updates(
                self.metadata(), ["manager"], lambda _line: None
            )
        self.assertEqual((self.install_root / "VERSION").read_text(), "2.0.0\n")
        self.assertEqual(
            (self.install_root / "WINE_VERSION").read_text(), "1.4.0\n"
        )
        self.assertEqual(target.read_bytes(), b"new-manager")

    def test_second_component_failure_rolls_back_all_targets_and_versions(self):
        manager_target = self.install_root / "lib/wine4office-manager-qt"
        manager_target.parent.mkdir()
        manager_target.write_bytes(b"old-manager")
        runner_target = self.install_root / "runner"
        (runner_target / "bin").mkdir(parents=True)
        old_wine = runner_target / "bin/wine"
        old_wine.write_bytes(b"old-wine")
        old_wine.chmod(0o755)
        (runner_target / "identity").write_text("old-runner")
        (self.install_root / "VERSION").write_text("1.0.0\n")
        (self.install_root / "WINE_VERSION").write_text("11.1.0\n")
        (self.install_root / "UPDATE_URL").write_text(
            "https://updates.example/old.json\n"
        )

        manager_download = self.root / "manager-download"
        manager_download.write_bytes(b"new-manager")
        wine_download = self.root / "wine-download.tar.zst"
        with tarfile.open(wine_download, "w") as bundle:
            for name, contents, mode in (
                    ("wine4office/bin/wine", b"new-wine", 0o755),
                    ("wine4office/identity", b"new-runner", 0o644),
            ):
                member = tarfile.TarInfo(name)
                member.mode = mode
                member.size = len(contents)
                bundle.addfile(member, io.BytesIO(contents))

        real_write = backend._atomic_write_text

        def fail_wine_version(path, value):
            if path.name == "WINE_VERSION":
                raise OSError("simulated second-component failure")
            return real_write(path, value)

        fake = types.SimpleNamespace(ZstdDecompressor=PassthroughZstd)
        with mock.patch.dict(sys.modules, {"zstandard": fake}), \
             mock.patch.object(
                 backend, "_download_artifact",
                 side_effect=[manager_download, wine_download],
             ), mock.patch.object(
                 backend, "_atomic_write_text", side_effect=fail_wine_version
             ):
            with self.assertRaisesRegex(OSError, "second-component failure"):
                backend.install_release_updates(
                    self.metadata(), ["manager", "wine"], lambda _line: None
                )

        self.assertEqual(manager_target.read_bytes(), b"old-manager")
        self.assertEqual((runner_target / "identity").read_text(), "old-runner")
        self.assertEqual((self.install_root / "VERSION").read_text(), "1.0.0\n")
        self.assertEqual(
            (self.install_root / "WINE_VERSION").read_text(), "11.1.0\n"
        )
        self.assertEqual(
            (self.install_root / "UPDATE_URL").read_text(),
            "https://updates.example/old.json\n",
        )

    def test_frozen_manager_updates_the_outer_executable(self):
        outer = self.root / "Wine4OfficeManager"
        with mock.patch.object(backend, "installed_root", return_value=None), \
             mock.patch.object(backend.sys, "frozen", True, create=True), \
             mock.patch.object(backend.sys, "executable", str(outer)):
            self.assertEqual(backend.manager_update_target(), outer)

    def test_installer_layout_is_discovered_by_frozen_manager(self):
        root = self.root / "custom-install"
        executable = root / "bin/Wine4OfficeManager"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"manager")
        (root / "STANDALONE").write_text("Wine4OfficeManager\n")
        (root / "UPDATE_URL").write_text("https://updates.example/release.json\n")
        with mock.patch.dict(os.environ, {"WINE4OFFICE_MANAGER_ROOT": ""}), \
             mock.patch.object(backend.sys, "frozen", True, create=True), \
             mock.patch.object(backend.sys, "executable", str(executable)):
            self.assertEqual(backend.installed_root(), root.resolve())
            self.assertEqual(backend.manager_update_target(), executable.resolve())
            self.assertEqual(backend.runner_update_target(), root.resolve() / "runner")
            self.assertEqual(
                backend.configured_update_url(),
                "https://updates.example/release.json",
            )

    def test_frozen_manager_requires_installer_marker_for_root_discovery(self):
        root = self.root / "unmarked-install"
        executable = root / "bin/Wine4OfficeManager"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"manager")
        with mock.patch.dict(os.environ, {"WINE4OFFICE_MANAGER_ROOT": ""}), \
             mock.patch.object(backend, "__file__", str(self.root / "bundle/backend.py")), \
             mock.patch.object(backend.sys, "frozen", True, create=True), \
             mock.patch.object(backend.sys, "executable", str(executable)):
            self.assertIsNone(backend.installed_root())

    def test_standalone_update_persists_version_for_concurrent_revalidation(self):
        outer = self.root / "Wine4OfficeManager"
        outer.write_bytes(b"old-manager")
        outer.chmod(0o755)
        download = self.root / "manager-download"
        download.write_bytes(b"new-manager")
        runner_target = self.root / "standalone-runner"
        with mock.patch.object(backend, "installed_root", return_value=None), \
             mock.patch.object(backend, "manager_update_target", return_value=outer), \
             mock.patch.object(backend, "runner_update_target", return_value=runner_target), \
             mock.patch.object(backend, "current_version", return_value="1.0.0"), \
             mock.patch.object(backend, "current_wine_version", return_value="11.2.0"), \
             mock.patch.object(backend, "_download_artifact", return_value=download):
            backend.install_release_updates(
                self.metadata(), ["manager"], lambda _line: None
            )
        self.assertEqual(outer.read_bytes(), b"new-manager")
        self.assertEqual(
            backend.standalone_manager_version_path(outer).read_text(),
            '{"sha256": "' + "1" * 64 + '", "version": "2.0.0"}\n',
        )

    def test_frozen_manager_reads_only_digest_bound_outer_version(self):
        outer = self.root / "Wine4OfficeManager"
        outer.write_bytes(b"manager")
        digest = hashlib.sha256(outer.read_bytes()).hexdigest()
        backend.standalone_manager_version_path(outer).write_text(
            f'{{"version": "3.1.4", "sha256": "{digest}"}}\n'
        )
        with mock.patch.object(backend, "installed_root", return_value=None), \
             mock.patch.object(backend.sys, "frozen", True, create=True), \
             mock.patch.object(backend.sys, "executable", str(outer)):
            self.assertEqual(backend.current_version(), "3.1.4")
            outer.write_bytes(b"manually replaced manager")
            self.assertEqual(backend.current_version(), "development")

    def test_manager_binary_is_a_persistent_shortcut_command(self):
        prefix = str(self.home / ".wine4office")
        wine = str(self.home / "runner/bin/wine")
        arguments = [
            "Wine4OfficeManager", "--prefix", prefix, "--wine", wine,
            "word", str(self.home / "document.docx"),
        ]
        with mock.patch.object(manager.sys, "argv", arguments), \
             mock.patch.object(backend, "launch_app", return_value=1234) as launch:
            self.assertEqual(manager.main(), 0)
        launch.assert_called_once_with(
            prefix, wine, "word", manager.FONT_HELPER, [str(self.home / "document.docx")],
            use_x11=True,
        )

    def _wait_for(self, predicate):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.01)
        self.fail("background operation did not finish")


if __name__ == "__main__":
    unittest.main()
