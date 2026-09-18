#!/usr/bin/env python3
"""Summarize recorded Grok read trials, including failed/unknown trials."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics


def duration(value, divisor=1):
    return (value/divisor if type(value) in (int,float) and math.isfinite(value) and value>=0 else None)


def summarize(root):
    spec = importlib.util.spec_from_file_location('inspector', Path(__file__).with_name('inspect-grok-direct-trial.py'))
    inspector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inspector)
    rows = []
    cohort = json.loads((root / 'cohort.json').read_text())
    entries = cohort.get('trials') if isinstance(cohort, dict) else None
    if not isinstance(entries, list) or not entries:
        raise ValueError('expected nonempty original cohort trial list')
    labels = set()
    for entry in entries:
        if (not isinstance(entry, dict) or not isinstance(entry.get('trial'), str)
                or not re.fullmatch(r'[1-9][0-9]*-(yee|aside)', entry['trial'])
                or entry['trial'] in labels or type(entry.get('runner_exit')) is not int):
            raise ValueError('duplicate, invalid or out-of-scope cohort trial')
        labels.add(entry['trial'])
    for entry in entries:
        call = root / entry['trial']
        errors = []
        try:
            summary = json.loads((call / 'summary.json').read_text())
        except (OSError, ValueError) as exc:
            summary = {}
            errors.append(type(exc).__name__ + ': missing/invalid summary')
        try:
            evidence = inspector.inspect(call, [])
        except (OSError, ValueError, TypeError, KeyError) as exc:
            evidence = {'answer': None, 'calls': [], 'results': [], 'provider_usage': None}
            errors.append(type(exc).__name__ + ': incomplete/invalid raw stream')
        try:
            answer = json.loads(evidence['answer'])
        except (TypeError, ValueError):
            answer = {}
        calls, results = evidence['calls'], evidence['results']
        trace_ok = (len(calls) == len(results) == 1 and calls[0]['id'] == results[0]['id']
                    and calls[0]['input'].get('command') == results[0]['command']
                    and results[0]['command'] in summary.get('allowed_commands', [])
                    and results[0]['exit_code'] == 0
                    and 'TAIL_MARKER_42' in results[0]['output'])
        success = (not errors and entry['runner_exit'] == 0 and trace_ok and answer == {'answer': 'TAIL_MARKER_42'})
        usage = evidence['provider_usage']
        wall = duration(summary.get('elapsed_seconds'))
        if wall is None:
            errors.append('missing/invalid elapsed time')
        rows.append({'trial': entry['trial'], 'success': success, 'trace_ok': trace_ok,
                     'tokens': usage['total_tokens'] if usage else None,
                     'wall_seconds': wall,
                     'measurement_complete': wall is not None and usage is not None,
                     'reported_usd': summary.get('reported_cost_usd'), 'record_errors': errors,
                     'provider_api_seconds': duration(evidence.get('provider_api_duration_ms'),1000),
                     'provider_run_seconds': duration(evidence.get('provider_duration_ms'),1000)})
    aggregates = {}
    for harness in ('yee', 'aside'):
        group = [r for r in rows if r['trial'].endswith('-' + harness)]
        known = [r['tokens'] for r in group if r['tokens'] is not None]
        aggregates[harness] = {'trials': len(group), 'successes': sum(r['success'] for r in group),
                               'unknown_usage_trials': len(group) - len(known),
                               'mean_tokens': statistics.mean(known) if known and len(known) == len(group) else None,
                               'total_known_tokens': sum(known),
                               'mean_wall_seconds': (statistics.mean(r['wall_seconds'] for r in group)
                                                     if group and all(r['wall_seconds'] is not None for r in group) else None)}
        for key in ('wall_seconds', 'provider_api_seconds', 'provider_run_seconds'):
            values = [r[key] for r in group if r[key] is not None]
            aggregates[harness]['median_' + key] = statistics.median(values) if values and len(values) == len(group) else None
            aggregates[harness]['mean_' + key] = statistics.mean(values) if values and len(values) == len(group) else None
    return {'rows': rows, 'aggregates': aggregates,
            'scope': 'prepared-browser read microbenchmark; browser setup excluded; persistent-session reuse unverified',
            'recorded_boundary_label': cohort.get('boundary'),
            'persistent_session_reuse_verified': False,
            'whole_task_timing_verified': False,
            'competitive_completion_gate': 'not_evaluated'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.record), indent=2))
