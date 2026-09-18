#!/usr/bin/env python3
"""Fixed local batch-form trace gate; never a comparative superiority gate."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
from yee_browser_compact import compact_response
from yee_browser_named import resolve_batch

spec = importlib.util.spec_from_file_location('inspector', Path(__file__).with_name('inspect-grok-direct-trial.py'))
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)
PLAN = [['fill', 'Name', 'Cedar'], ['click', 'Save locally']]


def verify_evidence(summary, evidence, rows):
    errors = []
    def check(ok, message):
        if not ok:
            errors.append(message)
    def finite(value):
        return type(value) in (int, float) and math.isfinite(value) and value >= 0
    usage = evidence.get('provider_usage')
    try:
        tokens = usage.get('total_tokens') if isinstance(usage, dict) else None
        check(type(tokens) is int and tokens > 0 and summary.get('usage') == usage, 'unknown/conflicting usage')
        elapsed, wait = summary.get('elapsed_seconds'), evidence.get('total_native_user_wait_seconds')
        check(finite(elapsed) and elapsed > 0 and finite(wait) and wait <= elapsed and
              finite(summary.get('reported_cost_usd')), 'invalid measurements')
        check(summary.get('returncode') == 0 and summary.get('timed_out') is False and
              evidence.get('terminal_error') is False and evidence.get('terminal_subtype') == 'success',
              'unclean invocation')
        check(summary.get('allowed_mcp_tool') == 'yee__yee_browser' and
              not summary.get('allowed_commands') and not summary.get('editable_test_plan'), 'wrong authority')
        calls, results = evidence['calls'], evidence['results']
        check([c['tool'] for c in calls] == ['search_tool', 'use_tool'], 'unexpected calls/retries')
        check(len(results) == 2 and [c['id'] for c in calls] == [r['id'] for r in results] and
              len({c['id'] for c in calls}) == 2, 'tool correlation mismatch')
        check(all(not r.get('tool_error') and not r.get('unrecognized_result') for r in results), 'tool error')
        submitted = calls[1]['input']
        check(set(submitted) == {'tool_name', 'tool_input'} and submitted['tool_name'] == 'yee__yee_browser', 'wrong tool')
        arguments = submitted['tool_input']
        check(set(arguments) == {'commands'} and len(arguments['commands']) == 1, 'wrong batch invocation')
        command = arguments['commands'][0]
        check(len(command) == 2 and command[0] == 'batch-named' and json.loads(command[1]) == PLAN, 'wrong task')
        check(len(rows) == 4 and [r['kind'] for r in rows] == ['request', 'response'] * 2, 'expected two native pairs')
        observe, initial, batch, final = rows[0]['request'], rows[1]['response'], rows[2]['request'], rows[3]['response']
        check(observe['command'] == 'observe' and observe.get('full') is True and batch['command'] == 'batch', 'wrong native sequence')
        check(observe['id'] == initial['id'] and batch['id'] == final['id'] and observe['id'] != batch['id'], 'native correlation')
        check(all(r.get('ok') is True and r.get('truncated') is False and
                  r.get('viewport') == {'width': 1440, 'height': 900} for r in (initial, final)), 'failed/truncated/resized')
        document, actions = resolve_batch(initial, PLAN)
        check(batch.get('actions') == actions and batch.get('baseline_document') == document and
              batch.get('baseline_revision') == initial.get('revision') and final.get('document') == document,
              'ungrounded or changed batch')
        check('field "Name" value=""' in initial['snapshot'] and '"Waiting for input"' in initial['snapshot'], 'not initially empty')
        check(final.get('completed') == 2 and final.get('partial_effect_possible') is not True and
              'field "Name" value="Cedar"' in final['snapshot'] and '"Saved: Cedar"' in final['snapshot'], 'missing actual complete result')
        shown = results[1]
        check(shown.get('server') == 'yee' and shown.get('tool') == 'yee_browser', 'wrong result source')
        check(json.loads(shown['output']) == compact_response({k:v for k,v in final.items() if k != 'timing'}), 'result differs from native')
        check('Saved: Cedar' in (evidence.get('answer') or ''), 'missing final report')
    except (ValueError, KeyError, TypeError, AttributeError, IndexError, OverflowError):
        errors.append('malformed evidence')
    return {'success': not errors, 'errors': errors,
            'tokens': usage.get('total_tokens') if isinstance(usage, dict) else None,
            'wall_seconds': summary.get('elapsed_seconds'), 'reported_usd': summary.get('reported_cost_usd'),
            'native_user_wait_seconds': evidence.get('total_native_user_wait_seconds'),
            'competitive_gate': 'not_evaluated'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('native', type=Path)
    args = parser.parse_args()
    summary = json.loads((args.record / 'summary.json').read_text())
    evidence = inspector.inspect(args.record, [args.native])
    rows = [json.loads(line) for line in args.native.read_text().splitlines()]
    result = verify_evidence(summary, evidence, rows)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['success'] else 1)
