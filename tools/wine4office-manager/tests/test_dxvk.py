#!/usr/bin/env python3
"""DXVK default backend: config, prefix transactions, probe, and launch paths."""

import fcntl
import hashlib
import itertools
import json
import os
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

MANAGER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MANAGER_DIR))
import wine4office_backend as backend  # noqa: E402
import wine4office_manager as manager  # noqa: E402
import wine4office_post_install as post_install  # noqa: E402

DXVK_NAMES = ("dxgi", "d3d11", "d3d10core")
MAIN_KEY = backend.DXVK_DLL_OVERRIDES_KEY
WEBVIEW_KEY = backend.dxvk_app_defaults_key("msedgewebview2.exe")


def wine_dll(label):
    """Return PE-like bytes with Wine's builtin signature at offset 0x40."""
    return b"MZ" + b"\0" * 0x3E + b"Wine builtin DLL\0" + label.encode() + b"\n"


class FakeRegistry:
    """Simulate `wine reg query/import` for _run_cancellable_command.

    Imports are applied value by value, so `fail_on` can fail one in the
    middle, like a real `reg import` that stops at a bad line. `writes()`
    lists each imported value change as an add/delete pseudo-command.
    """

    def __init__(self, values=None):
        self.values = {key: dict(entries) for key, entries in (values or {}).items()}
        self.commands = []
        self.environments = []
        self.changes = []
        self.fail_on = None
        self.prefix = None

    def run(self, command, env, *, cancel_event=None, process_callback=None,
            timeout=30, check=True):
        self.commands.append(list(command))
        self.environments.append(dict(env))
        operation, key = command[2], command[3]
        if operation == "import":
            return self._import(command)
        if self.fail_on and self.fail_on(command):
            raise subprocess.CalledProcessError(1, command)
        entries = self.values.setdefault(key, {})
        if operation == "query":
            if not entries:
                return subprocess.CompletedProcess(command, 1, "", "not found")
            lines = [key.replace("HKCU", "HKEY_CURRENT_USER")]
            lines.extend(f"    {name}    REG_SZ    {data}" for name, data in entries.items())
            return subprocess.CompletedProcess(command, 0, "\n".join(lines) + "\n", "")
        raise AssertionError(f"unexpected registry command: {command}")

    def _import(self, command):
        windows_path = command[3]
        self.assertion_prefix = windows_path
        relative = windows_path.split("\\", 1)[1].replace("\\", "/")
        data = (self.prefix / "drive_c" / relative).read_bytes()
        assert data.startswith(b"\xff\xfe"), "reg files are UTF-16LE with BOM"
        text = data[2:].decode("utf-16-le")
        lines = text.split("\r\n")
        assert lines[0] == "Windows Registry Editor Version 5.00"
        key = None
        for line in lines[1:]:
            if line.startswith("["):
                key = line[1:-1].replace("HKEY_CURRENT_USER\\", "HKCU\\", 1)
                continue
            if not line:
                continue
            name, _separator, value = line.partition("=")
            name = name[1:-1].replace('\\"', '"').replace("\\\\", "\\")
            if value == "-":
                pseudo = ["wine", "reg", "delete", key, "/v", name, "/f"]
            else:
                value = value[1:-1].replace('\\"', '"').replace("\\\\", "\\")
                pseudo = ["wine", "reg", "add", key, "/v", name, "/t", "REG_SZ",
                          "/d", value, "/f"]
            if self.fail_on and self.fail_on(pseudo):
                raise subprocess.CalledProcessError(1, command)
            self.changes.append(pseudo)
            entries = self.values.setdefault(key, {})
            if pseudo[2] == "delete":
                entries.pop(name, None)
            else:
                entries[name] = value
        return subprocess.CompletedProcess(command, 0, "", "")

    def reset_log(self):
        self.commands.clear()
        self.environments.clear()
        self.changes.clear()

    def imports(self):
        return [command for command in self.commands if command[2] == "import"]

    def writes(self):
        return list(self.changes)


class DxvkTestCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.home.mkdir()
        self.environment = mock.patch.dict(os.environ, {
            "HOME": str(self.home),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
            "XDG_CACHE_HOME": str(self.home / ".cache"),
        })
        self.environment.start()
        os.environ.pop("WINEDLLOVERRIDES", None)
        self.runner = self.root / "runner"
        (self.runner / "bin").mkdir(parents=True)
        self.wine = self.runner / "bin/wine"
        self.wine.write_text("#!/bin/sh\nexit 0\n")
        self.wine.chmod(0o755)
        self.bundle_files = self._make_bundle()
        self.builtin_files = self._make_builtins()
        self.prefix = self._make_prefix(self.home / ".wine4office")

    def tearDown(self):
        self.environment.stop()
        self.temp.cleanup()

    def _make_bundle(self):
        root = self.runner / "share/wine4office/dxvk" / backend.DXVK_VERSION
        files = {}
        for architecture in ("x64", "x32"):
            (root / architecture).mkdir(parents=True)
            for dll in DXVK_NAMES:
                data = f"MZ dxvk {architecture} {dll}\n".encode()
                (root / architecture / f"{dll}.dll").write_bytes(data)
                files[f"{architecture}/{dll}.dll"] = hashlib.sha256(data).hexdigest()
        (root / "manifest.json").write_text(json.dumps({
            "schema": 1, "name": "DXVK", "version": backend.DXVK_VERSION,
            "files": files,
        }))
        return files

    def _make_builtins(self):
        files = {}
        for directory in ("x86_64-windows", "i386-windows"):
            path = self.runner / "lib/wine" / directory
            path.mkdir(parents=True)
            for dll in DXVK_NAMES:
                data = wine_dll(f"runner {directory} {dll}")
                (path / f"{dll}.dll").write_bytes(data)
                files[f"{directory}/{dll}.dll"] = data
        return files

    def _make_prefix(self, path, managed=True):
        path.mkdir(parents=True)
        (path / "system.reg").write_text("WINE REGISTRY Version 2\n")
        (path / "user.reg").write_text("WINE REGISTRY Version 2\n")
        (path / "dosdevices").mkdir()
        for system in ("system32", "syswow64"):
            directory = path / "drive_c/windows" / system
            directory.mkdir(parents=True)
            for dll in DXVK_NAMES:
                (directory / f"{dll}.dll").write_bytes(wine_dll(f"prefix {system} {dll}"))
        if managed:
            marker = path / backend.PREFIX_MARKER_NAME
            marker.write_bytes(backend.PREFIX_MARKER_CONTENT)
            marker.chmod(0o600)
        return path

    def _apply(self, registry, enabled, **kwargs):
        # Wine is idle before apply, so user.reg holds the saved values; after
        # apply the (simulated) wineserver saves the imported values again.
        registry.prefix = self.prefix
        self._write_user_reg(registry)
        try:
            return self._apply_without_save(registry, enabled, **kwargs)
        finally:
            self._write_user_reg(registry)

    def _apply_without_save(self, registry, enabled, **kwargs):
        with mock.patch.object(backend, "_run_cancellable_command", side_effect=registry.run), \
             mock.patch.object(backend, "wine_running_in_prefix", return_value=False), \
             mock.patch.object(backend, "stop_wine", side_effect=AssertionError("kill")), \
             mock.patch.object(backend, "finalize_new_prefix",
                               side_effect=AssertionError("kill")), \
             mock.patch.object(backend, "_wait_for_registry_save",
                               return_value=True) as flush:
            result = backend.apply_dxvk_state(self.prefix, self.wine, enabled, **kwargs)
        return result, flush

    def _system_file(self, system, dll):
        return self.prefix / "drive_c/windows" / system / f"{dll}.dll"

    def _write_user_reg(self, registry):
        lines = ["WINE REGISTRY Version 2", ""]
        for key, entries in registry.values.items():
            relative = key.split("\\", 1)[1].replace("\\", "\\\\")
            lines.append(f"[{relative}] 1700000000")
            lines.append("#time=1d9")
            lines.extend(f'"{name}"="{data}"' for name, data in entries.items())
            lines.append("")
        # Like wineserver: write a new file and rename it over user.reg.
        temporary = self.prefix / "user.reg.tmp"
        temporary.write_text("\n".join(lines))
        os.replace(temporary, self.prefix / "user.reg")


