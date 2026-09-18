#!/usr/bin/env python3
"""Gate the fixed empty-Name → Cedar → Save direct-agent benchmark.

Checks recorded evidence, not general browser correctness or competitor wins.
Never hides usage for a failed trial. No model or browser calls are made.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re

from yee_browser_compact import compact_response

spec = importlib.util.spec_from_file_location('inspector', Path(__file__).with_name('inspect-grok-direct-trial.py'))
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def verify_evidence(summary, evidence, rows, transport, expected_viewport=None):
    errors = []
    def require(condition, message):
        if not condition:
            errors.append(message)

    require(summary.get('returncode') == 0 and summary.get('timed_out') is False,
            'recorder did not finish cleanly')
    require(evidence.get('terminal_error') is False and evidence.get('terminal_subtype') == 'success',
            'provider terminal result is not successful')
    usage = evidence.get('provider_usage')
    require(usage is not None and usage == summary.get('usage'), 'missing or conflicting measured usage')
    calls, results = evidence['calls'], evidence['results']
    require(len(calls) == len(results) and len({c['id'] for c in calls}) == len(calls)
            and [c['id'] for c in calls] == [r['id'] for r in results], 'tool ID/order mismatch')
    require(all(not r.get('tool_error') and not r.get('unrecognized_result') for r in results),
            'tool error or unknown result shape')

    browser_results = []
    submitted = None
    if transport == 'mcp':
        require(summary.get('allowed_mcp_tool') == 'yee__yee_browser'
                and not summary.get('allowed_commands') and not summary.get('editable_test_plan'),
                'unexpected MCP authority')
        require([c['tool'] for c in calls] == ['search_tool', 'use_tool', 'use_tool'],
                'unexpected MCP tool sequence or retry')
        for call, result in zip(calls, results):
            if call['tool'] == 'use_tool':
                require(call['input'].get('tool_name') == 'yee__yee_browser'
                        and result.get('server') == 'yee' and result.get('tool') == 'yee_browser',
                        'wrong MCP target')
                browser_results.append(result)
        if len(calls) == 3:
            require(calls[1]['input'].get('tool_input') == {'commands': [['observe', '--full']]},
                    'missing full observation')
            submitted = calls[2]['input'].get('tool_input', {}).get('commands')
    else:
        require(not summary.get('allowed_mcp_tool'), 'unexpected file-plan MCP authority')
        require([c['tool'] for c in calls] == ['run_terminal_command', 'read_file',
                                            'search_replace', 'run_terminal_command'],
                'unexpected file-plan tool sequence or retry')
        for call, result in zip(calls, results):
            if call['tool'] == 'run_terminal_command':
                require(result.get('exit_code') == 0, 'shell command failed')
                require(call['input'].get('command') == result.get('command')
                        and result.get('command') in summary.get('allowed_commands', []),
                        'shell command outside recorded authority')
                browser_results.append(result)
        if len(calls) == 4:
            path = summary.get('editable_test_plan')
            require(path and calls[1]['input'].get('target_file') == path
                    and calls[2]['input'].get('file_path') == path,
                    'wrong plan file')
            require(calls[2]['input'].get('old_string') == 'REPLACE_WITH_JSONL_PLAN',
                    'plan was not authored from placeholder')
            submitted = [json.loads(line) for line in calls[2]['input'].get('new_string', '').splitlines()
                         if line.strip()]

    require(len(browser_results) == 2, 'expected observe and batch tool results')
    observations = [json.loads(line) for result in browser_results
                    for line in result['output'].splitlines() if line.strip()]
    require(len(observations) == 3 and all(o.get('ok') is True for o in observations),
            'expected exactly three successful observations')
    if expected_viewport is not None:
        require(all(o.get('viewport') == expected_viewport for o in observations),
                'missing or mismatched viewport during task')
    require(len(rows) == 6, 'expected exactly three native request/response pairs')
    if len(rows) == 6 and len(observations) == 3:
        requests, responses = rows[::2], rows[1::2]
        require([r.get('kind') for r in requests] == ['request'] * 3
                and [r.get('kind') for r in responses] == ['response'] * 3,
                'invalid native record order')
        native_requests = [r['request'] for r in requests]
        native_responses = [r['response'] for r in responses]
        require([r['command'] for r in native_requests] == ['observe', 'fill', 'click'],
                'unexpected native command sequence')
        require(len({r['id'] for r in native_requests}) == 3
                and all(a['id'] == b['id'] for a, b in zip(native_requests, native_responses)),
                'native request/response ID mismatch')
        expected = [compact_response({k: v for k, v in r.items() if k != 'timing'})
                    for r in native_responses]
        require(observations == expected, 'model-visible results differ from native evidence')
        first, filled, saved = observations
        document = first.get('document')
        snapshot = first.get('snapshot', '')
        field = re.search(r'^\+(@\d+) field "Name" value=""$', snapshot, re.M)
        button = re.search(r'^\+(@\d+) button "Save locally"$', snapshot, re.M)
        require(field is not None and button is not None and '"Waiting for input"' in snapshot,
                'initial form is not empty/groundable')
        require(document and all(o.get('document') == document and o.get('truncated') is False
                                for o in observations), 'document changed or observation incomplete')
        require('value="Cedar"' in filled.get('snapshot', '')
                and '"Saved: Cedar"' in saved.get('snapshot', ''), 'missing filled/saved page evidence')
        if field and button and document:
            plan = [['--document', document, 'fill', field[1], 'Cedar'],
                    ['--document', document, 'click', button[1]]]
            require(submitted == plan, 'agent plan does not match observed refs/task')
            require(native_requests[1].get('ref') == document + '_' + field[1][1:]
                    and native_requests[1].get('value') == 'Cedar'
                    and native_requests[2].get('ref') == document + '_' + button[1][1:],
                    'native mutations differ from agent plan')
    require('Saved: Cedar' in (evidence.get('answer') or ''), 'agent did not report saved status')
    return {'success': not errors, 'errors': errors, 'transport': transport,
            'required_viewport': expected_viewport,
            'tokens': usage['total_tokens'] if usage else None,
            'wall_seconds': summary.get('elapsed_seconds'),
            'reported_usd': summary.get('reported_cost_usd'),
            'native_user_wait_seconds': evidence.get('total_native_user_wait_seconds'),
            'competitive_gate': 'not_evaluated'}


def verify(record, native, transport, expected_viewport=None):
    summary, evidence = {}, {}
    try:
        summary = json.loads((record / 'summary.json').read_text())
        evidence = inspector.inspect(record, native)
        rows = [json.loads(line) for path in native for line in path.read_text().splitlines()]
        return verify_evidence(summary, evidence, rows, transport, expected_viewport)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        usage = evidence.get('provider_usage') or summary.get('usage')
        return {'success': False, 'errors': [type(exc).__name__ + ': incomplete/malformed evidence'],
                'tokens': usage.get('total_tokens') if isinstance(usage, dict) else None,
                'competitive_gate': 'not_evaluated'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('native', nargs='+', type=Path)
    parser.add_argument('--transport', choices=('mcp', 'file'), required=True)
    parser.add_argument('--viewport', nargs=2, type=int, metavar=('WIDTH', 'HEIGHT'),
                        help='require these page dimensions in every native/model observation')
    args = parser.parse_args()
    if args.viewport and any(n <= 0 for n in args.viewport):
        parser.error('viewport dimensions must be positive')
    viewport = dict(zip(('width', 'height'), args.viewport)) if args.viewport else None
    result = verify(args.record, args.native, args.transport, viewport)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['success'] else 1)
