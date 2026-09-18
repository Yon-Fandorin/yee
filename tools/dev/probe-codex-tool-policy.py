#!/usr/bin/env python3
"""Exercise installed Codex against synthetic loopback Responses and MCP servers.

No paid model, real browser, or provider credentials. Reported usage is fixture
data. Original Codex configuration and permissions are not modified.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fixture = load_module('kimi_policy_fixture', 'probe-kimi-tool-policy.py')


def toml_value(value):
    if isinstance(value, dict):
        return '{' + ', '.join(json.dumps(k) + '=' + toml_value(v) for k, v in value.items()) + '}'
    if isinstance(value, list):
        return '[' + ', '.join(toml_value(v) for v in value) + ']'
    return json.dumps(value)


def offered_tools(request):
    roots = list(request.get('tools', []))
    for item in request.get('input', []):
        if isinstance(item, dict) and item.get('type') == 'additional_tools':
            roots.extend(item.get('tools', []))
    def flatten(items, prefix=''):
        for item in items:
            name = prefix + item.get('name', item.get('type', 'unknown'))
            if item.get('type') == 'namespace':
                yield from flatten(item.get('tools', []), name + '.')
            else:
                yield name
    return sorted(set(flatten(roots)))


def evaluate(result):
    """Separate a successful plumbing probe from a browser-only policy proof."""
    calls = result['mcp_calls']
    outputs = {o['call_id']: o['output'] for o in result['tool_outputs']}
    blocks = outputs.get('allowed_call', [])
    texts = [b.get('text', '') for b in blocks if isinstance(b, dict)] if isinstance(blocks, list) else []
    inventory = []
    allowed = None
    shell_denied = False
    for text in texts:
        try:
            value = json.loads(text)
        except (ValueError, TypeError):
            continue
        if isinstance(value, dict):
            inventory = value.get('inventory', inventory)
            allowed = value.get('allowed', allowed)
            shell_denied |= value.get('shell_denial') == 'TypeError: tools.exec_command is not a function'
    call_matches = (len(calls) == 1 and calls[0].get('name') == 'yee_browser'
                    and calls[0].get('arguments') == {'commands': [['status']]})
    allowed_matches = allowed == {'content': [{'type': 'text', 'text': 'SYNTHETIC_YEE_TOOL_EXECUTED'}]}
    patch_denied = any('patch rejected: writing is blocked by read-only sandbox' in t for t in texts)
    expected_usage = {'input_tokens': 22, 'cached_input_tokens': 4, 'cache_write_input_tokens': 0,
                      'output_tokens': 6, 'reasoning_output_tokens': 0}
    usage_matches = result['usage_events'] == [{'type': 'turn.completed', 'usage': expected_usage}]
    lifecycle = result['lifecycle']
    clean_exit = lifecycle['returncode'] == 0 and not any(lifecycle[k] for k in (
        'timed_out', 'interrupted', 'group_cleanup_requested', 'descendants_after_normal_exit'))
    plumbing = (result['provider_requests'] == 2 and len(result['tool_outputs']) == 2
                and call_matches and allowed_matches and shell_denied and patch_denied
                and outputs.get('forbidden_call') == 'unsupported call: exec_command'
                and not result['canary_created'] and not result['patch_canary_created']
                and usage_matches and clean_exit)
    bounded_inventory = {'apply_patch', 'list_mcp_resource_templates', 'list_mcp_resources',
                         'mcp__yee__yee_browser', 'read_mcp_resource'}
    bounded = plumbing and set(inventory) == bounded_inventory
    return {'plumbing_probe_pass': plumbing, 'nested_tools': inventory,
            'synthetic_usage_aggregation_verified': usage_matches,
            'browser_only_tool_inventory': inventory == ['mcp__yee__yee_browser'],
            'bounded_runtime_verified': bounded,
            'actual_trial_ready': False,
            'remaining_gate': ('Runtime verified; real fixture, native scope and external model authorization required. '
                               'Retained skills and helpers must be included in usage.' if bounded else
                               'Unexpected nested tools remain; bounded runtime not verified.')}


def run(args):
    root = args.record
    if not root.is_absolute():
        raise ValueError('record directory must be absolute and new')
    root.mkdir(mode=0o700)
    workspace = root / 'workspace'
    workspace.mkdir(mode=0o700)
    env = {k: v for k, v in os.environ.items()
           if k in ('PATH', 'HOME', 'USER', 'TMPDIR', 'SHELL', 'LANG')}
    policy = root / 'loopback.sb'
    policy.write_text('(version 1)\n(allow default)\n(deny network*)\n'
                      '(allow network-outbound (remote ip "localhost:*"))\n'
                      '(allow network-inbound (local ip "localhost:*"))\n'
                      '(allow network* (local unix-socket) (remote unix-socket))\n')
    policy.chmod(0o600)
    prefix = ['/usr/bin/sandbox-exec', '-f', str(policy)]
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'LOCAL_PROBE')

        def do_POST(self):
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size < 4 * 1024 * 1024:
                self.send_error(413)
                return
            body = json.loads(self.rfile.read(size))
            requests.append(body)
            count = len(requests)
            fixture.private_json(root / f'provider-request-{count}.json', body)
            if count > 3:
                self.send_error(429)
                return
            if count == 1:
                output = [
                    {'type': 'custom_tool_call', 'id': 'fc_allowed', 'call_id': 'allowed_call',
                     'name': 'exec', 'namespace': 'functions',
                     'input': ('text({inventory:ALL_TOOLS.map(t=>t.name)}); '
                               'const t=ALL_TOOLS.find(t=>t.name.endsWith("yee_browser")); '
                               'if(t) text({allowed:await tools[t.name]({commands:[["status"]]})}); '
                               'try {text(await tools.exec_command({cmd:"touch forbidden-canary"}));} '
                               'catch(e) {text({shell_denial:String(e)});} '
                               'text({patch:await tools.apply_patch('
                               + json.dumps('*** Begin Patch\n*** Add File: ' + str(workspace/'patch-canary')
                                            + '\n+synthetic\n*** End Patch') + ')});'),
                     'status': 'completed'},
                    {'type': 'function_call', 'id': 'fc_forbidden', 'call_id': 'forbidden_call',
                     'name': 'exec_command', 'arguments': '{"cmd":"touch forbidden-canary"}',
                     'status': 'completed'}]
            else:
                output = [{'type': 'message', 'id': 'msg_final', 'role': 'assistant',
                           'status': 'completed', 'content': [{'type': 'output_text',
                           'text': 'Synthetic policy probe finished.', 'annotations': []}]}]
            response = {'id': f'resp_{count}', 'object': 'response', 'status': 'completed',
                        'model': 'gpt-5.6-luna', 'output': output,
                        'usage': {'input_tokens': 11, 'output_tokens': 3, 'total_tokens': 14,
                                  'input_tokens_details': {'cached_tokens': 2}}}
            events = [{'type': 'response.created', 'response': {
                **response, 'status': 'in_progress', 'output': []}}]
            for index, item in enumerate(output):
                events.extend([
                    {'type': 'response.output_item.added', 'output_index': index, 'item': item},
                    {'type': 'response.output_item.done', 'output_index': index, 'item': item}])
            events.append({'type': 'response.completed', 'response': response})
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.end_headers()
            for sequence, event in enumerate(events):
                event['sequence_number'] = sequence
                self.wfile.write(('event: ' + event['type'] + '\ndata: '
                                  + json.dumps(event) + '\n\n').encode())
            self.wfile.flush()

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        origin = f'http://127.0.0.1:{server.server_port}'
        netcode = ('import socket,urllib.request\n'
                   f'assert urllib.request.urlopen({origin!r}).read()==b"LOCAL_PROBE"\n'
                   's=socket.socket();s.settimeout(1)\n'
                   'try:s.connect(("203.0.113.1",443))\n'
                   'except PermissionError:print("LOOPBACK_ONLY_VERIFIED")\n'
                   'else:raise RuntimeError("external network was not denied")\n')
        check = subprocess.run(prefix + [sys.executable, '-c', netcode], env=env,
                               capture_output=True, text=True, timeout=10)
        fixture.private_json(root / 'network-check.json', {
            'code': check.returncode, 'stdout': check.stdout, 'stderr': check.stderr})
        if check.returncode or 'LOOPBACK_ONLY_VERIFIED' not in check.stdout:
            raise RuntimeError('network boundary failed; Codex was not launched')
        command = prefix + [str(args.codex), 'exec', '--ignore-user-config', '--ephemeral',
                           '--strict-config', '--sandbox', 'read-only', '--skip-git-repo-check',
                           '--json', '--cd', str(workspace), '-m', 'gpt-5.6-luna']
        config = {
            'model_provider': 'probe', 'model_providers.probe.name': 'Synthetic loopback probe',
            'model_providers.probe.base_url': origin,
            'model_providers.probe.wire_api': 'responses',
            'model_providers.probe.requires_openai_auth': False,
            'model_providers.probe.request_max_retries': 0,
            'model_providers.probe.stream_max_retries': 0,
            'model_providers.probe.stream_idle_timeout_ms': 10000,
            'model_providers.probe.supports_websockets': False,
            'web_search': 'disabled', 'project_doc_max_bytes': 0,
            'agents.enabled': False,
            'skills.config': [{'path': str(p.parent), 'enabled': False}
                              for base in (Path.home()/'.codex/skills', Path.home()/'.agents/skills')
                              for p in sorted(base.rglob('SKILL.md'))],
            'features.skip_host_skill_discovery': True,
            'features.code_mode_only': False,
            f'projects.{json.dumps(str(workspace))}.trust_level': 'trusted',
            'mcp_servers.yee.enabled': True,
            'mcp_servers.yee.command': sys.executable,
            'mcp_servers.yee.args': [str(Path(__file__).resolve()), '--mcp-log', str(root/'mcp.jsonl')],
            'mcp_servers.yee.enabled_tools': ['yee_browser'],
            'mcp_servers.yee.tools.yee_browser.approval_mode': 'approve',
        }
        for feature in ('shell_tool', 'unified_exec', 'apps', 'plugins', 'remote_plugin',
                        'browser_use', 'browser_use_external', 'computer_use', 'in_app_browser',
                        'image_generation', 'multi_agent', 'multi_agent_v2', 'hooks', 'goals',
                        'code_mode', 'view_image', 'sleep_tool',
                        'skill_mcp_dependency_install', 'skill_search'):
            config[f'features.{feature}'] = False
        for key, value in config.items():
            command += ['-c', key + '=' + toml_value(value)]
        command += ['Call the synthetic Yee MCP status tool once, then finish. Do not use files or shell.']
        fixture.private_json(root/'manifest.json', {
            'synthetic_backend': True, 'actual_model_usage': False, 'command': command,
            'codex_sha256': hashlib.sha256(args.codex.read_bytes()).hexdigest(),
            'probe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()})
        supervisor = load_module('codex_probe_supervisor', 'run-kimi-recorded.py')
        with (root/'stdout.jsonl').open('xb') as out, (root/'stderr.txt').open('xb') as err:
            os.chmod(out.name, 0o600)
            os.chmod(err.name, 0o600)
            lifecycle = supervisor.execute(command, env=env, cwd=workspace, timeout=60,
                                           stdout=out, stderr=err)
        fixture.private_json(root/'lifecycle.json', lifecycle)
        events = [json.loads(s) for s in (root/'stdout.jsonl').read_text().splitlines() if s.strip()]
        mcp_events = [json.loads(s) for s in (root/'mcp.jsonl').read_text().splitlines()] if (root/'mcp.jsonl').exists() else []
        offered = sorted({name for r in requests for name in offered_tools(r)})
        outputs = [i for r in requests[1:] for i in r.get('input', [])
                   if isinstance(i, dict) and i.get('type') in ('function_call_output', 'custom_tool_call_output')]
        result = {'synthetic_backend': True, 'actual_model_usage': False,
                  'provider_requests': len(requests), 'offered_tools': offered,
                  'mcp_calls': [e['params'] for e in mcp_events if e.get('method') == 'tools/call'],
                  'tool_outputs': outputs, 'canary_created': (workspace/'forbidden-canary').exists(),
                  'patch_canary_created': (workspace/'patch-canary').exists(),
                  'usage_events': [e for e in events if e.get('type') == 'turn.completed'],
                  'lifecycle': lifecycle}
        result['evaluation'] = evaluate(result)
        fixture.private_json(root/'result.json', result)
        print(json.dumps(result))
        if not result['evaluation']['plumbing_probe_pass']:
            raise RuntimeError('synthetic plumbing probe failed; inspect retained raw evidence')
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', type=Path)
    parser.add_argument('--codex', type=Path, default=Path('/Users/yongjunkim/.local/bin/codex'))
    parser.add_argument('--mcp-log', type=Path)
    arguments = parser.parse_args()
    if arguments.mcp_log:
        fixture.mcp(arguments.mcp_log)
    elif arguments.record:
        run(arguments)
    else:
        parser.error('--record is required')