class DxvkConfigTests(DxvkTestCase):
    def _save_raw(self, payload):
        path = backend.config_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload))

    def test_default_config_selects_dxvk(self):
        config = backend.default_config()
        self.assertTrue(config["use_dxvk"])
        self.assertTrue(config["graphics_active_use_dxvk"])
        self.assertFalse(config["use_vulkan"])
        self.assertTrue(backend.active_use_dxvk(config))

    def test_saved_wined3d_vulkan_choice_keeps_wined3d(self):
        self._save_raw({"use_x11": False, "use_vulkan": True})
        config = backend.load_config()
        self.assertFalse(config["use_dxvk"])
        self.assertFalse(config["graphics_active_use_dxvk"])
        self.assertTrue(config["use_vulkan"])

    def test_saved_opengl_config_moves_to_dxvk(self):
        self._save_raw({"use_x11": True, "use_vulkan": False})
        config = backend.load_config()
        self.assertTrue(config["use_dxvk"])
        self.assertTrue(config["graphics_active_use_dxvk"])

    def test_explicit_dxvk_choice_is_preserved(self):
        self._save_raw({"use_vulkan": False, "use_dxvk": False,
                        "graphics_active_use_dxvk": False})
        self.assertFalse(backend.load_config()["use_dxvk"])
        self._save_raw({"use_vulkan": True, "use_dxvk": True})
        self.assertTrue(backend.load_config()["use_dxvk"])

    def test_pending_restart_migrates_active_from_active_vulkan(self):
        self._save_raw({
            "use_vulkan": True, "graphics_restart_required": True,
            "graphics_active_use_vulkan": False,
        })
        config = backend.load_config()
        self.assertFalse(config["use_dxvk"])
        self.assertTrue(config["graphics_active_use_dxvk"])
        self.assertTrue(backend.active_use_dxvk(config))

    def test_round_trip_saves_dxvk_keys(self):
        config = backend.default_config()
        config["use_dxvk"] = False
        config["graphics_active_use_dxvk"] = False
        backend.save_config(config)
        saved = json.loads(backend.config_path().read_text())
        self.assertIs(saved["use_dxvk"], False)
        self.assertIs(saved["graphics_active_use_dxvk"], False)

    def test_active_graphics_settings_keeps_two_values(self):
        self.assertEqual(len(backend.active_graphics_settings(backend.default_config())), 2)

    def test_dxvk_choice_change_requires_restart(self):
        state = manager.ManagerState()
        state.config.update({"use_dxvk": True, "graphics_active_use_dxvk": True,
                             "use_vulkan": False, "graphics_restart_required": False})
        updated = state.update_graphics_settings(True, False, False)
        self.assertFalse(updated["use_dxvk"])
        self.assertTrue(updated["graphics_active_use_dxvk"])
        self.assertTrue(updated["graphics_restart_required"])
        restored = state.update_graphics_settings(True, False, True)
        self.assertFalse(restored["graphics_restart_required"])


