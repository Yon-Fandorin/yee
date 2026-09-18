#!/usr/bin/env python3
"""Focused protocol tests for yee-browser.py; no external dependencies."""

import json
import fcntl
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import importlib.util


ROOT = pathlib.Path(__file__).resolve().parent
CLI = ROOT / "yee-browser.py"
COMPACT = ROOT / "yee_browser_compact.py"
spec = importlib.util.spec_from_file_location("yee_browser_compact", COMPACT)
compact = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compact)


class BridgeTests(unittest.TestCase):
    def test_retained_mcp_receipt_recovers_after_cli_ack_without_republication(self):
        spec=importlib.util.spec_from_file_location('receipt_cli',CLI)
        cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
        reference={'id':'already-settled','command':'click'}
        archive=self.bridge/('native-request-'+reference['id'].encode().hex().upper()+'.result')
        args=cli.build_parser().parse_args(['--bridge',str(self.bridge),'recover'])
        args._recover_reference=reference
        response={**reference,'ok':True,'execution_settled':True,'receipt_persisted':True}
        for invalid in (None,{**response,'id':'different'},
                        {**response,'execution_settled':False},{**response,'receipt_persisted':False}):
            if invalid is not None:archive.write_text(json.dumps(invalid))
            with self.assertRaises(cli.BridgeError):cli.run(args,emit_output=False)
            self.assertFalse((self.bridge/'client-pending.json').exists())
            self.assertFalse((self.bridge/'request.json').exists())
        archive.write_text(json.dumps(response))
        code,result=cli.run(args,emit_output=False)
        self.assertEqual((code,result),(0,response))
        self.assertFalse((self.bridge/'request.json').exists())
        self.assertFalse((self.bridge/'client-pending.json').exists())

    def test_disconnected_response_ack_retains_same_id_for_recovery(self):
        spec=importlib.util.spec_from_file_location('ack_cli',CLI)
        cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
        class Disconnected(BaseException):pass
        def reject_ack():raise Disconnected()
        args=cli.build_parser().parse_args(['--bridge',str(self.bridge),'status'])
        args._before_native_response_ack=reject_ack
        thread=self.peer('status',{'execution_settled':True,'receipt_persisted':True})
        with self.assertRaises(Disconnected):cli.run(args,emit_output=False)
        self.join_peer(thread)
        pending=json.loads((self.bridge/'client-pending.json').read_text())
        response=json.loads((self.bridge/'response.json').read_text())
        self.assertEqual(pending['id'],response['id'])
        payload=(self.bridge/'request.json').read_bytes()
        recovered=self.run_cli('recover')
        self.assertEqual(recovered.returncode,0,recovered.stderr)
        self.assertEqual(json.loads(recovered.stdout),response)
        self.assertEqual((self.bridge/'request.json').read_bytes(),payload)
        self.assertFalse((self.bridge/'client-pending.json').exists())

    def test_missing_durable_receipt_retains_fence_even_for_settled_execution(self):
        thread=self.peer('status',{'execution_settled':True,'receipt_persisted':False})
        result=self.run_cli('status');self.join_peer(thread)
        self.assertEqual(result.returncode,2,result.stderr)
        self.assertIn('completion receipt unavailable',result.stderr)
        pending=json.loads((self.bridge/'client-pending.json').read_text())
        second=self.run_cli('attach')
        self.assertEqual(second.returncode,2)
        self.assertIn(pending['id'],second.stderr)
        self.assertFalse((self.bridge/'client-state.json').exists())

    def test_content_wait_is_explicit_and_requires_native_confirmation(self):
        for confirmed in (False,True):
            (self.bridge/'client-state.json').write_text(json.dumps({'document':'doc','revision':1}))
            report={'reason':'limit','probes':2}
            if confirmed:report['comparison']='content'
            thread=self.peer('wait-change',{'execution_settled':True,'wait':report})
            result=self.run_cli('--document','doc','wait-change','100','--content')
            self.join_peer(thread)
            self.assertEqual(result.returncode,0 if confirmed else 2,result.stderr)
            self.assertEqual(json.loads((self.bridge/'request.json').read_text())['wait_mode'],'content')
            self.assertFalse((self.bridge/'client-pending.json').exists())
            (self.bridge/'request.json').unlink()
    def test_cancel_wait_bypasses_busy_lock_but_keeps_original_fence(self):
        pending={'id':'wait-id','command':'wait-change','document':'doc'}
        (self.bridge/'client-pending.json').write_text(json.dumps(pending))
        lock=(self.bridge/'request.lock').open('w')
        self.addCleanup(lock.close)
        fcntl.flock(lock,fcntl.LOCK_EX)
        def cancel_peer():
            for _ in range(200):
                path=self.bridge/('cancel-wait-'+'wait-id'.encode().hex().upper()+'.json')
                if path.exists():
                    control=json.loads(path.read_text())
                    if control=={'id':'wait-id','document':'doc'}:
                        (self.bridge/'response.json').write_text(json.dumps({
                            'id':'wait-id','ok':False,'execution_settled':True,'error':'wait_cancelled'}))
                    return
                time.sleep(.005)
        worker=threading.Thread(target=cancel_peer);worker.start()
        result=self.run_cli('--document','doc','cancel-wait','wait-id')
        worker.join(2)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['error'],'wait_cancelled')
        self.assertEqual(json.loads((self.bridge/'client-pending.json').read_text()),pending)
        self.assertFalse((self.bridge/'request.json').exists())

    def test_cancel_wait_rejects_wrong_identity_or_action_without_dispatch(self):
        for command,document,target in [('wait-change','wrong','wait-id'),
                                        ('wait-change','doc','other'),('click','doc','wait-id')]:
            (self.bridge/'client-pending.json').write_text(json.dumps({
                'id':'wait-id','command':command,'document':'doc'}))
            result=self.run_cli('--document',document,'cancel-wait',target)
            self.assertEqual(result.returncode,2,result.stderr)
            self.assertFalse(list(self.bridge.glob('cancel-wait-*.json')))

    def test_cancel_wait_completed_race_is_not_cancellation_success(self):
        pending={'id':'wait-id','command':'wait-change','document':'doc'}
        (self.bridge/'client-pending.json').write_text(json.dumps(pending))
        (self.bridge/'response.json').write_text(json.dumps({
            'id':'wait-id','ok':True,'execution_settled':True}))
        result=self.run_cli('--document','doc','cancel-wait','wait-id')
        self.assertEqual(result.returncode,1,result.stderr)
        self.assertTrue((self.bridge/'client-pending.json').exists())

    def test_cancel_wait_unsettled_response_retains_fence_and_unknown_outcome(self):
        pending={'id':'wait-id','command':'wait-change','document':'doc'}
        (self.bridge/'client-pending.json').write_text(json.dumps(pending))
        (self.bridge/'response.json').write_text(json.dumps({
            'id':'wait-id','ok':False,'execution_settled':False,'error':'wait_cancelled'}))
        result=self.run_cli('--timeout','0','--document','doc','cancel-wait','wait-id')
        self.assertEqual(result.returncode,2)
        self.assertIn('outcome unknown',result.stderr)
        self.assertEqual(json.loads((self.bridge/'client-pending.json').read_text()),pending)

    def test_tab_selection_updates_document_and_rejects_old_reference(self):
        tab='d12f382a-c0cf-4c2f-8836-5c22d3799adb'
        (self.bridge/'client-state.json').write_text(json.dumps({'document':'old-doc','revision':5}))
        thread=self.peer('select-tab',{'tab':tab,'document':'new-doc','revision':1,
                                      'truncated':False,'snapshot':'page @new-doc rev=1\n'})
        result=self.run_cli('select-tab',tab)
        self.join_peer(thread)
        self.assertEqual(result.returncode,0,result.stderr)
        request=json.loads((self.bridge/'request.json').read_text())
        self.assertEqual(request['tab'],tab)
        self.assertEqual(json.loads((self.bridge/'client-state.json').read_text())['document'],'new-doc')
        (self.bridge/'request.json').unlink()
        invalid=self.run_cli('--document','old-doc','click','old-doc_1')
        self.assertEqual(invalid.returncode,2)
        self.assertFalse((self.bridge/'request.json').exists())

    def test_tabs_preserves_expired_grant_and_truncation_metadata(self):
        tabs=[{'tab':'d12f382a-c0cf-4c2f-8836-5c22d3799adb','title':'Old title',
               'permission':'expired','metadata_truncated':True,'active':False}]
        thread=self.peer('tabs',{'tabs':tabs})
        result=self.run_cli('--compact','tabs');self.join_peer(thread)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['tabs'],tabs)

    def test_invalid_tab_capability_is_not_dispatched(self):
        for invalid in ('1','../tab','d12f382a-c0cf-4c2f-8836-5c22d3799adB',''):
            result=self.run_cli('select-tab',invalid)
            self.assertEqual(result.returncode,2)
            self.assertFalse((self.bridge/'request.json').exists())

    def test_compact_text_preserves_safety_metadata_without_nested_snapshot_json(self):
        response = {"document": "doc", "revision": 1, "scope": "main_document_visible_dom",
                    "viewport": {"width": 1440, "height": 900}, "truncated": True,
                    "snapshot": 'page @doc rev=1\n+@doc_1 text "A quoted label"\n',
                    "timing": {"user_wait_ms": 0}, "observation_bytes": 80}
        thread = self.peer("observe", response)
        result = self.run_cli("--compact", "--text", "observe", "--full")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0, result.stderr)
        header, snapshot = result.stdout.split("\n", 1)
        metadata = json.loads(header)
        self.assertEqual(metadata["document"], "doc")
        self.assertEqual(metadata["scope"], "main_document_visible_dom")
        self.assertTrue(metadata["truncated"])
        self.assertEqual(metadata["viewport"], response["viewport"])
        self.assertNotIn("timing", metadata)
        self.assertIn('+@1 text "A quoted label"', snapshot)
        self.assertNotIn('\\"A quoted label', snapshot)

    def test_compact_text_error_without_snapshot_stays_json(self):
        thread = self.peer("observe", {"ok": False, "error": "stale_document"})
        result = self.run_cli("--compact", "--text", "observe")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout), {"ok": False, "error": "stale_document"})

    def test_native_timing_is_recorded_but_not_sent_to_model(self):
        path = pathlib.Path(self.tmp.name) / "timed.jsonl"
        thread = self.peer("status", {"status": "idle", "timing": {"native_elapsed_ms": 50, "user_wait_ms": 40}})
        result = self.run_cli("--record", str(path), "status")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("timing", json.loads(result.stdout))
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        self.assertTrue(rows[0]["request"]["record_timing"])
        self.assertEqual(rows[1]["response"]["timing"]["user_wait_ms"], 40)

    def test_action_local_document_expands_ref(self):
        (self.bridge / "client-state.json").write_text(json.dumps({"document": "doc", "revision": 2}))
        for command in (("read", "4"), ("click", "4"), ("fill", "4", "Cedar")):
            thread = self.peer(command[0], {"ok": True})
            result = self.run_cli("--compact", *command, "--document", "doc")
            self.join_peer(thread)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads((self.bridge / "request.json").read_text())["ref"], "doc_4")
            (self.bridge / "request.json").unlink()

    def test_conflicting_document_options_never_dispatch(self):
        for argv in (("--document", "a", "click", "4", "--document", "b"),
                     ("--document", "a", "--document", "b", "click", "4"),
                     ("click", "4", "--document", "a", "--document", "b")):
            result = self.run_cli("--compact", *argv)
            self.assertEqual(result.returncode, 2)
            self.assertIn("conflicting --document", result.stderr)
            self.assertFalse((self.bridge / "request.json").exists())

    def test_opt_in_record_keeps_native_request_response_and_private_mode(self):
        path = pathlib.Path(self.tmp.name) / "transcript.jsonl"
        thread = self.peer("status", {"status": "idle"})
        result = self.run_cli("--record", str(path), "status")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        self.assertEqual([row["kind"] for row in rows], ["request", "response"])
        self.assertEqual(rows[1]["sequence"], rows[0]["sequence"] + 1)
        self.assertEqual(rows[0]["request"]["id"], rows[1]["response"]["id"])
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

    def test_record_never_overwrites_existing_file_or_dispatches(self):
        path = pathlib.Path(self.tmp.name) / "transcript.jsonl"
        path.write_text("keep")
        result = self.run_cli("--record", str(path), "status")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(path.read_text(), "keep")
        self.assertFalse((self.bridge / "request.json").exists())

    def test_compact_preserves_renderer_viewport_metadata(self):
        original = {"ok": True, "document": "doc", "viewport": {"width": 700, "height": 732},
                    "snapshot": "page @doc rev=1\n+@doc_1 text \"Article\"\n"}
        result = compact.compact_response(original)
        self.assertEqual(result["viewport"], original["viewport"])
        self.assertEqual(original["snapshot"], "page @doc rev=1\n+@doc_1 text \"Article\"\n")

    def test_compact_response_without_document_keeps_capability(self):
        result = compact.compact_response({"snapshot": "page @doc revision=1\n@doc_1 button label\n"})
        self.assertIn("capability document=doc\n", result["snapshot"])
        self.assertIn("@1 button label", result["snapshot"])

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.bridge = pathlib.Path(self.tmp.name) / "bridge"
        self.bridge.mkdir(mode=0o700)
        self.peer_errors = []

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, timeout=3):
        return subprocess.run(
            [sys.executable, str(CLI), "--bridge", str(self.bridge), "--timeout", str(timeout), *args],
            text=True, capture_output=True, timeout=timeout + 2,
        )

    def peer(self, expected_command, response_extra=None, delay=0):
        def work():
            try:
                deadline = time.time() + 2
                while time.time() < deadline and not (self.bridge / "request.json").exists():
                    time.sleep(.01)
                request = json.loads((self.bridge / "request.json").read_text())
                if request["command"] != expected_command:
                    raise AssertionError("unexpected command")
                if delay:
                    time.sleep(delay)
                response = {"id": request["id"], "ok": True}
                response.update(response_extra or {})
                temporary = self.bridge / ".peer-response"
                temporary.write_text(json.dumps(response, ensure_ascii=False, separators=(",", ":")))
                os.replace(temporary, self.bridge / "response.json")
            except BaseException as exc:
                self.peer_errors.append(exc)
        thread = threading.Thread(target=work)
        thread.start()
        return thread

    def join_peer(self, thread):
        thread.join()
        self.assertEqual(self.peer_errors, [])

    def test_pairing_and_arguments(self):
        thread = self.peer("fill", {"snapshot": "done"})
        result = self.run_cli("fill", "e1", "안녕")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout), {"id": json.loads((self.bridge / "response.json").read_text())["id"], "ok": True, "snapshot": "done"})
        request = json.loads((self.bridge / "request.json").read_text())
        self.assertEqual(request["value"], "안녕")

    def test_scroll_requires_document_and_allows_truncated_baseline_recovery(self):
        rejected = self.run_cli("scroll", "down")
        self.assertEqual(rejected.returncode, 2)
        self.assertFalse((self.bridge / "request.json").exists())
        thread = self.peer("scroll", {"scroll":{"y":640,"max_y":800,"moved":True}})
        result = self.run_cli("--compact", "--document", "doc", "scroll", "down", "2")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0, result.stderr)
        req = json.loads((self.bridge / "request.json").read_text())
        self.assertEqual((req["document"], req["direction"], req["pages"]), ("doc", "down", 2))
        self.assertEqual(json.loads(result.stdout)["scroll"]["max_y"], 800)

    def test_scroll_rejects_stale_acknowledged_document(self):
        (self.bridge / "client-state.json").write_text(json.dumps({"document":"current","revision":2}))
        result = self.run_cli("--document", "old", "scroll", "up")
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.bridge / "request.json").exists())

    def test_full_scroll_and_wait_keep_document_guards_in_cli_and_session(self):
        spec=importlib.util.spec_from_file_location('full_readonly_cli',CLI)
        cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
        config=cli.build_parser().parse_args(['--bridge',str(self.bridge),'observe'])
        baseline={'document':'doc','revision':7}
        for tokens in (['scroll','down','--full'],['scroll','up','2','--full'],
                       ['wait-change','100','--full'],['wait-change','100','--content','--full']):
            direct=cli.build_parser().parse_args(['--bridge',str(self.bridge),'--document','doc',*tokens])
            session=cli.session_command(['--document','doc',*tokens],config)
            for args in (direct,session):
                request=cli.request_for(args,baseline)
                self.assertTrue(request['full']);self.assertEqual(request['document'],'doc')
                with self.assertRaises(cli.BridgeError):cli.request_for(args,{'document':'other','revision':7})
        for tokens in (['scroll','down','--full','--full'],['wait-change','100','--full','--full']):
            with self.assertRaises(cli.BridgeError):cli.session_command(['--document','doc',*tokens],config)

    def test_wait_change_requires_baseline_and_sends_explicit_scope(self):
        for value in ('0', '30001', '100'):
            result = self.run_cli('--document', 'doc', 'wait-change', value)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((self.bridge/'request.json').exists())
        (self.bridge/'client-state.json').write_text(json.dumps({'document':'doc','revision':7}))
        thread = self.peer('wait-change', {'document':'doc','revision':8,
            'wait':{'reason':'limit','probes':2,'mechanism':'accessibility_events'}})
        result = self.run_cli('--document', 'doc', 'wait-change', '100')
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0, result.stderr)
        request = json.loads((self.bridge/'request.json').read_text())
        self.assertEqual((request['document'],request['wait_ms'],request['baseline_revision']),('doc',100,7))
        self.assertEqual(json.loads(result.stdout)['wait']['reason'],'limit')

    def test_read_sends_opaque_ref(self):
        thread = self.peer("read", {"ok": True, "text": "Name"})
        result = self.run_cli("read", "ref-17")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0)
        request = json.loads((self.bridge / "request.json").read_text())
        self.assertEqual(request["ref"], "ref-17")

    def test_snapshot_ack_is_sent_and_persisted(self):
        (self.bridge / "client-state.json").write_text(json.dumps({"document": "old", "revision": 4}))
        thread = self.peer("observe", {"ok": True, "document": "new", "revision": 5, "truncated": False})
        result = self.run_cli("observe")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0)
        request = json.loads((self.bridge / "request.json").read_text())
        self.assertEqual(request["baseline_document"], "old")
        self.assertEqual(request["baseline_revision"], 4)
        self.assertEqual(json.loads((self.bridge / "client-state.json").read_text()), {"document": "new", "revision": 5})

    def test_full_observe_skips_ack_and_truncated_clears_state(self):
        (self.bridge / "client-state.json").write_text(json.dumps({"document": "old", "revision": 4}))
        thread = self.peer("observe", {"ok": True, "document": "large", "revision": 6, "truncated": True})
        result = self.run_cli("observe", "--full")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0)
        request = json.loads((self.bridge / "request.json").read_text())
        self.assertNotIn("baseline_document", request)
        self.assertEqual(json.loads((self.bridge / "client-state.json").read_text()), {})

    def test_malformed_and_other_response_are_ignored(self):
        (self.bridge / "response.json").write_text("not json")
        def peer():
            while not (self.bridge / "request.json").exists():
                time.sleep(.01)
            request = json.loads((self.bridge / "request.json").read_text())
            (self.bridge / "response.json").write_text(json.dumps({"id": "other", "ok": True}))
            time.sleep(.05)
            (self.bridge / "response.json").write_text(json.dumps({"id": request["id"], "ok": True, "status": "idle"}))
        thread = threading.Thread(target=peer); thread.start()
        result = self.run_cli("status")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)["status"], "idle")

    def test_named_timeout_records_one_failure_across_recursive_frames(self):
        log = self.bridge / "named-timeout.jsonl"
        thread = self.peer("observe", {"document":"doc", "revision":1, "truncated":False,
                                      "snapshot":'page @doc rev=1\n+@doc_1 field "Name" value=""\n'})
        result = self.run_cli("--record", str(log), "fill-named", "Name", "Cedar", timeout=.2)
        self.join_peer(thread)
        self.assertEqual(result.returncode, 2, result.stderr)
        rows = [json.loads(line) for line in log.read_text().splitlines()]
        self.assertEqual([r["request"]["command"] for r in rows if r["kind"] == "request"], ["observe", "fill"])
        self.assertEqual(sum(r["kind"] == "client_error" for r in rows), 1)
        self.assertTrue((self.bridge / "client-pending.json").exists())

    def test_launcher_peer_exit_retains_uncertain_request_and_recovers_archive(self):
        peer = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
        process = subprocess.Popen([sys.executable, str(CLI), '--bridge', str(self.bridge),
            '--peer-pid', str(peer.pid), '--timeout', '10', 'ask', 'wait'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 3
            while not (self.bridge/'request.json').exists() and time.monotonic() < deadline:
                time.sleep(.01)
            request = json.loads((self.bridge/'request.json').read_text())
            peer.terminate(); peer.wait(timeout=3)
            stdout, stderr = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 2)
            self.assertIn('browser_disconnected', stderr)
            self.assertEqual(json.loads((self.bridge/'client-pending.json').read_text())['id'], request['id'])
            self.assertEqual(self.run_cli('observe').returncode, 2)
            archived = self.bridge / ('native-request-' + request['id'].encode().hex().upper() + '.result')
            archived.write_text(json.dumps({'id': request['id'], 'ok': True, 'execution_settled': True, 'answer':'done'}))
            result = self.run_cli('--peer-pid', str(peer.pid), 'recover')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['answer'], 'done')
            self.assertFalse((self.bridge/'client-pending.json').exists())
        finally:
            if peer.poll() is None: peer.terminate()
            peer.wait()
            if process.poll() is None: process.kill()
            process.communicate()

    def test_dead_peer_before_dispatch_writes_no_request(self):
        peer = subprocess.Popen([sys.executable, '-c', 'pass']); peer.wait()
        result = self.run_cli('--peer-pid', str(peer.pid), 'observe')
        self.assertEqual(result.returncode, 2)
        self.assertIn('before dispatch', result.stderr)
        self.assertFalse((self.bridge/'request.json').exists())

    def test_timeout_preserves_request_and_blocks_overwrite(self):
        result = self.run_cli("ask", "wait", timeout=.15)
        self.assertEqual(result.returncode, 2)
        self.assertIn("timed out", result.stderr)
        original = (self.bridge / "request.json").read_bytes()
        pending = json.loads((self.bridge / "client-pending.json").read_text())
        self.assertEqual(pending["id"], json.loads(original)["id"])
        for command in (("status",), ("cancel",), ("fill-named", "Name", "duplicate")):
            blocked = self.run_cli(*command)
            self.assertEqual(blocked.returncode, 2)
            self.assertIn("unresolved request", blocked.stderr)
            self.assertEqual((self.bridge / "request.json").read_bytes(), original)
        (self.bridge / "response.json").write_text(json.dumps({"id": pending["id"], "ok": True, "answer": "done"}))
        recovered = self.run_cli("recover")
        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        self.assertEqual(json.loads(recovered.stdout)["answer"], "done")
        self.assertFalse((self.bridge / "client-pending.json").exists())
        self.assertEqual((self.bridge / "request.json").read_bytes(), original)

    def test_settled_native_expiry_releases_fence_without_retry(self):
        thread = self.peer("click", {"ok": False, "error": "request_expired", "execution_settled": True})
        result = self.run_cli("click", "ref")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse((self.bridge / "client-pending.json").exists())

    def test_explicit_unsettled_result_keeps_fence_even_without_error(self):
        thread = self.peer("click", {"ok": True, "execution_settled": False})
        result = self.run_cli("click", "ref")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 2)
        self.assertTrue((self.bridge / "client-pending.json").exists())

    def test_client_deadline_covers_transport_wait(self):
        started = time.time()
        result = self.run_cli("status", timeout=.1)
        self.assertEqual(result.returncode, 2)
        request = json.loads((self.bridge / "client-pending.json").read_text())
        self.assertGreaterEqual(request["expires_unix_ms"], started * 1000)
        self.assertLessEqual(request["expires_unix_ms"], time.time() * 1000 + 100)
        self.assertEqual(request["timeout_ms"], 120000)

    def test_uncertain_native_expiry_keeps_fence(self):
        thread = self.peer("click", {"ok": False, "error": "request_expired"})
        result = self.run_cli("click", "ref")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 2)
        recovered = self.run_cli("recover")
        self.assertEqual(recovered.returncode, 2)
        self.assertIn("uncertain execution outcome", recovered.stderr)
        self.assertTrue((self.bridge / "client-pending.json").exists())

    def test_process_death_after_publication_preserves_fence(self):
        process = subprocess.Popen([sys.executable, str(CLI), "--bridge", str(self.bridge),
                                    "ask", "wait"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 3
            while not (self.bridge / "request.json").exists() and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue((self.bridge / "request.json").exists())
            original = (self.bridge / "request.json").read_bytes()
            process.kill()
            process.communicate(timeout=3)
            blocked = self.run_cli("status")
            self.assertEqual(blocked.returncode, 2)
            self.assertIn("unresolved request", blocked.stderr)
            self.assertEqual((self.bridge / "request.json").read_bytes(), original)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    def test_recover_rejects_other_or_incomplete_response(self):
        self.run_cli("ask", "wait", timeout=.01)
        pending = (self.bridge / "client-pending.json").read_bytes()
        for response in ({"id": "other", "ok": True}, {"id": json.loads(pending)["id"]}):
            (self.bridge / "response.json").write_text(json.dumps(response))
            result = self.run_cli("recover", timeout=.01)
            self.assertEqual(result.returncode, 2)
            self.assertEqual((self.bridge / "client-pending.json").read_bytes(), pending)

    def test_recover_archived_result_without_republishing_lost_response(self):
        self.run_cli("status", timeout=.01)
        pending = json.loads((self.bridge / "client-pending.json").read_text())
        (self.bridge / "request.json").unlink()  # Native has consumed it.
        response = {"id": pending["id"], "ok": True, "execution_settled": True,
                    "attached": False}
        archive = self.bridge / ("native-request-" + pending["id"].encode().hex().upper() + ".result")
        archive.write_text(json.dumps(response))
        log = self.bridge / "recovery-log.jsonl"
        result = self.run_cli("--record", str(log), "recover")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), response)
        self.assertFalse((self.bridge / "request.json").exists())
        self.assertFalse((self.bridge / "client-pending.json").exists())
        rows = [json.loads(line) for line in log.read_text().splitlines()]
        self.assertFalse(any(row["kind"] == "request" for row in rows))
        self.assertEqual(next(row for row in rows if row["kind"] == "response")["response_source"],
                         "native_archive")

    def test_incomplete_archive_does_not_clear_pending_fence(self):
        self.run_cli("status", timeout=.01)
        pending = json.loads((self.bridge / "client-pending.json").read_text())
        archive = self.bridge / ("native-request-" + pending["id"].encode().hex().upper() + ".result")
        for result in ({"id":pending["id"], "ok":True},
                       {"id":pending["id"], "ok":False, "execution_settled":False},
                       {"id":"different", "ok":True, "execution_settled":True}):
            archive.write_text(json.dumps(result))
            self.assertEqual(self.run_cli("recover", timeout=.01).returncode, 2)
            self.assertTrue((self.bridge / "client-pending.json").exists())

    def test_unacknowledged_prejournal_mailbox_is_adopted_without_overwrite(self):
        orphan = {"id": "legacy-request", "command": "click", "ref": "old"}
        (self.bridge / "request.json").write_text(json.dumps(orphan))
        original = (self.bridge / "request.json").read_bytes()
        blocked = self.run_cli("status")
        self.assertEqual(blocked.returncode, 2)
        self.assertIn("unresolved request legacy-request", blocked.stderr)
        self.assertEqual(json.loads((self.bridge / "client-pending.json").read_text()), orphan)
        self.assertEqual((self.bridge / "request.json").read_bytes(), original)

    def test_corrupt_pending_journal_fails_closed(self):
        (self.bridge / "client-pending.json").write_text("broken")
        result = self.run_cli("status")
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.bridge / "request.json").exists())

    def test_invalid_request_timeout_writes_nothing(self):
        result = subprocess.run([sys.executable, str(CLI), "--bridge", str(self.bridge),
                                 "--request-timeout", "0", "status"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.bridge / "request.json").exists())

    def test_private_directory_and_no_writes_outside_fixture(self):
        public = pathlib.Path(self.tmp.name) / "public"
        public.mkdir(mode=0o755)
        result = subprocess.run([sys.executable, str(CLI), "--bridge", str(public), "status"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse((public / "request.json").exists())
        link = pathlib.Path(self.tmp.name) / "link"
        link.symlink_to(self.bridge, target_is_directory=True)
        result = subprocess.run([sys.executable, str(CLI), "--bridge", str(link), "status"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_compact_formatter_keeps_capability_and_opaque_refs(self):
        document = "doc-uuid"
        snapshot = ('page @doc-uuid rev=7 title="Long title" origin="file:" delta\n'
                    'base_rev=6\n+@doc-uuid_4 button "Save"\n'
                    '~@doc-uuid_12 field "A very long static label"\n'
                    '-@opaque/ref\n')
        shown = compact.format_compact_snapshot(snapshot, document)
        self.assertIn("capability document=doc-uuid\npage rev=7", shown)
        self.assertIn("@4 button", shown)
        self.assertIn("@12 field \"A very long static label\"", shown)
        self.assertIn("@opaque/ref", shown)
        self.assertEqual(shown.count("doc-uuid"), 1)

    def test_compact_formatter_does_not_rewrite_literal_refs_in_labels(self):
        snapshot = ('page @doc-uuid rev=7 title="x" origin="file:"\n'
                    '+@doc-uuid_4 button "literal @doc-uuid_4" value="@doc-uuid_12"\n')
        shown = compact.format_compact_snapshot(snapshot, "doc-uuid")
        self.assertIn('+@4 button "literal @doc-uuid_4" value="@doc-uuid_12"', shown)

    def test_compact_formatter_fails_closed_on_document_mismatch(self):
        snapshot = 'page @doc-a rev=1 title="x" origin="file:"\n+@doc-a_4 button "Save"\n'
        self.assertEqual(compact.format_compact_snapshot(snapshot, "doc-b"), snapshot)

    def test_compact_json_keeps_document_once_as_envelope_metadata(self):
        response = {
            "id": "request", "ok": True, "document": "doc-a",
            "observation_bytes": 99,
            "snapshot": 'page @doc-a rev=1 title="x" origin="file:"\n+@doc-a_4 button "Save"\n',
        }
        shown = compact.compact_response(response)
        self.assertNotIn("capability document=", shown["snapshot"])
        self.assertIn("page rev=1", shown["snapshot"])
        self.assertIn("@4 button", shown["snapshot"])
        self.assertEqual(shown["document"], "doc-a")

    def test_compact_ref_round_trip_and_malformed_refs(self):
        document = "doc-uuid"
        for native in ("doc-uuid_1", "@doc-uuid_99"):
            shown = compact.compact_ref(native, document)
            self.assertEqual(compact.native_ref(shown, document), native.lstrip("@"))
        self.assertEqual(compact.compact_ref("doc-uuid_0", document), "doc-uuid_0")
        self.assertEqual(compact.compact_ref("doc-uuid_01", document), "doc-uuid_01")
        self.assertEqual(compact.compact_ref("doc-uuid_x", document), "doc-uuid_x")

    def test_compact_action_requires_state_and_matching_capability(self):
        missing = self.run_cli("--compact", "click", "@4")
        self.assertEqual(missing.returncode, 2)
        self.assertIn("acknowledged client state", missing.stderr)
        (self.bridge / "client-state.json").write_text(json.dumps({"document": "doc", "revision": 2}))
        mismatch = self.run_cli("--compact", "--document", "other", "click", "@4")
        self.assertEqual(mismatch.returncode, 2)
        self.assertIn("does not match", mismatch.stderr)

    def test_compact_action_expands_numeric_ref_before_transport(self):
        (self.bridge / "client-state.json").write_text(json.dumps({"document": "doc", "revision": 2}))
        thread = self.peer("click", {"ok": True, "error": "approval_required"})
        result = self.run_cli("--compact", "--document", "doc", "click", "@4")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 0)
        request = json.loads((self.bridge / "request.json").read_text())
        self.assertEqual(request["ref"], "doc_4")

    def test_compact_json_strips_transport_fields_but_preserves_error(self):
        thread = self.peer("status", {"ok": False, "error": "stale_document", "observation_bytes": 17})
        result = self.run_cli("--compact", "status")
        self.join_peer(thread)
        self.assertEqual(result.returncode, 1)
        shown = json.loads(result.stdout)
        self.assertEqual(shown, {"ok": False, "error": "stale_document"})


if __name__ == "__main__":
    unittest.main()
