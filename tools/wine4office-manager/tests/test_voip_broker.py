#!/usr/bin/env python3
"""Integration coverage for the installed Wine4Office VoIP broker."""

from __future__ import annotations

import os
import re
import select
import shutil
import subprocess
import time
import unittest
from pathlib import Path


SERVICE = "org.wine.VoipCallBroker1"
PATH = "/org/wine/VoipCallBroker1"
INTERFACE = SERVICE


class VoipBrokerIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
            raise unittest.SkipTest("run this integration test under dbus-run-session")
        cls.gdbus = shutil.which("gdbus")
        if not cls.gdbus:
            raise unittest.SkipTest("gdbus is unavailable")
        wine = os.environ.get("WINE4OFFICE_TEST_WINE")
        broker = os.environ.get("WINE4OFFICE_TEST_VOIP_BROKER")
        if not wine or not broker or not Path(wine).is_file() or not Path(broker).is_file():
            raise unittest.SkipTest(
                "set WINE4OFFICE_TEST_WINE and WINE4OFFICE_TEST_VOIP_BROKER"
            )
        cls.command = [wine, broker]

    def setUp(self):
        self.processes: list[subprocess.Popen[str]] = []
        self.broker = self._start_broker()

    def tearDown(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                if process.stdin:
                    try:
                        process.stdin.write("\n")
                        process.stdin.flush()
                    except (BrokenPipeError, OSError):
                        pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

    def _start_broker(self):
        process = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env={**os.environ, "WINEDEBUG": "-all"},
        )
        self.processes.append(process)
        assert process.stdout is not None
        deadline = time.monotonic() + 15
        output = []
        while process.poll() is None and time.monotonic() < deadline:
            readable, _, _ = select.select(
                [process.stdout], [], [], deadline - time.monotonic()
            )
            if not readable:
                break
            line = process.stdout.readline()
            output.append(line)
            if line.startswith("READY "):
                return process
        self.fail("broker did not own its DBus name: " + "".join(output))

    def _call(self, method, *arguments, check=True):
        completed = subprocess.run(
            [
                self.gdbus,
                "call",
                "--session",
                "--dest",
                SERVICE,
                "--object-path",
                PATH,
                "--method",
                f"{INTERFACE}.{method}",
                *map(str, arguments),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
            check=False,
        )
        if check and completed.returncode:
            self.fail(f"{method} failed: {completed.stderr or completed.stdout}")
        return completed

    def _id_from(self, result):
        match = re.search(r"'([0-9a-f-]{36})'", result.stdout)
        self.assertIsNotNone(match, result.stdout)
        return match.group(1)

    def _create_outgoing(self, name):
        result = self._call("CreateOutgoing", f"context-{name}", name, "Wine4Office", 1)
        return self._id_from(result)

    def test_multiple_calls_transition_order_failure_rollback_and_cleanup(self):
        reservation = self._call("ReserveResources", "Office.VoipTask").stdout
        self.assertRegex(reservation, r"uint32 [01]")
        monitor = subprocess.Popen(
            [
                self.gdbus,
                "monitor",
                "--session",
                "--dest",
                SERVICE,
                "--object-path",
                PATH,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self.processes.append(monitor)
        first = self._create_outgoing("Alice")
        second = self._create_outgoing("Bob")
        self.assertNotEqual(first, second)

        failed = self._call("SetState", second, 3, check=False)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("InvalidTransition", failed.stderr)
        self._call("SetState", second, 2)

        self._call("SetState", first, 2)
        self._call("SetMediaState", first, 3)
        self._call("SetState", first, 3)
        self._call("SetState", first, 2)
        self._call("SetMuted", "true")
        self._call("End", first)
        self._call("End", second)
        time.sleep(0.2)
        monitor.terminate()
        output, _ = monitor.communicate(timeout=5)

        ordered = [
            f"CallStateChanged ('{first}', uint32 2)",
            f"MediaStateChanged ('{first}', uint32 3)",
            f"CallStateChanged ('{first}', uint32 3)",
            f"CallStateChanged ('{first}', uint32 2)",
            "MuteStateChanged (true,)",
            f"CallStateChanged ('{first}', uint32 4)",
            f"CallEnded ('{first}',)",
        ]
        position = -1
        for signal in ordered:
            next_position = output.find(signal, position + 1)
            self.assertGreater(next_position, position, output)
            position = next_position

    def test_accept_reject_upgrade_cancel_and_identity_cleanup(self):
        accepted = self._create_outgoing("Accepted")
        rejected = self._create_outgoing("Rejected")
        self._call("Accept", accepted, 3)
        self._call("Reject", rejected)

        upgrade = self._id_from(
            self._call(
                "CreateOutgoingUpgrade",
                accepted,
                "upgrade-context",
                "Accepted",
                "Wine4Office",
            )
        )
        self.assertNotEqual(upgrade, accepted)
        self._call("CancelUpgrade", upgrade)
        self._call("End", accepted)

        for ended in (accepted, rejected, upgrade):
            missing = self._call("End", ended, check=False)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("NotFound", missing.stderr)


    def test_optional_desktop_failures_do_not_mutate_calls(self):
        call_id = self._create_outgoing("NoDesktop")
        shown = self._call("ShowAppUI", call_id, check=False)
        self.assertNotEqual(shown.returncode, 0)
        self.assertIn("NotSupported", shown.stderr)

        associated = self._call(
            "SetActiveOnDevices",
            call_id,
            "['/org/freedesktop/ModemManager1/Modem/0']",
            check=False,
        )
        self.assertNotEqual(associated.returncode, 0)
        self.assertIn("NotSupported", associated.stderr)
        terminated = self._call("TerminateCellular", call_id, check=False)
        self.assertNotEqual(terminated.returncode, 0)
        self.assertIn("InvalidState", terminated.stderr)
        self._call("SetState", call_id, 2)

        incoming = self._call(
            "CreateIncoming",
            "ctx",
            "Caller",
            "+12025550123",
            "Wine4Office",
            "details",
            "",
            "",
            "",
            1,
            50000000,
            check=False,
        )
        self.assertNotEqual(incoming.returncode, 0)
        self.assertIn("NotSupported", incoming.stderr)
        incoming_upgrade = self._call(
            "CreateIncomingUpgrade",
            "ctx",
            "Caller",
            "+12025550123",
            "Wine4Office",
            "details",
            "",
            "",
            "",
            50000000,
            check=False,
        )
        self.assertNotEqual(incoming_upgrade.returncode, 0)
        self.assertIn("NotSupported", incoming_upgrade.stderr)


    def test_disconnect_restart_drops_all_call_identity(self):
        old_id = self._create_outgoing("BeforeRestart")
        assert self.broker.stdin is not None
        self.broker.stdin.write("\n")
        self.broker.stdin.flush()
        self.broker.wait(timeout=10)

        self.broker = self._start_broker()
        stale = self._call("End", old_id, check=False)
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn("NotFound", stale.stderr)
        new_id = self._create_outgoing("AfterRestart")
        self.assertNotEqual(old_id, new_id)
        self._call("End", new_id)


if __name__ == "__main__":
    unittest.main()