class DxvkPrefixTransactionTests(DxvkTestCase):
    def test_enable_installs_files_and_registry_with_webview2_exclusion(self):
        registry = FakeRegistry()
        result, flush = self._apply(registry, True)

        self.assertTrue(result["enabled"])
        self.assertTrue(result["changed"])
        for architecture, system in (("x64", "system32"), ("x32", "syswow64")):
            for dll in DXVK_NAMES:
                self.assertEqual(
                    hashlib.sha256(self._system_file(system, dll).read_bytes()).hexdigest(),
                    self.bundle_files[f"{architecture}/{dll}.dll"],
                )
        self.assertEqual(registry.values[MAIN_KEY], {dll: "native" for dll in DXVK_NAMES})
        self.assertEqual(registry.values[WEBVIEW_KEY], {dll: "builtin" for dll in DXVK_NAMES})
        # One `wine reg import`, and no `wine reg query` while Wine was idle.
        self.assertEqual(len(registry.imports()), 1)
        self.assertEqual([c for c in registry.commands if c[2] != "import"], [])
        self.assertEqual(list((self.prefix / "drive_c/windows/temp").glob("*.reg")), [])
        adds = [command for command in registry.writes() if command[2] == "add"]
        self.assertEqual(len(adds), 6)
        for command in adds:
            self.assertEqual(command[command.index("/t") + 1], "REG_SZ")
            self.assertEqual(command[-1], "/f")
        for env in registry.environments:
            self.assertFalse(
                set(env.get("WINEDLLOVERRIDES", "").replace("=", ";").split(";"))
                & set(DXVK_NAMES)
            )
        state = backend.read_dxvk_prefix_state(self.prefix)
        self.assertTrue(state["enabled"])
        self.assertEqual(state["version"], backend.DXVK_VERSION)
        self.assertEqual(len(state["owned_values"]), 6)
        self.assertEqual(stat.S_IMODE(backend.dxvk_state_path(self.prefix).stat().st_mode), 0o600)
        flush.assert_called_once()
        self.assertEqual(list(self.prefix.rglob("*.wine4office-tmp")), [])

    def test_enable_is_idempotent(self):
        registry = FakeRegistry()
        self._apply(registry, True)
        registry.reset_log()
        result, flush = self._apply(registry, True)
        self.assertEqual(registry.writes(), [])
        self.assertEqual(registry.imports(), [])
        self.assertTrue(result["enabled"])
        self.assertFalse(result["changed"])
        flush.assert_not_called()

    def test_disable_restores_builtins_and_deletes_only_owned_values(self):
        registry = FakeRegistry({WEBVIEW_KEY: {"dxgi": "b"}})
        self._apply(registry, True)
        # The user's own (compatible) WebView2 dxgi value is neither replaced nor owned.
        self.assertEqual(registry.values[WEBVIEW_KEY]["dxgi"], "b")
        registry.values[MAIN_KEY]["d3d11"] = "native,builtin"  # user edited ours
        registry.reset_log()

        result, flush = self._apply(registry, False)

        self.assertFalse(result["enabled"])
        deleted = {(command[3], command[5]) for command in registry.writes()}
        self.assertEqual(deleted, {
            (MAIN_KEY, "dxgi"), (MAIN_KEY, "d3d10core"),
            (WEBVIEW_KEY, "d3d11"), (WEBVIEW_KEY, "d3d10core"),
        })
        self.assertEqual(registry.values[MAIN_KEY], {"d3d11": "native,builtin"})
        self.assertEqual(registry.values[WEBVIEW_KEY], {"dxgi": "b"})
        self.assertEqual(len(registry.imports()), 1)
        for directory, system in (("x86_64-windows", "system32"), ("i386-windows", "syswow64")):
            for dll in DXVK_NAMES:
                self.assertEqual(
                    self._system_file(system, dll).read_bytes(),
                    self.builtin_files[f"{directory}/{dll}.dll"],
                )
        self.assertIsNone(backend.read_dxvk_prefix_state(self.prefix))
        flush.assert_called_once()

    def test_registry_save_is_awaited_without_closing_wine(self):
        registry = FakeRegistry()
        running = itertools.chain([False, False], itertools.repeat(True))
        registry.prefix = self.prefix
        with mock.patch.object(backend, "_run_cancellable_command", side_effect=registry.run), \
             mock.patch.object(backend, "wine_running_in_prefix",
                               side_effect=lambda *_a: next(running)), \
             mock.patch.object(backend, "DXVK_REGISTRY_SAVE_WAIT_SECONDS", 0.3), \
             mock.patch.object(backend, "stop_wine", side_effect=AssertionError("kill")), \
             mock.patch.object(backend, "finalize_new_prefix",
                               side_effect=AssertionError("kill")), \
             mock.patch.object(backend.subprocess, "run",
                               side_effect=AssertionError("wineserver")):
            result = backend.apply_dxvk_state(self.prefix, self.wine, True)
        self.assertTrue(result["enabled"])
        self.assertTrue(result["registry_pending"])
        self.assertFalse(backend.read_dxvk_prefix_state(self.prefix).get("pending"))

    def test_wine_started_without_the_lock_postpones_the_registry_change(self):
        registry = FakeRegistry()
        registry.prefix = self.prefix
        self._write_user_reg(registry)
        before = {
            path: path.read_bytes()
            for path in (self.prefix / "drive_c/windows").rglob("*.dll")
        }
        running = iter([False, True])
        with mock.patch.object(backend, "_run_cancellable_command", side_effect=registry.run), \
             mock.patch.object(backend, "wine_running_in_prefix",
                               side_effect=lambda *_a: next(running)):
            with self.assertRaises(backend.DxvkBusyError):
                backend.apply_dxvk_state(self.prefix, self.wine, True)
        self.assertEqual(registry.commands, [])
        self.assertEqual({path: path.read_bytes() for path in before}, before)
        self.assertIsNone(backend.read_dxvk_prefix_state(self.prefix))

    def test_launch_waits_longer_than_apply_can_hold_the_lock(self):
        worst_apply = (backend.DXVK_APPLY_LOCK_TIMEOUT_SECONDS
                       + 2 * backend.DXVK_REG_IMPORT_TIMEOUT_SECONDS)
        self.assertGreater(backend.DXVK_LAUNCH_LOCK_TIMEOUT_SECONDS, worst_apply)

    def test_registry_save_wait_has_no_kill_fallback(self):
        source = (MANAGER_DIR / "wine4office_backend.py").read_text()
        start = source.index("def _wait_for_registry_save(")
        end = source.index("def converge_dxvk_state(")
        body = source[start:end]
        for forbidden in ("stop_wine(", "finalize_new_prefix(", '"-k"', '"-w"',
                          "--kill", "SIGKILL", "os.kill("):
            self.assertNotIn(forbidden, body)

    def test_apply_waits_for_exclusive_prefix_lock(self):
        holder = subprocess.Popen([
            sys.executable, "-c",
            "import fcntl, os, sys, time\n"
            "fd = os.open(sys.argv[1], os.O_RDWR | os.O_CREAT, 0o600)\n"
            "fcntl.flock(fd, fcntl.LOCK_SH)\n"
            "print('locked', flush=True)\n"
            "time.sleep(30)\n",
            str(self.prefix / backend.DXVK_LOCK_NAME),
        ], stdout=subprocess.PIPE, text=True)
        registry = FakeRegistry()
        try:
            self.assertEqual(holder.stdout.readline().strip(), "locked")
            with mock.patch.object(backend, "DXVK_APPLY_LOCK_TIMEOUT_SECONDS", 0.2), \
                 mock.patch.object(backend, "_run_cancellable_command",
                                   side_effect=registry.run), \
                 mock.patch.object(backend, "wine_running_in_prefix", return_value=False):
                with self.assertRaises(backend.DxvkBusyError):
                    backend.apply_dxvk_state(self.prefix, self.wine, True)
                result = backend.converge_dxvk_state(self.prefix, self.wine, True)
        finally:
            holder.kill()
            holder.wait()
            holder.stdout.close()
        self.assertTrue(result["deferred"])
        self.assertEqual(registry.commands, [])
        with backend.dxvk_prefix_lock(self.prefix) as acquired:
            self.assertTrue(acquired)

    def test_pending_ownership_record_is_written_before_changes(self):
        seen = []
        real_install = backend._install_verified_file

        def install(source, target, expected):
            seen.append(backend.read_dxvk_prefix_state(self.prefix))
            return real_install(source, target, expected)

        with mock.patch.object(backend, "_install_verified_file", side_effect=install):
            self._apply(FakeRegistry(), True)
        self.assertTrue(seen[0]["pending"])
        self.assertEqual(len(seen[0]["owned_values"]), 6)
        self.assertEqual(len(seen[0]["files"]), 6)
        state = backend.read_dxvk_prefix_state(self.prefix)
        self.assertFalse(state.get("pending"))
        state["pending"] = True
        backend._write_dxvk_prefix_state(self.prefix, state)
        self.assertFalse(backend.dxvk_prefix_converged(self.prefix, self.wine, True))
        self.assertFalse(backend.dxvk_prefix_converged(self.prefix, self.wine, False))

    def test_user_native_dll_files_are_never_overwritten(self):
        user_dll = self._system_file("system32", "d3d11")
        user_dll.write_bytes(b"MZ user's own d3d11 build\n")
        registry = FakeRegistry()
        result, _flush = self._apply(registry, True)
        self.assertFalse(result["enabled"])
        self.assertIn("not Wine's builtin DLL", result["reason"])
        self.assertEqual(user_dll.read_bytes(), b"MZ user's own d3d11 build\n")
        self.assertEqual(registry.writes(), [])
        self.assertEqual(
            self._system_file("system32", "dxgi").read_bytes(),
            wine_dll("prefix system32 dxgi"),
        )
        self._write_user_reg(registry)
        self.assertTrue(backend.dxvk_prefix_converged(self.prefix, self.wine, True))

    def test_runner_builtin_copies_without_signature_are_replaced(self):
        target = self._system_file("syswow64", "dxgi")
        target.write_bytes(self.builtin_files["i386-windows/dxgi.dll"])
        result, _flush = self._apply(FakeRegistry(), True)
        self.assertTrue(result["enabled"])

    def test_disable_without_runner_builtins_removes_manager_files(self):
        registry = FakeRegistry()
        self._apply(registry, True)
        for directory in ("x86_64-windows", "i386-windows"):
            for dll in DXVK_NAMES:
                (self.runner / "lib/wine" / directory / f"{dll}.dll").unlink()
        result, _flush = self._apply(registry, False)
        self.assertFalse(result["enabled"])
        for system in ("system32", "syswow64"):
            for dll in DXVK_NAMES:
                self.assertFalse(self._system_file(system, dll).exists())
        self.assertEqual(registry.values[MAIN_KEY], {})
        self.assertIsNone(backend.read_dxvk_prefix_state(self.prefix))

    def test_compatible_user_edit_of_owned_exclusion_is_kept_and_disowned(self):
        registry = FakeRegistry()
        self._apply(registry, True)
        registry.values[WEBVIEW_KEY]["dxgi"] = "b"
        registry.reset_log()
        self._apply(registry, True)
        self.assertEqual(registry.writes(), [])
        self.assertNotIn([WEBVIEW_KEY, "dxgi", "builtin"],
                         backend.read_dxvk_prefix_state(self.prefix)["owned_values"])
        self._apply(registry, False)
        self.assertEqual(registry.values[WEBVIEW_KEY], {"dxgi": "b"})

    def test_native_webview2_override_is_a_conflict(self):
        for value in ("native", "n,b", ""):
            with self.subTest(value=value):
                registry = FakeRegistry({WEBVIEW_KEY: {"d3d11": value}})
                (self.prefix / backend.DXVK_STATE_NAME).unlink(missing_ok=True)
                result, _flush = self._apply(registry, True)
                self.assertFalse(result["enabled"])
                self.assertIn("msedgewebview2.exe", result["reason"])
                self.assertEqual(registry.writes(), [])
                self.assertEqual(registry.values[WEBVIEW_KEY], {"d3d11": value})
                self.assertTrue(backend.dxvk_prefix_converged(self.prefix, self.wine, True))

    def test_user_switching_owned_exclusion_to_native_disables_dxvk(self):
        registry = FakeRegistry()
        self._apply(registry, True)
        registry.values[WEBVIEW_KEY]["dxgi"] = "native"
        self._write_user_reg(registry)
        self.assertFalse(backend.dxvk_prefix_converged(self.prefix, self.wine, True))
        registry.reset_log()
        result, _flush = self._apply(registry, True)
        self.assertFalse(result["enabled"])
        self.assertEqual(registry.values[WEBVIEW_KEY], {"dxgi": "native"})
        self.assertEqual(registry.values[MAIN_KEY], {})
        self.assertEqual(
            self._system_file("system32", "d3d11").read_bytes(),
            self.builtin_files["x86_64-windows/d3d11.dll"],
        )
        self.assertFalse(backend.read_dxvk_prefix_state(self.prefix)["enabled"])

    def test_disable_keeps_files_the_user_replaced(self):
        registry = FakeRegistry()
        self._apply(registry, True)
        replaced = self._system_file("system32", "dxgi")
        replaced.write_bytes(b"MZ user dxgi\n")
        self._apply(registry, False)
        self.assertEqual(replaced.read_bytes(), b"MZ user dxgi\n")
        self.assertEqual(
            self._system_file("system32", "d3d11").read_bytes(),
            self.builtin_files["x86_64-windows/d3d11.dll"],
        )

    def test_disable_without_ownership_record_changes_nothing(self):
        registry = FakeRegistry({MAIN_KEY: {"dxgi": "native"}})
        before = self._system_file("system32", "dxgi").read_bytes()
        result, _flush = self._apply(registry, False)
        self.assertFalse(result["changed"])
        self.assertEqual(registry.commands, [])
        self.assertEqual(registry.values[MAIN_KEY], {"dxgi": "native"})
        self.assertEqual(self._system_file("system32", "dxgi").read_bytes(), before)

    def test_user_builtin_override_conflict_keeps_wined3d(self):
        registry = FakeRegistry({MAIN_KEY: {"dxgi": "builtin"}})
        before = self._system_file("system32", "d3d11").read_bytes()
        result, _flush = self._apply(registry, True)
        self.assertFalse(result["enabled"])
        self.assertIn("dxgi=builtin", result["reason"])
        self.assertEqual(registry.writes(), [])
        self.assertEqual(registry.values[MAIN_KEY], {"dxgi": "builtin"})
        self.assertEqual(self._system_file("system32", "d3d11").read_bytes(), before)
        state = backend.read_dxvk_prefix_state(self.prefix)
        self.assertFalse(state["enabled"])
        self.assertIn("dxgi=builtin", state["reason"])

    def test_user_native_override_is_compatible_and_not_owned(self):
        registry = FakeRegistry({MAIN_KEY: {"dxgi": "native"}})
        self._apply(registry, True)
        owned = backend.read_dxvk_prefix_state(self.prefix)["owned_values"]
        self.assertNotIn([MAIN_KEY, "dxgi", "native"], owned)
        self._apply(registry, False)
        self.assertEqual(registry.values[MAIN_KEY], {"dxgi": "native"})

    def test_sha256_mismatch_is_rejected_and_rolled_back(self):
        tampered = (self.runner / "share/wine4office/dxvk" / backend.DXVK_VERSION
                    / "x32/d3d11.dll")
        tampered.write_bytes(b"MZ tampered\n")
        before = {
            path: path.read_bytes()
            for path in (self.prefix / "drive_c/windows").rglob("*.dll")
        }
        registry = FakeRegistry()
        with self.assertRaisesRegex(RuntimeError, "SHA-256 verification.*restored"):
            self._apply(registry, True)
        self.assertEqual(
            {path: path.read_bytes() for path in before}, before,
        )
        self.assertEqual(registry.writes(), [])
        self.assertIsNone(backend.read_dxvk_prefix_state(self.prefix))
        self.assertEqual(list(self.prefix.rglob("*.wine4office-tmp")), [])

    def test_registry_failure_rolls_back_files_and_values(self):
        before = {
            path: path.read_bytes()
            for path in (self.prefix / "drive_c/windows").rglob("*.dll")
        }
        registry = FakeRegistry()
        registry.fail_on = lambda command: (
            command[2] == "add" and command[3] == WEBVIEW_KEY and command[5] == "d3d10core"
        )
        # The first import fails part-way; the rollback import must undo the
        # values it already wrote.
        with self.assertRaisesRegex(RuntimeError, "previous Direct3D setup was restored"):
            self._apply(registry, True)
        self.assertEqual({path: path.read_bytes() for path in before}, before)
        self.assertEqual(registry.values.get(MAIN_KEY, {}), {})
        self.assertEqual(registry.values.get(WEBVIEW_KEY, {}), {})
        self.assertIsNone(backend.read_dxvk_prefix_state(self.prefix))

    def test_atomic_install_keeps_target_when_replace_fails(self):
        target = self._system_file("system32", "dxgi")
        original = target.read_bytes()
        source = (self.runner / "share/wine4office/dxvk" / backend.DXVK_VERSION
                  / "x64/dxgi.dll")
        with mock.patch.object(backend.os, "replace", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                backend._install_verified_file(
                    source, target, self.bundle_files["x64/dxgi.dll"]
                )
        self.assertEqual(target.read_bytes(), original)
        self.assertEqual(list(target.parent.glob(".*wine4office-tmp")), [])

    def test_no_change_while_wine_is_running(self):
        registry = FakeRegistry()
        before = self._system_file("system32", "dxgi").read_bytes()
        with mock.patch.object(backend, "_run_cancellable_command",
                               side_effect=registry.run), \
             mock.patch.object(backend, "wine_running_in_prefix", return_value=True):
            with self.assertRaises(backend.DxvkBusyError):
                backend.apply_dxvk_state(self.prefix, self.wine, True)
            result = backend.converge_dxvk_state(self.prefix, self.wine, True)
        self.assertTrue(result["deferred"])
        self.assertFalse(result["changed"])
        self.assertEqual(registry.commands, [])
        self.assertEqual(self._system_file("system32", "dxgi").read_bytes(), before)

    def test_unmanaged_prefix_is_never_changed(self):
        other = self._make_prefix(self.home / "unmanaged", managed=False)
        with self.assertRaisesRegex(ValueError, "Wine4Office prefixes"):
            backend.apply_dxvk_state(other, self.wine, True)
        result = backend.converge_dxvk_state(other, self.wine, True)
        self.assertFalse(result["changed"])

    def test_missing_bundle_cannot_enable(self):
        with self.assertRaisesRegex(FileNotFoundError, "does not include DXVK"):
            with mock.patch.object(backend, "wine_running_in_prefix", return_value=False):
                backend.apply_dxvk_state(
                    self.prefix, self._runner_without_bundle(), True
                )

    def _runner_without_bundle(self):
        runner = self.root / "plain-runner/bin"
        runner.mkdir(parents=True)
        wine = runner / "wine"
        wine.write_text("#!/bin/sh\n")
        wine.chmod(0o755)
        return wine

    def test_bundle_manifest_must_list_expected_files(self):
        manifest = (self.runner / "share/wine4office/dxvk" / backend.DXVK_VERSION
                    / "manifest.json")
        payload = json.loads(manifest.read_text())
        payload["files"].pop("x32/dxgi.dll")
        manifest.write_text(json.dumps(payload))
        with self.assertRaisesRegex(ValueError, "unexpected files"):
            backend.load_dxvk_bundle(self.wine)
        payload["files"]["x32/dxgi.dll"] = "0" * 64
        payload["version"] = "9.9"
        manifest.write_text(json.dumps(payload))
        with self.assertRaisesRegex(ValueError, "invalid"):
            backend.load_dxvk_bundle(self.wine)
        self.assertIsNone(backend.load_dxvk_bundle(self._runner_without_bundle()))

    def test_converged_reads_user_reg_and_files(self):
        registry = FakeRegistry()
        self.assertTrue(backend.dxvk_prefix_converged(self.prefix, self.wine, False))
        self.assertFalse(backend.dxvk_prefix_converged(self.prefix, self.wine, True))
        self._apply(registry, True)
        self._write_user_reg(registry)
        self.assertTrue(backend.dxvk_prefix_converged(self.prefix, self.wine, True))
        self.assertFalse(backend.dxvk_prefix_converged(self.prefix, self.wine, False))
        # A runner update with different DLLs is detected without starting Wine.
        bundle = self.runner / "share/wine4office/dxvk" / backend.DXVK_VERSION
        (bundle / "x64/dxgi.dll").write_bytes(b"MZ newer\n")
        manifest = json.loads((bundle / "manifest.json").read_text())
        manifest["files"]["x64/dxgi.dll"] = hashlib.sha256(b"MZ newer\n").hexdigest()
        (bundle / "manifest.json").write_text(json.dumps(manifest))
        self.assertFalse(backend.dxvk_prefix_converged(self.prefix, self.wine, True))

    def test_converge_reinstalls_after_registry_values_disappear(self):
        registry = FakeRegistry()
        self._apply(registry, True)
        self._write_user_reg(registry)
        registry.values[MAIN_KEY].pop("dxgi")
        self._write_user_reg(registry)
        with mock.patch.object(backend, "_run_cancellable_command",
                               side_effect=registry.run), \
             mock.patch.object(backend, "wine_running_in_prefix", return_value=False), \
             mock.patch.object(backend, "_wait_for_registry_save", return_value=True):
            result = backend.converge_dxvk_state(self.prefix, self.wine, True)
        self.assertTrue(result["changed"])
        self.assertEqual(registry.values[MAIN_KEY]["dxgi"], "native")

    def test_converge_skips_unknown_support(self):
        with mock.patch.object(backend, "apply_dxvk_state") as apply:
            result = backend.converge_dxvk_state(self.prefix, self.wine, None)
        apply.assert_not_called()
        self.assertFalse(result["changed"])

    def test_wine_process_detection_uses_wineprefix_and_wine_executables(self):
        proc = self.root / "proc"

        def process(pid, executable, prefix):
            path = proc / str(pid)
            path.mkdir(parents=True)
            (path / "exe").symlink_to(executable)
            (path / "environ").write_bytes(
                b"HOME=/x\0WINEPREFIX=" + os.fsencode(str(prefix)) + b"\0"
            )
            (path / "stat").write_text(f"{pid} (x) S " + " ".join(["0"] * 30))

        process(100, "/usr/bin/python3", self.prefix)
        process(101, "/opt/other-runner/bin/wine64-preloader", self.home / "elsewhere")
        self.assertFalse(backend.wine_running_in_prefix(self.prefix, self.wine, proc))
        process(102, "/opt/other-runner/bin/wineserver", self.prefix)
        self.assertTrue(backend.wine_running_in_prefix(self.prefix, self.wine, proc))

    def test_wineserver_lock_is_inspected_without_taking_it(self):
        server_root = self.root / "server-root"
        info = os.stat(self.prefix)
        server = (server_root / f".wine-{os.getuid()}"
                  / f"server-{info.st_dev:x}-{info.st_ino:x}")
        server.mkdir(parents=True)
        lock = server / "lock"
        lock.write_text("")
        self.assertFalse(backend._wineserver_lock_held(self.prefix, server_root))
        holder = subprocess.Popen([
            sys.executable, "-c",
            "import fcntl, os, sys, time\n"
            "fd = os.open(sys.argv[1], os.O_WRONLY)\n"
            "fcntl.lockf(fd, fcntl.LOCK_EX)\n"
            "print('locked', flush=True)\n"
            "time.sleep(30)\n",
            str(lock),
        ], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(holder.stdout.readline().strip(), "locked")
            self.assertTrue(backend._wineserver_lock_held(self.prefix, server_root))
        finally:
            holder.kill()
            holder.wait()
            holder.stdout.close()
        self.assertFalse(backend._wineserver_lock_held(self.prefix, server_root))

    def test_create_environment_installs_dxvk_before_finalizing(self):
        calls = []
        with mock.patch.object(backend, "_stream_command"), \
             mock.patch.object(backend, "install_bundled_wine_gecko"), \
             mock.patch.object(backend, "install_bundled_wine_mono"), \
             mock.patch.object(backend, "apply_dxvk_state",
                               side_effect=lambda *a, **k: calls.append(("dxvk", k))), \
             mock.patch.object(backend, "finalize_new_prefix",
                               side_effect=lambda *a, **k: calls.append(("finalize", k))), \
             mock.patch.object(backend, "classify_prefix", side_effect=["missing", "valid"]), \
             mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend, "mark_prefix_owned"):
            backend.create_environment(
                str(self.home / "new-prefix"), str(self.wine), False, lambda _line: None,
                dxvk=True,
            )
        self.assertEqual([name for name, _kwargs in calls], ["dxvk", "finalize"])
        self.assertTrue(calls[0][1]["_manager_create"])
        self.assertFalse(calls[0][1]["wait_for_registry_save"])

    def test_create_environment_dxvk_failure_keeps_wined3d(self):
        lines = []
        with mock.patch.object(backend, "_stream_command"), \
             mock.patch.object(backend, "install_bundled_wine_gecko"), \
             mock.patch.object(backend, "install_bundled_wine_mono"), \
             mock.patch.object(backend, "apply_dxvk_state",
                               side_effect=RuntimeError("Could not enable DXVK")), \
             mock.patch.object(backend, "finalize_new_prefix"), \
             mock.patch.object(backend, "classify_prefix", side_effect=["missing", "valid"]), \
             mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend, "mark_prefix_owned"):
            backend.create_environment(
                str(self.home / "new-prefix"), str(self.wine), False, lines.append,
                dxvk=True,
            )
        self.assertTrue(any("Direct3D uses WineD3D" in line for line in lines))


