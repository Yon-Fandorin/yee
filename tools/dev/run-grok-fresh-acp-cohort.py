#!/usr/bin/env python3
"""All twelve original tasks with fresh ACP/MCP connections and inclusive timing.

Uses the original cold browser preparation/verification, with the same recorded
ACP transport, high effort and audited twelve-turn semantics as the warm caller.
"""
import argparse
import asyncio
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import tomllib

from grok_acp_session import AcpSession
from grok_acp_events import inspect_prompt
from grok_acp_usage import normalize, FIELDS

DEV = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('original_cold', DEV/'run-grok-scenario-cohort.py')
c = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = c
spec.loader.exec_module(c)
spec = importlib.util.spec_from_file_location('native_audit', DEV/'inspect-yee-mcp-calls.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def lines(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def save_current(root, browser, case):
    """Replace only the mutable progress pointer; all trial receipts stay exclusive."""
    if case.parent != root/browser or case.name not in {f'S{i:02}' for i in range(1, 13)}:
        raise ValueError('Current task is outside the declared cohort')
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', dir=root, prefix='.current-', delete=False) as f:
            temporary = Path(f.name)
            json.dump({'browser': browser, 'scenario': case.name, 'case': str(case)}, f)
            f.write('\n')
        os.replace(temporary, root/'current.json')
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


async def model_run(case, args, browser, prompt, bridge=None):
    model = case/'model'
    model.mkdir(mode=0o700)
    (case/'prompt.txt').write_text(prompt)
    host = browser == 'aside' and case.name in ('S08', 'S12')
    workspace = c.configuration(case, args, browser, bridge, host)
    config = workspace/'.grok/config.toml'
    blueprint = tomllib.loads(config.read_text())['mcp_servers']
    servers = [{'name': name, 'command': value['command'], 'args': value['args'], 'env': []}
               for name, value in blueprint.items()]
    allow = [browser+'__'+('yee_browser' if browser == 'yee' else 'repl')]
    if host:
        allow.append('trial_host__request_user')
    config.write_text('[ui]\npermission_mode = "dontAsk"\n[permission]\nallow = '+
                      json.dumps(['MCPTool('+name+')' for name in allow])+
                      '\ndeny = ["Bash(*)", "Read(*)", "Edit(*)"]\n')
    c.save(model/'mcp-setup.json', servers)
    profile = model/'browser-only.md'
    profile.write_text('---\nname: browser-only\ndescription: Declared browser tasks only\n'
                       'tools: [search_tool, use_tool]\nsubagents: []\n---\n${base_prompt}\n'+
                       (DEV/'browser-agent-policy.md').read_text())
    events_path = model/'acp-events.jsonl'
    actor = None
    with events_path.open('x') as events, (model/'acp.stderr').open('x') as stderr:
        try:
            command = [str(args.grok), 'agent', '--model', 'grok-4.6', '--reasoning-effort',
                       'high', '--no-leader', '--agent-profile', str(profile), 'stdio']
            actor = AcpSession(command, workspace, events, stderr, authorized_mcp_tools=allow)
            setup = await asyncio.to_thread(actor.initialize, workspace, servers)
            c.save(model/'acp-setup.json', setup)
            before = events.tell()
            out = await asyncio.to_thread(actor.prompt, prompt, 240)
            after = events.tell()
            record = {**out, 'session_id': actor.session_id, 'agent_pid': actor.process.pid,
                      'global_event_offsets': [before, after]}
            c.save(model/'acp-prompt.json', record)
            with events_path.open('rb') as source:
                source.seek(before)
                raw_events = [json.loads(line) for line in source.read(after-before).splitlines()]
            usage = normalize([record], actor.session_id)
            c.save(model/'summary.json', {'schema': 'yee.actual-fresh-acp-prompt-summary.v1',
                   'session_id': actor.session_id, 'agent_pid': actor.process.pid,
                   'elapsed_seconds': out['elapsed_seconds'], 'usage': usage,
                   'model_process_reused': False, 'mcp_connection_reused': False})
            parsed = inspect_prompt(record, raw_events)
            c.save(model/'acp-inspection.json', parsed)
            if browser == 'yee':
                rows = lines(model/'mcp-native.jsonl')
                settled = native.inspect(lines(model/'mcp-calls.jsonl'), rows)
                c.save(model/'settlement-before-next-task.json', settled)
                if not settled['native_settlement_verified']:
                    raise RuntimeError('Unsettled native work; no next task or replay')
                if any(row['kind'] == 'response' and (row['response'].get('error') in
                       ('user_cancelled', 'user_takeover', 'client_cancelled', 'session_stopped')
                       or row['response'].get('session_stopped') is True) for row in rows):
                    raise RuntimeError('Native user stop; no subsequent task')
            elif (model/'host-calls.jsonl').exists():
                if any(row['kind'] == 'host_response' and row['result'].get('status') in
                       ('cancelled', 'timeout') for row in lines(model/'host-calls.jsonl')):
                    raise RuntimeError('Host user stop; no subsequent task')
            return parsed
        finally:
            if actor:
                actor.close()


async def run(args):
    started = time.monotonic_ns()
    root = args.record
    root.mkdir(mode=0o700, exist_ok=False)
    freeze = c.frozen_sources(args.yee_app)
    c.save(root/'frozen.json', freeze)
    full = json.loads((DEV.parents[2]/'runtime-native-freeze.json').read_text())
    def verify_full():
        if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest for path, digest in full.items()):
            raise RuntimeError('Frozen source/App/operator changed')
    verify_full()
    c.save(root/'full-freeze.json', full)
    c.save(root/'plan.json', {'schema': 'yee.actual-fresh-acp-cohort.v1', 'browser': args.browser,
           'seed': args.seed, 'cases': [[args.browser, f'S{i:02}'] for i in range(1, 13)],
           'model': 'grok-4.6', 'reasoning_effort': 'high', 'timeout_seconds': 240,
           'requested_turn_cap': 12, 'turn_cap_enforcement': 'usage audit; ACP provider cap not verified',
           'approval_wait_comparison_seconds': 0, 'model_retries': 0, 'no_best_of': True,
           'initial_setup_included': True})
    failed = True
    try:
        for index in range(1, 13):
            verify_full()
            case = root/args.browser/f'S{index:02}'
            case.mkdir(mode=0o700, parents=True)
            save_current(root, args.browser, case)
            task_started = time.monotonic_ns()
            fixture = c.Fixture(case.name, args.seed, case/'fixture')
            server = c.server_for(fixture, 8787)
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            prompt = ('New independent task. Bind only the current prepared tabs; old task facts, '
                      'refs and tab objects do not authorize this task. '+c.common_prompt(case.name, fixture.data))
            prompt += ' Reuse complete successful feedback instead of repeatedly extracting the whole page. '
            (case/'common-prompt.txt').write_text(prompt)
            try:
                trial = c.yee_trial if args.browser == 'yee' else c.aside_trial
                try:
                    await trial(root, case, args, fixture, prompt, run_model=model_run)
                finally:
                    if args.browser == 'aside' and case.name == 'S10' and ((case/'owned.json').exists() or any(case.glob('native-owned-*.json'))):
                        import subprocess
                        result = await asyncio.to_thread(subprocess.run,
                                 [sys.executable, str(DEV/'cleanup-owned-aside-native.py'), str(root), '--restore-layout'],
                                 capture_output=True, text=True)
                        (case/'cleanup-native.output').write_text(result.stdout+result.stderr)
                        if result.returncode:
                            raise RuntimeError('Owned Native Aside cleanup failed')
                verify_full()
                c.save(case/'source-integrity-at-finish.json', c.frozen_sources(args.yee_app))
            finally:
                server.shutdown()
                thread.join()
                server.server_close()
                fixture.close()
                finished = time.monotonic_ns()
                c.save(case/'timing-end.json', {'monotonic_ns': finished,
                       'task_elapsed_seconds': (finished-task_started)/1e9})
            print(json.dumps({'stage': 'fresh_acp_task_collected', 'browser': args.browser, 'scenario': case.name}), flush=True)
        totals = {key: 0 for key in FIELDS}
        identities = []
        for index in range(1, 13):
            summary = json.loads((root/args.browser/f'S{index:02}'/'model/summary.json').read_text())
            identities.append({'session_id': summary['session_id'], 'agent_pid': summary['agent_pid']})
            for key in FIELDS:
                totals[key] += summary['usage']['totals'][key]
        c.save(root/'usage.json', {'totals': totals, 'task_identities': identities,
                                 'cached_input_included_in_input': True})
        c.save(root/'collection-complete.json', {'cases': 12, 'source_integrity': True})
        failed = False
    except Exception:
        import traceback
        (root/'failure.txt').write_text(traceback.format_exc())
        raise
    finally:
        finished = time.monotonic_ns()
        c.save(root/'cohort-timing.json', {'started_monotonic_ns': started,
               'finished_monotonic_ns': finished, 'elapsed_seconds': (finished-started)/1e9,
               'includes_initial_browser_agent_mcp_setup': True,
               'includes_task_preparation_and_verification': True, 'failed': failed})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--browser', choices=['yee', 'aside'], required=True)
    selected, rest = parser.parse_known_args()
    args = c.parse_args(rest)
    args.browser = selected.browser
    if args.cases:
        raise SystemExit('Fresh ACP comparison requires all original twelve tasks; no subsets')
    asyncio.run(run(args))
