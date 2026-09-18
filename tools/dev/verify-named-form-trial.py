#!/usr/bin/env python3
"""Fixed named-form evidence gate, not a general correctness or superiority gate."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
from yee_browser_compact import compact_response
from yee_browser_named import resolve

spec = importlib.util.spec_from_file_location('inspector', Path(__file__).with_name('inspect-grok-direct-trial.py'))
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def verify_evidence(summary, evidence, rows, viewport):
    errors = []
    def require(value, message):
        if not value:
            errors.append(message)
    usage = evidence.get('provider_usage')
    try:
        def finite_nonnegative(value):
            return type(value) in (int, float) and math.isfinite(value) and value >= 0
        elapsed = summary.get('elapsed_seconds')
        wait = evidence.get('total_native_user_wait_seconds')
        require(finite_nonnegative(elapsed) and elapsed > 0, 'missing/invalid whole-task time')
        require(finite_nonnegative(summary.get('reported_cost_usd')), 'missing/invalid reported cost')
        require(finite_nonnegative(wait) and finite_nonnegative(elapsed) and wait <= elapsed,
                'missing/invalid native wait time')
        tokens = usage.get('total_tokens') if isinstance(usage, dict) else None
        require(type(tokens) is int and tokens > 0, 'missing/invalid actual token total')
        require(summary.get('returncode') == 0 and summary.get('timed_out') is False,
                'unclean invocation')
        require(evidence.get('terminal_error') is False and evidence.get('terminal_subtype') == 'success',
                'provider did not report success')
        require(usage is not None and summary.get('usage') == usage, 'unknown/conflicting usage')
        require(summary.get('allowed_mcp_tool') == 'yee__yee_browser' and
                not summary.get('allowed_commands') and not summary.get('editable_test_plan'),
                'unexpected declared authority')
        calls, results = evidence['calls'], evidence['results']
        require([c['tool'] for c in calls] == ['search_tool', 'use_tool'], 'unexpected calls/retries')
        require(len(results) == 2 and [c['id'] for c in calls] == [r['id'] for r in results]
                and len({c['id'] for c in calls}) == 2, 'tool correlation mismatch')
        require(all(not r.get('tool_error') and not r.get('unrecognized_result') for r in results),
                'tool error/unknown result')
        require(calls[1]['input'] == {'tool_name': 'yee__yee_browser', 'tool_input': {
            'commands': [['fill-named', 'Name', 'Cedar'], ['click-named', 'Save locally']]}},
                'wrong submitted task')
        require(results[1].get('server') == 'yee' and results[1].get('tool') == 'yee_browser',
                'wrong result server/tool')
        require(len(rows) == 8 and [r['kind'] for r in rows] == ['request', 'response'] * 4,
                'expected four complete native pairs')
        requests = [r['request'] for r in rows[::2]]
        responses = [r['response'] for r in rows[1::2]]
        require([r['command'] for r in requests] == ['observe', 'fill', 'observe', 'click'],
                'unexpected native sequence')
        require(len({r['id'] for r in requests}) == 4 and all(
            a['id'] == b['id'] for a, b in zip(requests, responses)), 'native correlation mismatch')
        require(all(r.get('ok') is True and r.get('truncated') is False and
                    r.get('viewport') == viewport for r in responses), 'incomplete/failed/resized observation')
        document = responses[0]['document']
        require(all(r.get('document') == document for r in responses), 'document changed')
        for i, role, name in ((0, 'field', 'Name'), (2, 'button', 'Save locally')):
            doc, ref = resolve(responses[i], role, name)
            require(requests[i].get('full') is True and requests[i + 1].get('ref') == ref
                    and requests[i + 1].get('baseline_document') == doc,
                    'action not grounded in internal full observation')
        require(requests[1].get('value') == 'Cedar', 'wrong fill value')
        require(re.search(r'^\+@\S+ field "Name" value=""$', responses[0]['snapshot'], re.M)
                and '"Waiting for input"' in responses[0]['snapshot'], 'initial form not empty')
        require('value="Cedar"' in responses[1]['snapshot'] and
                '"Saved: Cedar"' in responses[3]['snapshot'], 'missing actual result')
        expected = [compact_response({k: v for k, v in r.items() if k != 'timing'})
                    for r in (responses[1], responses[3])]
        shown = [json.loads(line) for line in results[1]['output'].splitlines() if line.strip()]
        require(shown == expected, 'model-visible results differ from native evidence')
        require('Saved: Cedar' in (evidence.get('answer') or ''), 'missing final report')
    except (ValueError, KeyError, TypeError, AttributeError, IndexError):
        errors.append('incomplete/malformed evidence')
    return {'success': not errors, 'errors': errors,
            'tokens': usage.get('total_tokens') if isinstance(usage, dict) else None,
            'wall_seconds': summary.get('elapsed_seconds'),
            'reported_usd': summary.get('reported_cost_usd'),
            'native_user_wait_seconds': evidence.get('total_native_user_wait_seconds'),
            'required_viewport': viewport, 'competitive_gate': 'not_evaluated'}


def verify(record, native, viewport):
    summary, evidence = {}, {}
    try:
        summary = json.loads((record / 'summary.json').read_text())
        evidence = inspector.inspect(record, [native])
        rows = [json.loads(line) for line in native.read_text().splitlines()]
        return verify_evidence(summary, evidence, rows, viewport)
    except (OSError, ValueError, KeyError, TypeError):
        usage = summary.get('usage')
        return {'success': False, 'errors': ['missing/malformed record'],
                'tokens': usage.get('total_tokens') if isinstance(usage, dict) else None,
                'competitive_gate': 'not_evaluated'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('--viewport', nargs=2, type=int, required=True)
    args = parser.parse_args()
    if any(n <= 0 for n in args.viewport):
        parser.error('viewport dimensions must be positive')
    result = verify(args.record, args.native, dict(zip(('width', 'height'), args.viewport)))
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['success'] else 1)