class DxvkProbeTests(DxvkTestCase):
    def _device(self, **changes):
        device = {
            "name": "Intel Iris Xe", "type": 1, "api_version": "1.4.318",
            "api_supported": True, "missing_extensions": [],
        }
        device.update(changes)
        return device

    def test_device_evaluation(self):
        self.assertTrue(backend.evaluate_dxvk_devices([self._device()])["supported"])
        cpu = backend.evaluate_dxvk_devices([self._device(name="llvmpipe", type=4)])
        self.assertFalse(cpu["supported"])
        self.assertEqual(cpu["code"], "cpu-only")
        old = backend.evaluate_dxvk_devices([
            self._device(api_version="1.2.0", api_supported=False)
        ])
        self.assertEqual(old["code"], "old-api")
        self.assertIn("1.2.0", old["reason"])
        missing = backend.evaluate_dxvk_devices([
            self._device(missing_extensions=["VK_KHR_maintenance6"])
        ])
        self.assertEqual(missing["code"], "missing-extensions")
        self.assertIn("VK_KHR_maintenance6", missing["reason"])
        mixed = backend.evaluate_dxvk_devices([
            self._device(name="llvmpipe", type=4), self._device(name="Radeon"),
        ])
        self.assertTrue(mixed["supported"])
        self.assertEqual(backend.evaluate_dxvk_devices([])["code"], "no-device")

    def test_required_extensions_follow_dxvk_baseline(self):
        self.assertEqual(set(backend.DXVK_REQUIRED_DEVICE_EXTENSIONS), {
            "VK_EXT_depth_clip_enable", "VK_EXT_robustness2", "VK_EXT_transform_feedback",
            "VK_KHR_load_store_op_none", "VK_KHR_maintenance5", "VK_KHR_maintenance6",
            "VK_KHR_swapchain",
        })

    def test_probe_output_parsing(self):
        good = backend._probe_result(True, "supported", "ok", [self._device()])
        parsed = backend.parse_dxvk_probe_output(
            "driver noise\n" + json.dumps(good) + "\n", 0
        )
        self.assertTrue(parsed["supported"])
        for junk in ("", "not json", '{"schema": 99}', '{"schema": 1, "supported": "yes"}'):
            with self.subTest(junk=junk):
                result = backend.parse_dxvk_probe_output(junk, -11)
                self.assertIsNone(result["supported"])
                self.assertEqual(result["code"], "error")

    def test_probe_timeout_and_spawn_failure_are_unknown(self):
        with mock.patch.object(backend.subprocess, "run",
                               side_effect=subprocess.TimeoutExpired(["probe"], 20)):
            result = backend.run_dxvk_probe(timeout=20)
        self.assertIsNone(result["supported"])
        self.assertEqual(result["code"], "timeout")
        with mock.patch.object(backend.subprocess, "run", side_effect=OSError("no exec")):
            self.assertIsNone(backend.run_dxvk_probe()["supported"])

    def test_probe_runs_in_child_process_with_timeout(self):
        completed = subprocess.CompletedProcess(
            [], 0, json.dumps(backend._probe_result(False, "no-loader", "missing")), ""
        )
        with mock.patch.object(backend.subprocess, "run", return_value=completed) as run:
            result = backend.run_dxvk_probe(timeout=7)
        self.assertFalse(result["supported"])
        command = run.call_args.args[0]
        self.assertEqual(command[-1], backend.DXVK_PROBE_FLAG)
        self.assertTrue(command[1].endswith("wine4office_manager.py"))
        self.assertEqual(run.call_args.kwargs["timeout"], 7)
        with mock.patch.object(backend.sys, "frozen", True, create=True):
            self.assertEqual(
                backend.dxvk_probe_command(), [sys.executable, backend.DXVK_PROBE_FLAG]
            )

    def test_probe_cli_always_prints_json(self):
        with mock.patch.object(backend, "probe_vulkan_for_dxvk",
                               side_effect=RuntimeError("driver exploded")), \
             mock.patch.object(backend.sys, "stdout") as stdout:
            self.assertEqual(backend.run_dxvk_probe_cli(), 0)
        printed = json.loads(stdout.write.call_args.args[0])
        self.assertIsNone(printed["supported"])
        self.assertIn("driver exploded", printed["reason"])

    def test_manager_entry_point_dispatches_hidden_probe_flag(self):
        with mock.patch.object(manager.sys, "argv", ["manager", backend.DXVK_PROBE_FLAG]), \
             mock.patch.object(backend, "run_dxvk_probe_cli", return_value=0) as probe:
            self.assertEqual(manager.main(), 0)
        probe.assert_called_once_with()

    def test_support_is_cached_by_fingerprint(self):
        supported = backend._probe_result(True, "supported", "ok", [self._device()])
        with mock.patch.object(backend, "vulkan_icd_fingerprint", return_value="a"), \
             mock.patch.object(backend, "run_dxvk_probe", return_value=supported) as probe:
            self.assertTrue(backend.dxvk_host_support()["supported"])
            self.assertTrue(backend.dxvk_host_support()["supported"])
        self.assertEqual(probe.call_count, 1)
        cache = json.loads(backend.dxvk_support_cache_path().read_text())
        self.assertEqual(cache["fingerprint"], "a")
        with mock.patch.object(backend, "vulkan_icd_fingerprint", return_value="b"), \
             mock.patch.object(backend, "run_dxvk_probe", return_value=supported) as probe:
            backend.dxvk_host_support()
        probe.assert_called_once()

    def test_unknown_support_is_retried_after_retry_window(self):
        unknown = backend._probe_result(None, "timeout", "slow")
        with mock.patch.object(backend, "vulkan_icd_fingerprint", return_value="a"), \
             mock.patch.object(backend, "run_dxvk_probe", return_value=unknown) as probe:
            backend.dxvk_host_support()
            backend.dxvk_host_support()
            self.assertEqual(probe.call_count, 1)
            later = time.time() + backend.DXVK_PROBE_RETRY_SECONDS + 1
            with mock.patch.object(backend.time, "time", return_value=later):
                backend.dxvk_host_support()
        self.assertEqual(probe.call_count, 2)

    def test_fingerprint_tracks_icd_files(self):
        icd = self.home / ".local/share/vulkan/icd.d"
        icd.mkdir(parents=True)
        manifest = icd / "test_icd.json"
        manifest.write_text(json.dumps({"ICD": {"library_path": "/nonexistent/libtest.so"}}))
        first = backend.vulkan_icd_fingerprint()
        self.assertEqual(first, backend.vulkan_icd_fingerprint())
        manifest.write_text(json.dumps({"ICD": {"library_path": "/nonexistent/libother.so"}}))
        os.utime(manifest, ns=(1, 1))
        self.assertNotEqual(first, backend.vulkan_icd_fingerprint())

    def test_requested_backend_falls_back_with_reason(self):
        config = backend.default_config()
        config["wine"] = str(self.wine)
        unsupported = backend._probe_result(False, "old-api", "GPU is too old.")
        self.assertEqual(
            backend.dxvk_requested(config, self.wine, support=unsupported),
            (False, "GPU is too old."),
        )
        unknown = backend._probe_result(None, "timeout", "slow")
        self.assertEqual(backend.dxvk_requested(config, self.wine, support=unknown)[0], None)
        supported = backend._probe_result(True, "supported", "ok")
        self.assertEqual(backend.dxvk_requested(config, self.wine, support=supported),
                         (True, ""))
        config["use_dxvk"] = False
        self.assertFalse(backend.dxvk_requested(config, self.wine, support=supported)[0])
        config["use_dxvk"] = True
        requested, reason = backend.dxvk_requested(
            config, self.root / "missing-runner/bin/wine", support=supported
        )
        self.assertFalse(requested)
        self.assertIn("does not include DXVK", reason)

    def test_transient_negative_results_expire(self):
        for code, transient in (("cpu-only", True), ("no-instance", True),
                                ("no-device", True), ("old-api", False),
                                ("missing-extensions", False)):
            with self.subTest(code=code):
                result = backend._probe_result(False, code, code)
                with mock.patch.object(backend, "vulkan_icd_fingerprint", return_value="a"), \
                     mock.patch.object(backend, "run_dxvk_probe", return_value=result) as probe:
                    backend.dxvk_host_support(refresh=True)
                    later = time.time() + backend.DXVK_PROBE_RETRY_SECONDS + 1
                    with mock.patch.object(backend.time, "time", return_value=later):
                        backend.dxvk_host_support()
                self.assertEqual(probe.call_count, 2 if transient else 1)

    def test_fingerprint_includes_boot_id_and_drm_drivers(self):
        source = (MANAGER_DIR / "wine4office_backend.py").read_text()
        start = source.index("def vulkan_icd_fingerprint(")
        body = source[start:source.index("def dxvk_support_cache_path(")]
        self.assertIn("/proc/sys/kernel/random/boot_id", body)
        self.assertIn('"device/driver"', body)

    def test_status_checks_files_and_registry_not_only_the_record(self):
        config = backend.default_config()
        config.update({"prefix": str(self.prefix), "wine": str(self.wine)})
        supported = backend._probe_result(True, "supported", "")
        registry = FakeRegistry()
        self._apply(registry, True)
        self.assertTrue(backend.direct3d_status(config, supported)["dxvk_active"])
        registry.values[MAIN_KEY]["d3d11"] = "builtin"
        self._write_user_reg(registry)
        status = backend.direct3d_status(config, supported)
        self.assertFalse(status["dxvk_active"])
        self.assertIn("not fully set up", status["dxvk_reason"])
        registry.values[MAIN_KEY]["d3d11"] = "native"
        self._write_user_reg(registry)
        self.assertTrue(backend.direct3d_status(config, supported)["dxvk_active"])
        self._system_file("syswow64", "dxgi").write_bytes(wine_dll("restored by someone"))
        self.assertFalse(backend.direct3d_status(config, supported)["dxvk_active"])

    def test_status_reports_office_app_overrides(self):
        config = backend.default_config()
        config.update({"prefix": str(self.prefix), "wine": str(self.wine)})
        registry = FakeRegistry()
        self._apply(registry, True)
        registry.values[backend.dxvk_app_defaults_key("WINWORD.EXE")] = {"d3d11": "builtin"}
        self._write_user_reg(registry)
        status = backend.direct3d_status(config, backend._probe_result(True, "supported", ""))
        self.assertFalse(status["dxvk_active"])
        self.assertIn("WINWORD.EXE", status["dxvk_reason"])

    def test_status_never_claims_dxvk_without_prefix_state(self):
        config = backend.default_config()
        config.update({"prefix": str(self.prefix), "wine": str(self.wine)})
        supported = backend._probe_result(True, "supported", "ok")
        status = backend.direct3d_status(config, supported)
        self.assertEqual(status["selected"], "dxvk")
        self.assertTrue(status["dxvk_available"])
        self.assertFalse(status["dxvk_active"])
        self._apply(FakeRegistry(), True)
        status = backend.direct3d_status(config, supported)
        self.assertTrue(status["dxvk_active"])
        self.assertEqual(status["dxvk_version"], backend.DXVK_VERSION)
        with mock.patch.dict(os.environ, {"WINEDLLOVERRIDES": "dxgi=b"}):
            status = backend.direct3d_status(config, supported)
        self.assertFalse(status["dxvk_active"])
        self.assertIn("WINEDLLOVERRIDES", status["dxvk_reason"])
        checking = backend.direct3d_status(config, None)
        self.assertIsNone(checking["dxvk_available"])


