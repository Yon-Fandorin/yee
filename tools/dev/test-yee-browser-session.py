#!/usr/bin/env python3
"""Focused JSONL session tests for yee-browser.py."""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import argparse
import importlib.util
import io
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parent
CLI = ROOT / "yee-browser.py"
sys.path.insert(0, str(ROOT))
spec = importlib.util.spec_from_file_location("yee_browser", CLI)
yee_browser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(yee_browser)


class SessionTests(unittest.TestCase):
    def test_document_option_two_positions_preserve_capability_and_literal_values(self):
        config=argparse.Namespace(bridge=str(self.bridge),timeout=1,request_timeout=2,compact=True)
        for tail in (['read','4'],['fill','4','--document'],['click','4','--full'],
                     ['scroll','down','1'],['wait-change','250','--content'],
                     ['batch-ref','[{"command":"click","ref":"4"}]']):
            first=yee_browser.session_command(['--document','doc',*tail],config)
            second=yee_browser.session_command([tail[0],'--document','doc',*tail[1:]],config)
            self.assertEqual(vars(first),vars(second))
        for args in (['fill','--document'],['fill','--document','doc'],
                     ['--document','doc','fill','--document','other','4','x'],
                     ['fill','--bridge','/private/tmp/other','4','x'],
                     ['fill','4','x','--document','doc']):
            with self.assertRaises(yee_browser.BridgeError):yee_browser.session_command(args,config)
        value=yee_browser.session_command(['--document','doc','fill','4','--bridge'],config)
        self.assertEqual(value.value,'--bridge')

    def test_tab_commands_and_invalid_later_selection_preflight(self):
        config=argparse.Namespace(bridge=str(self.bridge),timeout=1,request_timeout=2,compact=True)
        tab='d12f382a-c0cf-4c2f-8836-5c22d3799adb'
        self.assertEqual(yee_browser.session_command(['tabs'],config).command,'tabs')
        self.assertEqual(yee_browser.session_command(['select-tab',tab],config).tab,tab)
        with mock.patch.object(yee_browser,'run') as execute:
            with self.assertRaises(yee_browser.BridgeError):
                yee_browser.preflight_command_input(io.BytesIO(b'["tabs"]\n["select-tab","not-a-capability"]\n'),config)
            execute.assert_not_called()

    def test_file_plan_ignores_editor_blank_lines(self):
        plan = self.bridge / "plan.jsonl"
        plan.write_bytes(b'\n["status"]\n \t\r\n["detach"]\n\n')
        plan.chmod(0o600)
        self.assertEqual(yee_browser.load_command_input(str(plan)).read(),
                         b'["status"]\n["detach"]\n')

    def test_file_preflight_rejects_bad_later_command_before_dispatch(self):
        config = argparse.Namespace(bridge=str(self.bridge), timeout=1,
                                    request_timeout=2, compact=True)
        for suffix in (b'not-json\n', b'["bad"]\n', b'[]\n',
                       b'["status"]\n' * 256):
            with mock.patch.object(yee_browser, 'run') as execute:
                with self.assertRaises(yee_browser.BridgeError):
                    yee_browser.preflight_command_input(io.BytesIO(b'["status"]\n' + suffix), config)
                execute.assert_not_called()

    def test_file_preflight_rewinds_valid_plan(self):
        config = argparse.Namespace(bridge=str(self.bridge), timeout=1,
                                    request_timeout=2, compact=True)
        data = b'["status"]\n["detach"]\n'
        self.assertEqual(yee_browser.preflight_command_input(io.BytesIO(data), config).read(), data)

    def test_private_command_file_loads_without_using_stdin(self):
        plan = self.bridge / "plan.jsonl"
        plan.write_bytes(b'["status"]\n["detach"]\n')
        plan.chmod(0o600)
        loaded = yee_browser.load_command_input(str(plan))
        self.assertEqual(loaded.read(), plan.read_bytes())

    def test_command_file_rejects_public_symlink_fifo_and_oversize(self):
        plan = self.bridge / "plan.jsonl"
        plan.write_bytes(b'["status"]\n')
        plan.chmod(0o644)
        with self.assertRaises(yee_browser.BridgeError):
            yee_browser.load_command_input(str(plan))
        plan.chmod(0o600)
        link = self.bridge / "link.jsonl"
        link.symlink_to(plan)
        with self.assertRaises(yee_browser.BridgeError):
            yee_browser.load_command_input(str(link))
        fifo = self.bridge / "fifo.jsonl"
        os.mkfifo(fifo, 0o600)
        with self.assertRaises(yee_browser.BridgeError):
            yee_browser.load_command_input(str(fifo))
        plan.write_bytes(b'x' * (yee_browser.MAX_SESSION_LINE + 1))
        with self.assertRaises(yee_browser.BridgeError):
            yee_browser.load_command_input(str(plan))

    def test_file_input_bad_command_still_stops_before_following_command(self):
        stream = io.BytesIO(b'["bad"]\n["status"]\n')
        with mock.patch.object(sys, "stdout", io.StringIO()), mock.patch.object(yee_browser, "run") as execute:
            self.assertEqual(yee_browser.run_session(argparse.Namespace(compact=True), stream), 2)
        execute.assert_not_called()

    def test_request_record_failure_prevents_dispatch(self):
        writer = mock.Mock()
        writer.write.side_effect = OSError("disk full")
        args = argparse.Namespace(bridge=str(self.bridge), timeout=1.0,
                                  request_timeout=2.0, command="status",
                                  compact=False, text=False, _transcript=writer)
        with self.assertRaises(yee_browser.RecordingError):
            yee_browser.run(args)
        self.assertFalse((self.bridge / "request.json").exists())
        lock = yee_browser.acquire_lock(str(self.bridge / "request.lock"), 1)
        lock.close()

    def test_response_record_failure_prevents_ack_and_next_command(self):
        writer = mock.Mock()
        writer.write.side_effect = [None, OSError("disk full")]
        (self.bridge / "response.json").write_text(json.dumps({
            "id": "test-request", "ok": True, "document": "doc", "revision": 1,
            "truncated": False, "snapshot": "page"}))
        config = argparse.Namespace(bridge=str(self.bridge), timeout=1.0,
                                    request_timeout=2.0, compact=False,
                                    _transcript=writer)
        stream = argparse.Namespace(buffer=io.BytesIO(b'["observe"]\n["status"]\n'))
        output = io.StringIO()
        with mock.patch.object(sys, "stdin", stream), mock.patch.object(sys, "stdout", output), \
                mock.patch.object(yee_browser.uuid, "uuid4", return_value="test-request"):
            self.assertEqual(yee_browser.run_session(config), 2)
        self.assertFalse((self.bridge / "client-state.json").exists())
        self.assertEqual(json.loads((self.bridge / "request.json").read_text())["command"], "observe")
        self.assertEqual(writer.write.call_count, 2)
        self.assertIn("may already have executed", output.getvalue())

    def test_command_limit_does_not_execute_257th_command(self):
        stream = argparse.Namespace(buffer=io.BytesIO(b'["status"]\n' * 257))
        config = argparse.Namespace(compact=False)
        output = io.StringIO()
        command_config = argparse.Namespace()
        with mock.patch.object(sys, "stdin", stream), mock.patch.object(sys, "stdout", output):
            with mock.patch.object(yee_browser, "session_command", return_value=command_config):
                with mock.patch.object(yee_browser, "run", return_value=(0, {"ok": True})) as execute:
                    self.assertEqual(yee_browser.run_session(config), 2)
        self.assertEqual(execute.call_count, 256)
        self.assertIn("limit exceeded", json.loads(output.getvalue().splitlines()[-1])["error"])

    def test_help_and_surrogate_are_single_structured_errors(self):
        for line in ('["observe","--help"]\n', '["ask","\\ud800"]\n'):
            result = self.run_session(line)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(len(result.stdout.splitlines()), 1)
            self.assertFalse(json.loads(result.stdout)["ok"])
            self.assertNotIn("Traceback", result.stderr)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.bridge = pathlib.Path(self.tmp.name) / "bridge"
        self.bridge.mkdir(mode=0o700)

    def tearDown(self):
        self.tmp.cleanup()

    def peer(self, count):
        errors = []
        requests = []

        def work():
            try:
                for _ in range(count):
                    deadline = time.time() + 3
                    while time.time() < deadline and not (self.bridge / "request.json").exists():
                        time.sleep(.01)
                    request = json.loads((self.bridge / "request.json").read_text())
                    requests.append(request)
                    response = {"id": request["id"], "ok": True, "status": request["command"]}
                    temporary = self.bridge / ".peer-response"
                    temporary.write_text(json.dumps(response))
                    os.replace(temporary, self.bridge / "response.json")
                    # The client replaces request.json on the next command.
                    (self.bridge / "request.json").unlink()
            except BaseException as exc:
                errors.append(exc)

        thread = threading.Thread(target=work)
        thread.start()
        return thread, errors, requests

    def run_session(self, data, timeout=5):
        return subprocess.run(
            [sys.executable, str(CLI), "--bridge", str(self.bridge),
             "--timeout", "2", "session"],
            input=data, text=True, capture_output=True, timeout=timeout)

    def test_two_commands_stream_responses_before_eof(self):
        thread, errors, _ = self.peer(2)
        process = subprocess.Popen(
            [sys.executable, str(CLI), "--bridge", str(self.bridge), "session"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        process.stdin.write('["status"]\n')
        process.stdin.flush()
        first = json.loads(process.stdout.readline())
        self.assertEqual(first["status"], "status")
        process.stdin.write('["cancel"]\n')
        process.stdin.flush()
        second = json.loads(process.stdout.readline())
        self.assertEqual(second["status"], "cancel")
        process.stdin.close()
        self.assertEqual(process.wait(timeout=3), 0)
        process.stdout.close()
        thread.join(timeout=3)
        self.assertEqual(errors, [])

    def test_invalid_line_stops_prequeued_commands(self):
        result = self.run_session('["status", 3]\n["cancel"]\n')
        self.assertEqual(result.returncode, 2)
        self.assertIn("array of strings", result.stdout)
        self.assertFalse((self.bridge / "request.json").exists())

    def test_oversize_line_stops_session(self):
        result = self.run_session("[\"ask\",\"" + ("x" * (64 * 1024)) + "\"]\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("64 KiB", result.stdout)

    def test_oversize_line_without_newline_fails_before_eof(self):
        process = subprocess.Popen(
            [sys.executable, str(CLI), "--bridge", str(self.bridge), "session"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        process.stdin.write("[\"status\"]" + (" " * (64 * 1024)))
        process.stdin.flush()
        output = process.stdout.readline()
        self.assertIn("64 KiB", output)
        process.stdin.close()
        self.assertEqual(process.wait(timeout=3), 2)
        process.stdout.close()

    def test_config_override_rejected_and_later_action_not_sent(self):
        result = self.run_session('["--timeout", "1", "status"]\n["status"]\n')
        self.assertEqual(result.returncode, 2)
        self.assertIn("override", result.stdout)
        self.assertFalse((self.bridge / "request.json").exists())

    def test_native_rejection_stops_queued_action(self):
        errors = []
        def peer():
            try:
                deadline = time.time() + 3
                while time.time() < deadline and not (self.bridge / "request.json").exists():
                    time.sleep(.01)
                request = json.loads((self.bridge / "request.json").read_text())
                (self.bridge / "response.json").write_text(json.dumps({
                    "id": request["id"], "ok": False, "error": "approval_required"}))
            except BaseException as exc:
                errors.append(exc)
        thread = threading.Thread(target=peer); thread.start()
        result = self.run_session('["click", "ref"]\n["status"]\n')
        thread.join(timeout=3)
        self.assertEqual(result.returncode, 1)
        self.assertIn("approval_required", result.stdout)
        self.assertEqual(errors, [])

    def test_compact_session_document_round_trip(self):
        (self.bridge / "client-state.json").write_text(json.dumps({"document": "doc", "revision": 2}))
        thread, errors, requests = self.peer(1)
        result = subprocess.run(
            [sys.executable, str(CLI), "--bridge", str(self.bridge), "--compact", "session"],
            input='["--document", "doc", "click", "@4"]\n', text=True,
            capture_output=True, timeout=5)
        thread.join(timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(requests[0]["ref"], "doc_4")
        self.assertEqual(errors, [])

    def test_option_looking_fill_value_is_data(self):
        thread, errors, requests = self.peer(1)
        result = self.run_session('["fill", "ref", "--bridge"]\n')
        thread.join(timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(errors, [])
        self.assertEqual(requests[0]["value"], "--bridge")

    def test_scroll_session_shape_and_bounds(self):
        config = argparse.Namespace(bridge=str(self.bridge), timeout=1, request_timeout=2, compact=True)
        parsed = yee_browser.session_command(["--document","doc","scroll","down","3"], config)
        self.assertEqual((parsed.command,parsed.direction,parsed.pages,parsed.document), ("scroll","down",3,"doc"))
        for argv in (["scroll","left"],["scroll","up","0"],["scroll","up","4"],["scroll","up","1.5"]):
            with self.assertRaises(yee_browser.BridgeError): yee_browser.session_command(argv,config)

    def test_wait_change_session_bounds(self):
        config = argparse.Namespace(bridge=str(self.bridge),timeout=1,request_timeout=2,compact=True)
        parsed = yee_browser.session_command(['--document','doc','wait-change','30000'],config)
        self.assertEqual((parsed.document,parsed.wait_ms),('doc',30000))
        self.assertFalse(parsed.content)
        content = yee_browser.session_command(['--document','doc','wait-change','1000','--content'],config)
        self.assertTrue(content.content)
        for value in ('0','30001','-1','1.5','nan','١','9'*100):
            with self.assertRaises(yee_browser.BridgeError):
                yee_browser.session_command(['wait-change',value],config)

    def test_validation_failure_releases_lock(self):
        result = self.run_session('["fill-named", "", "value"]\n')
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.bridge / "request.json").exists())
        self.assertFalse((self.bridge / "client-pending.json").exists())
        thread, errors, _ = self.peer(1)
        followup = self.run_session('["status"]\n')
        thread.join(timeout=3)
        self.assertEqual(followup.returncode, 0)
        self.assertEqual(errors, [])

    def test_validation_failure_releases_lock_in_process(self):
        original = yee_browser.request_for
        def fail(*_args, **_kwargs):
            raise yee_browser.BridgeError("forced validation failure")
        yee_browser.request_for = fail
        try:
            args = argparse.Namespace(bridge=str(self.bridge), timeout=1.0,
                                      request_timeout=2.0, command="status",
                                      compact=False, text=False)
            with self.assertRaises(yee_browser.BridgeError):
                yee_browser.run(args)
        finally:
            yee_browser.request_for = original
        lock = yee_browser.acquire_lock(str(self.bridge / "request.lock"), 1)
        lock.close()


if __name__ == "__main__":
    unittest.main()
