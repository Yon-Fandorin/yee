#!/usr/bin/env python3
"""Join Codex model/MCP/native evidence with the unchanged scenario oracle."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from browser_trial_answer import unique_object, invalid_constant


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


correlation = load('codex_correlation', 'inspect-yee-mcp-calls.py')
oracle = load('codex_oracle', 'verify-agent-scenario.py')


def inspect(record, fixture):
    record, fixture = Path(record), Path(fixture)
    hashes = {}
    def read(path, lines=False):
        raw = path.read_bytes()
        if len(raw) > 8 * 1024 * 1024:
            raise ValueError('evidence exceeds limit')
        hashes[str(path)] = hashlib.sha256(raw).hexdigest()
        parse = lambda s: json.loads(s, object_pairs_hook=unique_object,
                                     parse_constant=invalid_constant)
        return [parse(s) for s in raw.splitlines() if s.strip()] if lines else parse(raw)
    summary = read(record/'summary.json')
    events = read(record/'stdout.jsonl', True)
    calls = read(record/'mcp-calls.jsonl', True)
    native = read(record/'native.jsonl', True)
    items = [e['item'] for e in events if e.get('type') == 'item.completed']
    tools = [i for i in items if i.get('type') == 'mcp_tool_call']
    if len(tools) * 2 != len(calls):
        raise ValueError('incomplete model/MCP coverage')
    for index, item in enumerate(tools):
        request, reply = calls[2*index:2*index+2]
        if (item.get('server') != 'yee' or item.get('tool') != 'yee_browser'
                or request.get('name') != 'yee_browser'
                or item.get('arguments') != request.get('arguments')):
            raise ValueError('model/MCP tool scope or argument mismatch')
        content = item.get('result', {}).get('content')
        if (not isinstance(content, list) or any(b.get('type') != 'text' for b in content)
                or [b['text'] for b in content] != reply.get('content')):
            raise ValueError('model-visible result differs from MCP output')
    answers = [i for i in items if i.get('type') == 'agent_message'
               and i.get('text', '').strip()]
    if len(answers) != 1 or items.index(answers[0]) < max(
            (items.index(t) for t in tools), default=-1):
        raise ValueError('no unique terminal JSON answer')
    answer = json.loads(answers[0]['text'], object_pairs_hook=unique_object,
                        parse_constant=invalid_constant)
    if not isinstance(answer, dict):
        raise ValueError('final answer is not an object')
    for name in ('manifest.json', 'dataset.json', 'events.jsonl'):
        read(fixture/name, name.endswith('jsonl'))
    task = oracle.verify(fixture, answer, native, None, 'http://127.0.0.1:8787')
    trace = correlation.inspect(calls, native)
    lifecycle = summary['lifecycle']
    clean = lifecycle['returncode'] == 0 and not any(lifecycle[k] for k in (
        'timed_out', 'interrupted', 'descendants_after_normal_exit'))
    failures = []
    if not clean: failures.append('model_process_not_cleanly_completed')
    if not task['success']: failures.append('scenario_oracle_failed')
    if not trace['native_settlement_verified']: failures.append('native_outcome_unsettled')
    return {'schema': 'yee.codex-scenario-inspection.v1',
            'model': summary['model'], 'scenario': task['scenario'],
            'accepted': not failures, 'failures': failures,
            'task_oracle': task, 'native_correlation': trace,
            'model_mcp_exact_correlation': True, 'summary': summary,
            'tool_calls': len(tools),
            'mcp_errors': sum(r.get('is_error') is True for r in calls),
            'input_sha256': hashes,
            'scope': 'Recorded direct browser use; not an Aside comparison or provider isolation proof.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('fixture', type=Path)
    args = parser.parse_args()
    result = inspect(args.record, args.fixture)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(0 if result['accepted'] else 1)