class DxvkLaunchEnvironmentTests(DxvkTestCase):
    def _assert_no_dxvk_names(self, text):
        for name in DXVK_NAMES:
            self.assertNotIn(name, text.lower())

    def test_wine_environment_never_overrides_dxvk_dlls(self):
        for use_vulkan in (False, True):
            env = backend.wine_environment(self.prefix, self.wine, True, use_vulkan)
            self._assert_no_dxvk_names(env["WINEDLLOVERRIDES"])
            self._assert_no_dxvk_names(backend._outlook_environment(env)["WINEDLLOVERRIDES"])
            self.assertIn("renderer=", env["WINE_D3D_CONFIG"])

    def test_user_winedlloverrides_is_left_untouched(self):
        with mock.patch.dict(os.environ, {"WINEDLLOVERRIDES": "dxgi=b"}):
            env = backend.wine_environment(self.prefix, self.wine)
        self.assertEqual(env["WINEDLLOVERRIDES"], "dxgi=b")

    def test_launcher_scripts_never_mention_dxvk_dlls(self):
        executable = self.prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        executable.mkdir(parents=True)
        for app in ("word", "excel", "outlook"):
            target = executable / backend.APP_META[app]["exe"]
            target.write_text("exe")
            text = backend._shortcut_launcher_text(app, self.prefix, self.wine, target, None)
            self._assert_no_dxvk_names(text)

    def test_preload_environment_never_overrides_dxvk_dlls(self):
        source = (MANAGER_DIR / "wine4office_backend.py").read_text()
        for line in source.splitlines():
            if "WINEDLLOVERRIDES" in line:
                self._assert_no_dxvk_names(line)

    def test_office_launch_verifies_dxvk_before_other_wine_commands(self):
        office = self.prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "WINWORD.EXE").write_text("exe")
        order = []
        with mock.patch.object(backend, "converge_dxvk_state",
                               side_effect=lambda *a, **k: order.append("dxvk")) as converge, \
             mock.patch.object(backend, "ensure_safe_x11_defaults",
                               side_effect=lambda *a, **k: order.append("x11")), \
             mock.patch.object(backend, "prepare_office_building_blocks"), \
             mock.patch.object(backend, "register_cloud_fonts"), \
             mock.patch.object(backend.subprocess, "Popen") as popen:
            backend.launch_app_process(self.prefix, self.wine, "word", use_dxvk=True)
        converge.assert_called_once()
        self.assertEqual(converge.call_args.args[2], True)
        self.assertEqual(order, ["dxvk", "x11"])
        popen.assert_called_once()

    def test_office_launch_holds_shared_lock_for_wine_commands(self):
        office = self.prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "WINWORD.EXE").write_text("exe")
        held = []

        def x11(*_args, **_kwargs):
            # flock conflicts between separate open file descriptions, even
            # inside one process, so this sees the launch's shared lock.
            descriptor = os.open(self.prefix / backend.DXVK_LOCK_NAME, os.O_RDWR)
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                held.append("free")
                fcntl.flock(descriptor, fcntl.LOCK_UN)
            except BlockingIOError:
                held.append("held")
            finally:
                os.close(descriptor)

        with mock.patch.object(backend, "converge_dxvk_state"), \
             mock.patch.object(backend, "ensure_safe_x11_defaults", side_effect=x11), \
             mock.patch.object(backend, "prepare_office_building_blocks"), \
             mock.patch.object(backend, "register_cloud_fonts"), \
             mock.patch.object(backend.subprocess, "Popen"):
            backend.launch_app_process(self.prefix, self.wine, "word", use_dxvk=True)
        self.assertEqual(held, ["held"])
        with backend.dxvk_prefix_lock(self.prefix) as acquired:
            self.assertTrue(acquired)

    def test_launcher_script_waits_on_shared_dxvk_lock(self):
        target = self.prefix / "drive_c/WINWORD.EXE"
        target.write_text("exe")
        text = backend._shortcut_launcher_text("word", self.prefix, self.wine, target, None)
        self.assertIn(backend.DXVK_LOCK_NAME, text)
        self.assertIn(
            f'flock -s -w {int(backend.DXVK_LAUNCH_LOCK_TIMEOUT_SECONDS)} "$dxvk_lock_fd" || true',
            text,
        )
        self.assertLess(text.index("flock -s"), text.index('exec "$wine"'))
        script = self.root / "launcher.sh"
        script.write_text(text)
        subprocess.run(["bash", "-n", str(script)], check=True)

    def test_office_launch_survives_dxvk_failure(self):
        office = self.prefix / "drive_c/Program Files/Microsoft Office/root/Office16"
        office.mkdir(parents=True)
        (office / "WINWORD.EXE").write_text("exe")
        with mock.patch.object(backend, "converge_dxvk_state",
                               side_effect=RuntimeError("broken")), \
             mock.patch.object(backend, "ensure_safe_x11_defaults"), \
             mock.patch.object(backend, "prepare_office_building_blocks"), \
             mock.patch.object(backend, "register_cloud_fonts"), \
             mock.patch.object(backend.subprocess, "Popen") as popen:
            backend.launch_app_process(self.prefix, self.wine, "word", use_dxvk=True)
        popen.assert_called_once()


