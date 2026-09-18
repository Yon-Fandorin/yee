"""Recorded, sequential Grok ACP client for a continuously live model/MCP session.

This is caller-side measurement code, not a browser service. Whole-prompt usage
is normalized separately; transport timings do not include browser preparation.
"""
import json
import os
import queue
import signal
import subprocess
import threading
import time


class AcpSession:
    def __init__(self, command, cwd, event_stream, stderr_stream, *, authorized_mcp_tools=()):
        self.events = event_stream
        self.started_ns = time.monotonic_ns()
        self.session_id = None
        self.next_id = 1
        self.closed = False
        self.broken = False
        self.responses = queue.Queue()
        self.permission_requests = 0
        self.permission_approvals = 0
        self.authorized_mcp_tools = frozenset(authorized_mcp_tools)
        self.permission_tool_calls = set()
        self.process = subprocess.Popen(
            command, cwd=cwd, env={**os.environ, 'GROK_SUBAGENTS': '0'},
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr_stream,
            text=True, bufsize=1, start_new_session=True)
        self._record('lifecycle', {'kind': 'agent_started', 'pid': self.process.pid,
                                   'started_monotonic_ns': self.started_ns})
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _record(self, direction, value):
        self.events.write(json.dumps({'direction': direction,
                                      'monotonic_ns': time.monotonic_ns(),
                                      'time_ns': time.time_ns(), 'value': value}) + '\n')
        self.events.flush()

    def _read(self):
        try:
            for line in self.process.stdout:
                try:
                    value = json.loads(line)
                    if not isinstance(value, dict):
                        raise ValueError('object required')
                    self.responses.put((time.monotonic_ns(), value))
                except ValueError:
                    self.responses.put((time.monotonic_ns(), {'transport_error': 'non-JSON ACP frame'}))
        finally:
            self.responses.put((time.monotonic_ns(), {'transport_eof': True}))

    def _send(self, value):
        self._record('request', value)
        self.process.stdin.write(json.dumps(value) + '\n')
        self.process.stdin.flush()

    def _client_request(self, value):
        identity = value['id']
        if value['method'] == 'session/request_permission':
            self.permission_requests += 1
            result = {'outcome': {'outcome': 'cancelled'}}
            params = value.get('params', {})
            if not isinstance(params, dict):params = {}
            tool = params.get('toolCall', {})
            if not isinstance(tool, dict):tool = {}
            raw = tool.get('rawInput', {})
            tool_meta = tool.get('_meta', {})
            metadata = tool_meta.get('x.ai/tool', {}) if isinstance(tool_meta, dict) else {}
            if not isinstance(metadata, dict):metadata = {}
            options = params.get('options', [])
            if not isinstance(options, list):options = []
            once = [o for o in options if isinstance(o, dict) and o.get('kind') == 'allow_once']
            call_id = tool.get('toolCallId')
            name = raw.get('tool_name') if isinstance(raw, dict) else None
            # Only the explicitly declared MCP tool may be selected once.
            # Browser task/document consent remains a separate Native gate.
            permitted = (not self.broken and not self.closed and self.session_id is not None
                and params.get('sessionId') == self.session_id
                and isinstance(call_id, str) and bool(call_id) and call_id not in self.permission_tool_calls
                and isinstance(raw, dict) and set(raw) == {'variant', 'tool_name', 'tool_input'}
                and raw.get('variant') == 'UseTool' and isinstance(name, str)
                and name in self.authorized_mcp_tools and tool.get('title') == name
                and isinstance(raw.get('tool_input'), dict)
                and type(metadata.get('version')) is int and metadata['version'] == 1 and metadata.get('name') == 'use_tool'
                and metadata.get('kind') == 'use_tool' and len(once) == 1
                and isinstance(once[0].get('optionId'), str) and bool(once[0]['optionId'])
                and sum(isinstance(o, dict) and o.get('optionId') == once[0]['optionId'] for o in options) == 1)
            if isinstance(call_id, str):self.permission_tool_calls.add(call_id)
            if permitted:
                result = {'outcome': {'outcome': 'selected', 'optionId': once[0]['optionId']}}
                self.permission_approvals += 1
            self._record('permission_decision', {'request_id': identity, 'tool_call_id': call_id,
                'session_id': params.get('sessionId'), 'tool_name': name,
                'approved': permitted, 'scope': 'declared MCP tool once; Native consent unchanged'})
            self._send({'jsonrpc': '2.0', 'id': identity, 'result': result})
        else:
            self._send({'jsonrpc': '2.0', 'id': identity,
                        'error': {'code': -32601, 'message': 'Client capability unavailable'}})

    def _receive(self, identity, timeout):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('ACP response deadline exceeded')
            try:
                received_ns, value = self.responses.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError('ACP response deadline exceeded') from exc
            self._record('response', {'received_monotonic_ns': received_ns, 'frame': value})
            if value.get('transport_eof') or value.get('transport_error'):
                raise RuntimeError('ACP transport ended or returned an invalid frame')
            if 'method' in value:
                if 'id' in value:
                    self._client_request(value)
                params = value.get('params', {})
                if (value['method'] == 'session/update' and self.session_id is not None
                        and params.get('sessionId') != self.session_id):
                    raise ValueError('foreign ACP session update')
                continue
            if value.get('id') != identity:
                raise ValueError('unexpected or replayed ACP response')
            if 'error' in value:
                raise RuntimeError('ACP request failed')
            if not isinstance(value.get('result'), dict):
                raise ValueError('ACP result object required')
            return received_ns, value

    def request(self, method, params, timeout=90):
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or timeout <= 0:
            raise ValueError('positive ACP timeout required')
        if self.closed or self.broken or self.process.poll() is not None:
            raise RuntimeError('ACP session is unavailable; request was not replayed')
        identity = self.next_id
        self.next_id += 1
        started = time.monotonic_ns()
        try:
            self._send({'jsonrpc': '2.0', 'id': identity, 'method': method, 'params': params})
            received_ns, response = self._receive(identity, timeout)
        except Exception as exc:
            self.broken = True
            failure = {'rpc_id': identity, 'method': method, 'session_id': self.session_id,
                       'started_monotonic_ns': started, 'failed_monotonic_ns': time.monotonic_ns(),
                       'timeout_seconds': timeout, 'error_type': type(exc).__name__,
                       'success': False, 'session_unavailable': True,
                       'cancel_sent': False, 'original_late_response': None}
            if method == 'session/prompt' and self.session_id is not None:
                try:
                    self._send({'jsonrpc': '2.0', 'method': 'session/cancel',
                                'params': {'sessionId': self.session_id}})
                    failure['cancel_sent'] = True
                    received_ns, response = self._receive(identity, 5)
                    failure['original_late_response'] = {
                        'received_monotonic_ns': received_ns, 'response': response}
                except Exception:
                    pass
            # Retain cancelled/late usage without turning a failed request into
            # success. The broken session still rejects every subsequent call.
            self._record('request_failed', failure)
            raise
        return {'rpc_id': identity, 'started_monotonic_ns': started,
                'received_monotonic_ns': received_ns,
                'elapsed_seconds': (received_ns - started) / 1e9, 'response': response}

    def initialize(self, cwd, mcp_servers, timeout=90):
        if self.session_id is not None:
            raise ValueError('continuous session already initialized')
        handshake = self.request('initialize', {
            'protocolVersion': 1,
            'clientCapabilities': {'fs': {'readTextFile': False, 'writeTextFile': False},
                                   'terminal': False},
            'clientInfo': {'name': 'yee-recorded-continuous-client', 'version': '1'}}, timeout)
        if handshake['response']['result'].get('protocolVersion') != 1:
            self.broken = True
            raise ValueError('unsupported ACP version')
        created = self.request('session/new', {'cwd': str(cwd), 'mcpServers': mcp_servers,
                                              '_meta': {'yoloMode': False, 'autoMode': False}}, timeout)
        identity = created['response']['result'].get('sessionId')
        if not isinstance(identity, str) or not identity:
            self.broken = True
            raise ValueError('ACP session identity required')
        self.session_id = identity
        return {'initialize': handshake, 'new': created,
                'agent_and_mcp_setup_seconds': (created['received_monotonic_ns'] - self.started_ns) / 1e9}

    def prompt(self, text, timeout=240):
        if self.session_id is None:
            raise RuntimeError('ACP session must be initialized')
        if not isinstance(text, str) or not text.strip():
            raise ValueError('nonempty ACP prompt required')
        return self.request('session/prompt', {'sessionId': self.session_id,
                                              'prompt': [{'type': 'text', 'text': text}]}, timeout)

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=3)
        self.reader.join(timeout=1)
        self.process.stdout.close()
        self._record('lifecycle', {'kind': 'agent_closed', 'returncode': self.process.returncode,
                                   'broken': self.broken,
                                   'agent_lifetime_seconds': (time.monotonic_ns() - self.started_ns) / 1e9})

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()
