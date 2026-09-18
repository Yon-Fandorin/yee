#!/usr/bin/env python3
"""Audit native timing without treating approval-free time as task latency."""
import argparse
import json
import math
from pathlib import Path


def analyze(events):
    pending = {}
    seen = set()
    rows = []
    errors = []
    missing = []
    client_errors = 0
    for sequence, event in enumerate(events, 1):
        if event.get('sequence') != sequence:
            errors.append(f'event {sequence}: invalid sequence')
        kind = event.get('kind')
        if kind == 'client_error':
            client_errors += 1
            continue
        if kind not in ('request', 'response'):
            errors.append(f'event {sequence}: unknown event kind')
            continue
        body = event.get(kind, {})
        request_id = body.get('id')
        if not isinstance(request_id, str) or not request_id:
            errors.append(f'event {sequence}: missing request id')
            continue
        if kind == 'request':
            if request_id in seen:
                errors.append(f'event {sequence}: duplicate request id')
                continue
            seen.add(request_id)
            pending[request_id] = body.get('command')
            continue
        if request_id not in pending:
            errors.append(f'event {sequence}: unmatched response')
            continue
        command = pending.pop(request_id)
        timing = body.get('timing')
        if not isinstance(timing, dict):
            missing.append(request_id)
            continue
        elapsed = timing.get('native_elapsed_ms')
        wait = timing.get('user_wait_ms')
        if any(type(n) not in (int, float) or not math.isfinite(n) or n < 0
               for n in (elapsed, wait)) or wait > elapsed:
            errors.append(f'event {sequence}: invalid timing')
            continue
        rows.append({'id': request_id, 'command': command,
                     'native_elapsed_ms': elapsed, 'user_wait_ms': wait,
                     'native_nonwait_ms': round(elapsed - wait, 6)})
    if pending:
        errors.append(f'{len(pending)} requests lack a response')
    return {'timing_audit_pass': not errors and not missing and not client_errors and bool(rows),
            'requests': rows, 'missing_timing_ids': missing,
            'client_error_count': client_errors, 'errors': errors,
            'totals_ms': {key: round(sum(r[key] for r in rows), 6)
                          for key in ('native_elapsed_ms', 'user_wait_ms', 'native_nonwait_ms')},
            'whole_task_elapsed_seconds': None,
            'scope': 'Native request timer only; excludes CLI, transport, model, setup and final verification. Nonwait time is diagnostic, not task latency or comparative victory.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('transcript', type=Path)
    args = parser.parse_args()
    result = analyze([json.loads(line) for line in args.transcript.read_text().splitlines()])
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result['timing_audit_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