class DxvkManagerFlowTests(DxvkTestCase):
    def _pending_dxvk_state(self):
        state = manager.ManagerState()
        state.config.update({
            "prefix": str(self.prefix), "wine": str(self.wine),
            "use_dxvk": True, "use_vulkan": False, "use_x11": False,
            "graphics_active_use_dxvk": False, "graphics_active_use_vulkan": False,
            "graphics_active_use_x11": True, "graphics_restart_required": True,
        })
        return state

    def test_deferred_dxvk_change_is_not_marked_active(self):
        state = self._pending_dxvk_state()
        with mock.patch.object(backend, "prepare_preload_runner_update", return_value=None), \
             mock.patch.object(backend, "stop_wine"), \
             mock.patch.object(backend, "dxvk_requested", return_value=(True, "")), \
             mock.patch.object(backend, "converge_dxvk_state",
                               return_value={"changed": False, "deferred": True,
                                             "reason": "Wine is running"}), \
             mock.patch.object(backend, "save_config"):
            applied = state.apply_graphics_settings()
        self.assertFalse(applied["graphics_active_use_dxvk"])
        self.assertTrue(applied["graphics_restart_required"])
        self.assertFalse(applied["graphics_active_use_x11"])  # display change applied

    def test_unknown_vulkan_support_is_not_marked_active(self):
        state = self._pending_dxvk_state()
        with mock.patch.object(backend, "prepare_preload_runner_update", return_value=None), \
             mock.patch.object(backend, "stop_wine"), \
             mock.patch.object(backend, "dxvk_requested", return_value=(None, "timeout")), \
             mock.patch.object(backend, "apply_dxvk_state") as apply, \
             mock.patch.object(backend, "save_config"):
            applied = state.apply_graphics_settings()
        apply.assert_not_called()
        self.assertFalse(applied["graphics_active_use_dxvk"])
        self.assertTrue(applied["graphics_restart_required"])

    def test_completed_dxvk_change_and_unsupported_fallback_are_active(self):
        for requested in (True, False):
            with self.subTest(requested=requested):
                state = self._pending_dxvk_state()
                with mock.patch.object(backend, "prepare_preload_runner_update",
                                       return_value=None), \
                     mock.patch.object(backend, "stop_wine"), \
                     mock.patch.object(backend, "dxvk_requested",
                                       return_value=(requested, "")), \
                     mock.patch.object(backend, "converge_dxvk_state",
                                       return_value={"changed": True, "deferred": False}), \
                     mock.patch.object(backend, "save_config"):
                    applied = state.apply_graphics_settings()
                self.assertTrue(applied["graphics_active_use_dxvk"])
                self.assertFalse(applied["graphics_restart_required"])

    def test_apply_graphics_settings_changes_dxvk_while_wine_is_stopped(self):
        state = manager.ManagerState()
        state.config.update({
            "prefix": str(self.prefix), "wine": str(self.wine),
            "use_dxvk": False, "use_vulkan": False,
            "graphics_active_use_dxvk": True, "graphics_active_use_vulkan": False,
            "graphics_restart_required": True,
        })
        order = []
        with mock.patch.object(backend, "prepare_preload_runner_update", return_value=None), \
             mock.patch.object(backend, "stop_wine",
                               side_effect=lambda *a, **k: order.append("stop")), \
             mock.patch.object(backend, "converge_dxvk_state",
                               side_effect=lambda *a, **k: order.append(("dxvk", a[2]))
                               or {"changed": True, "deferred": False}), \
             mock.patch.object(backend, "save_config"):
            applied = state.apply_graphics_settings()
        self.assertEqual(order, ["stop", ("dxvk", False)])
        self.assertFalse(applied["graphics_active_use_dxvk"])
        self.assertFalse(applied["graphics_restart_required"])

    def test_failed_dxvk_change_keeps_previous_graphics_settings(self):
        state = manager.ManagerState()
        state.config.update({
            "prefix": str(self.prefix), "wine": str(self.wine),
            "use_dxvk": False, "graphics_active_use_dxvk": True,
            "graphics_restart_required": True,
        })
        with mock.patch.object(backend, "prepare_preload_runner_update", return_value=None), \
             mock.patch.object(backend, "stop_wine"), \
             mock.patch.object(backend, "converge_dxvk_state",
                               side_effect=RuntimeError("Could not disable DXVK")), \
             mock.patch.object(backend, "save_config") as save:
            with self.assertRaisesRegex(RuntimeError, "disable DXVK"):
                state.apply_graphics_settings()
        save.assert_not_called()
        self.assertTrue(state.config["graphics_active_use_dxvk"])
        self.assertTrue(state.config["graphics_restart_required"])

    def test_launch_setting_never_runs_the_probe(self):
        with mock.patch.object(backend, "run_dxvk_probe",
                               side_effect=AssertionError("probe")), \
             mock.patch.object(backend, "dxvk_requested",
                               wraps=backend.dxvk_requested) as requested:
            config = backend.default_config()
            setting = manager.launch_dxvk_setting(config, str(self.prefix), str(self.wine))
        self.assertIsNone(setting)
        self.assertIs(requested.call_args.kwargs["probe"], False)

    def test_launch_setting_skips_unmanaged_prefixes(self):
        unmanaged = self._make_prefix(self.home / "plain", managed=False)
        with mock.patch.object(backend, "dxvk_requested") as requested:
            self.assertIsNone(manager.launch_dxvk_setting({}, str(unmanaged), str(self.wine)))
        requested.assert_not_called()
        with mock.patch.object(backend, "dxvk_requested", return_value=(True, "")):
            self.assertTrue(manager.launch_dxvk_setting({}, str(self.prefix), str(self.wine)))

    def test_snapshot_reports_direct3d_status(self):
        state = manager.ManagerState()
        state.config.update({"prefix": str(self.prefix), "wine": str(self.wine)})
        state.dxvk_support = backend._probe_result(False, "cpu-only", "CPU only.")
        snapshot = state.snapshot()
        self.assertEqual(snapshot["direct3d"]["selected"], "dxvk")
        self.assertFalse(snapshot["direct3d"]["dxvk_available"])
        self.assertEqual(snapshot["direct3d"]["dxvk_reason"], "CPU only.")

    def test_post_install_never_pauses_services_and_defers_while_wine_runs(self):
        config = backend.default_config()
        config.update({"prefix": str(self.prefix), "wine": str(self.wine)})
        lines = []
        context = post_install.PostInstallContext(
            config=config, font_helper=Path("/none"), manager_command=[],
            icons=Path("/none"), output=lines.append,
        )
        with mock.patch.object(backend, "dxvk_requested", return_value=(True, "")), \
             mock.patch.object(backend, "prepare_preload_runner_update",
                               side_effect=AssertionError("paused")), \
             mock.patch.object(backend, "stop_wine", side_effect=AssertionError("stopped")), \
             mock.patch.object(backend, "wine_running_in_prefix", return_value=True), \
             mock.patch.object(backend, "apply_dxvk_state") as apply:
            result = post_install._sync_direct3d_backend(context)
        self.assertTrue(result["deferred"])
        apply.assert_not_called()
        self.assertTrue(any("after Wine stops" in line for line in lines))

    def test_post_install_applies_when_idle(self):
        config = backend.default_config()
        config.update({"prefix": str(self.prefix), "wine": str(self.wine)})
        context = post_install.PostInstallContext(
            config=config, font_helper=Path("/none"), manager_command=[],
            icons=Path("/none"), output=lambda _line: None,
        )
        with mock.patch.object(backend, "dxvk_requested", return_value=(True, "")), \
             mock.patch.object(backend, "prepare_preload_runner_update",
                               side_effect=AssertionError("paused")), \
             mock.patch.object(backend, "wait_for_prefix_idle", return_value=True), \
             mock.patch.object(backend, "apply_dxvk_state",
                               return_value={"changed": True, "enabled": True,
                                             "reason": ""}) as apply:
            result = post_install._sync_direct3d_backend(context)
        apply.assert_called_once()
        self.assertTrue(result["changed"])

    def test_runner_update_refreshes_dxvk_and_never_fails_the_update(self):
        state = manager.ManagerState()
        lines = []
        state.output = lines.append
        with mock.patch.object(backend, "dxvk_requested", return_value=(True, "")), \
             mock.patch.object(backend, "converge_dxvk_state",
                               side_effect=RuntimeError("rolled back")) as converge:
            state._sync_direct3d_after_runner_update(
                {"prefix": str(self.prefix)}, str(self.wine)
            )
        converge.assert_called_once()
        self.assertEqual(converge.call_args.kwargs["idle_wait"], 15)
        self.assertTrue(any("rolled back" in line for line in lines))


if __name__ == "__main__":
    unittest.main()
