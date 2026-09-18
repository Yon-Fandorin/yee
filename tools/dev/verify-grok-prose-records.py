#!/usr/bin/env python3
"""Audit the bounded Yee prose ablation from preserved raw CLI/browser records.

This is deliberately NOT a competitive benchmark or an all-workflow usage gate.
"""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('grok_record', Path(__file__).with_name('run-grok-recorded.py'))
recorder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recorder)


def read_json(path):
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError(f'record too large: {path.name}')
    return json.loads(path.read_text(encoding='utf-8'))


def load_call(path):
    raw = read_json(path / 'stdout.json')
    summary = read_json(path / 'summary.json')
    usage = recorder.normalize_usage(raw)
    if not usage or summary.get('usage') != usage:
        raise ValueError('missing or inconsistent raw usage')
    if summary.get('returncode') != 0 or summary.get('timed_out'):
        raise ValueError('unsuccessful CLI invocation')
    if raw.get('num_turns') != 1:
        raise ValueError('unexpected internal turn count')
    models = raw.get('modelUsage', {})
    if set(models) != {'grok-4.6-build'} or models['grok-4.6-build'].get('modelCalls') != 1:
        raise ValueError('unexpected model or internal call count')
    cost = raw.get('total_cost_usd')
    if type(cost) not in (int, float) or not 0 <= cost < 100:
        raise ValueError('unknown or invalid cost')
    return {'decision': json.loads(raw['text']), 'usage': usage, 'reported_usd': cost}


def audit(root):
    before1 = load_call(root / 'yee-read-call-1')
    before2 = load_call(root / 'yee-read-call-2')
    after = load_call(root / 'yee-read-after-call-1')
    initial = read_json(root / 'yee-read-call-1/browser-before.json')
    scoped = read_json(root / 'yee-read-call-1/browser-after.json')
    improved = read_json(root / 'yee-read-after-call-1/browser-before.json')
    if not initial.get('ok') or not improved.get('ok'):
        raise ValueError('unsuccessful browser observation')
    if not scoped.get('ok') or scoped.get('field_truncated') is not False:
        raise ValueError('scoped browser evidence is incomplete')
    words = scoped.get('text', '').split()
    if not words:
        raise ValueError('empty browser text')
    expected = words[-1]
    if before1['decision'] != {'command': ['--document', initial['document'], 'read', '10']}:
        raise ValueError('unexpected original model command')
    if before2['decision'] != {'answer': expected} or after['decision'] != {'answer': expected}:
        raise ValueError('model answer differs from browser evidence')
    if improved.get('truncated') is not False or expected not in improved.get('snapshot', ''):
        raise ValueError('improved first observation lacks the answer')
    if expected in initial.get('snapshot', ''):
        raise ValueError('original observation already contained the answer')
    before_tokens = before1['usage']['total_tokens'] + before2['usage']['total_tokens']
    after_tokens = after['usage']['total_tokens']
    return {'status': 'single_task_ablation_pass', 'competitive_gate': 'not_evaluated',
            'expected_answer': expected, 'before_calls': 2, 'after_calls': 1,
            'before_total_tokens': before_tokens, 'after_total_tokens': after_tokens,
            'sample_token_reduction_percent': 100 * (before_tokens - after_tokens) / before_tokens,
            'before_reported_usd': before1['reported_usd'] + before2['reported_usd'],
            'after_reported_usd': after['reported_usd'], 'billing_reconciled': False,
            'coverage': 'three specified stateless CLI invocations; no competitor or development tokens'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record_root', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.record_root), ensure_ascii=False, indent=2))
