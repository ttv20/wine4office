#!/usr/bin/env python3

import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import wine4office_incident as incident


COMPLETE_ENV = {
    "OPENOBSERVE_URL": "https://opos.example.com",
    "OPENOBSERVE_ACCOUNT": "writer@example.com",
    "OPENOBSERVE_TOKEN": "test-secret",
    "OPENOBSERVE_ORG": "default",
    "OPENOBSERVE_STREAM": "wine4office",
    "OPENOBSERVE_TRACE_STREAM": "wine4office_trace",
    "OPENOBSERVE_ARTIFACT_STREAM": "wine4office_artifact",
}


class FakeResponse:
    status = 200

    def __init__(self, items=1):
        self.payload = json.dumps({
            "errors": False,
            "items": [{"index": {"status": 200}} for _index in range(items)],
        }).encode()

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self):
        return self.payload


class FakeProcess:
    def __init__(self, return_code, polls=1, trace=b""):
        self.pid = os.getpid()
        self.return_code = return_code
        self.remaining_polls = polls
        self.stdout = io.BytesIO(trace)

    def poll(self):
        if self.remaining_polls:
            self.remaining_polls -= 1
            return None
        return self.return_code

    def wait(self):
        return self.return_code


class FakeProbe:
    def __init__(self, *_args):
        self.closed = False

    def ping(self, _timeout):
        return False

    def close(self):
        self.closed = True


class IncidentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.data_home = Path(self.temp.name) / "data"
        self.environment = mock.patch.dict(os.environ, {
            "HOME": str(Path(self.temp.name) / "home"),
            "XDG_DATA_HOME": str(self.data_home),
        }, clear=False)
        self.environment.start()

    def tearDown(self):
        self.environment.stop()
        self.temp.cleanup()

    def _incident(self, kind="hang", trace="bounded trace"):
        with mock.patch.object(incident, "technical_report", return_value={
            "application": "powerpoint", "runner_version": "test",
        }):
            return incident.create_incident(
                kind=kind, app="powerpoint", summary="Synthetic test",
                trace=trace, use_x11=True, manager_version="test",
                runner_version="test", exit_code=9 if kind == "crash" else None,
            )

    def test_reporting_screen_requires_every_openobserve_environment_value(self):
        self.assertFalse(incident.reporting_available({}))
        for key in COMPLETE_ENV:
            incomplete = dict(COMPLETE_ENV)
            incomplete.pop(key)
            with self.subTest(missing=key):
                self.assertFalse(incident.reporting_available(incomplete))
        self.assertTrue(incident.reporting_available(dict(COMPLETE_ENV)))

    def test_openobserve_url_and_streams_are_validated(self):
        unsafe = {**COMPLETE_ENV, "OPENOBSERVE_URL": "http://opos.example.com"}
        self.assertFalse(incident.reporting_available(unsafe))
        unsafe = {**COMPLETE_ENV, "OPENOBSERVE_STREAM": "../../wrong"}
        self.assertFalse(incident.reporting_available(unsafe))

    def test_redaction_removes_home_email_and_secret_shapes(self):
        home = str(Path.home())
        redacted = incident.redact_text(
            f"{home}/Documents/private.docx jane@example.com token=abc123 password:mine"
        )
        self.assertNotIn(home, redacted)
        self.assertNotIn("jane@example.com", redacted)
        self.assertNotIn("abc123", redacted)
        self.assertNotIn("mine", redacted)

    def test_incident_files_are_private_and_loadable(self):
        path = self._incident(trace="trace from /home/private/file.pptx")
        loaded_path, record, trace = incident.load_incident(path)
        self.assertEqual(loaded_path, path.resolve())
        self.assertEqual(record["status"], "pending")
        self.assertNotIn("/home/private", trace)
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(path.parent.stat().st_mode & 0o777, 0o700)

    def test_normal_exit_does_not_create_incident(self):
        process = FakeProcess(0, trace=b"normal output")
        with mock.patch.object(incident, "create_incident") as create:
            result = incident.supervise_process(
                process, app="powerpoint", prefix=Path("/tmp/prefix"),
                use_x11=False, manager_version="test", runner_version="test",
            )
        self.assertEqual(result, 0)
        create.assert_not_called()

    def test_nonzero_exit_creates_bounded_crash_incident(self):
        process = FakeProcess(9, trace=b"Unhandled exception token=private")
        with mock.patch.object(incident, "_notify"):
            result = incident.supervise_process(
                process, app="powerpoint", prefix=Path("/tmp/prefix"),
                use_x11=False, manager_version="test", runner_version="test",
            )
        self.assertEqual(result, 9)
        paths = list(incident.incident_home().glob("*.json"))
        self.assertEqual(len(paths), 1)
        _path, record, trace = incident.load_incident(paths[0])
        self.assertEqual(record["kind"], "crash")
        self.assertNotIn("private", trace)

    def test_repeated_x11_ping_failures_create_one_hang_incident(self):
        process = FakeProcess(0, polls=5)
        probe = FakeProbe()
        with mock.patch.dict(os.environ, {"DISPLAY": ":99"}), \
             mock.patch.object(incident, "process_snapshot", return_value="threads"), \
             mock.patch.object(incident, "_notify"):
            result = incident.supervise_process(
                process, app="powerpoint", prefix=Path("/tmp/prefix"),
                use_x11=True, manager_version="test", runner_version="test",
                probe_factory=lambda *_args: probe, hang_interval=0.01,
                hang_timeout=0.01, hang_failures=2,
            )
        self.assertEqual(result, 0)
        paths = list(incident.incident_home().glob("*.json"))
        self.assertEqual(len(paths), 1)
        self.assertEqual(incident.load_incident(paths[0])[1]["kind"], "hang")
        self.assertTrue(probe.closed)

    def test_explicit_submit_uses_all_three_streams_and_marks_sent(self):
        path = self._incident()
        attachment = Path(self.temp.name) / "example.pptx"
        attachment.write_bytes(b"synthetic office file")
        captured = {}

        def urlopen(request, **_kwargs):
            captured["request"] = request
            documents = request.data.decode().splitlines()
            return FakeResponse(items=len(documents) // 2)

        with mock.patch.object(incident.urllib.request, "urlopen", side_effect=urlopen):
            result = incident.submit_incident(
                path, context="Opened Layout repeatedly", technical={"safe": True},
                trace="bounded trace", attachment=attachment,
                environ=dict(COMPLETE_ENV),
            )

        payload = captured["request"].data.decode()
        self.assertIn('"_index":"wine4office"', payload)
        self.assertIn('"_index":"wine4office_trace"', payload)
        self.assertIn('"_index":"wine4office_artifact"', payload)
        self.assertNotIn("test-secret", payload)
        self.assertGreaterEqual(result["items"], 3)
        self.assertEqual(incident.load_incident(path)[1]["status"], "sent")


if __name__ == "__main__":
    unittest.main()
