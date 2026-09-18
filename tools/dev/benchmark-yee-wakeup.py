#!/usr/bin/env python3
"""Interleaved real-native status latency: notification vs 100ms fallback.

No model calls; diagnostic only. Includes fresh CLI subprocess startup in parent
wall timing. The fallback arm emulates old sleep behavior in the same source.
"""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import random
import statistics
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--one', choices=('notification', 'poll'))
    args = parser.parse_args()
    if args.one:
        spec = importlib.util.spec_from_file_location('cli', Path(__file__).with_name('yee-browser.py'))
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        if args.one == 'poll':
            class Poll:
                def __init__(self, directory):
                    pass
                def wait(self, seconds):
                    time.sleep(max(0, min(0.1, seconds)))
                def close(self):
                    pass
            cli.ResponseWakeup = Poll
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = cli.main(['--bridge', args.bridge, '--compact', '--record', str(args.record), 'status'])
        print(json.dumps({'exit': code, 'output': output.getvalue()}))
        return code
    args.record.mkdir(mode=0o700, exist_ok=False)
    rng = random.Random(20260908)
    rows = []
    for pair in range(30):
        modes = ['notification', 'poll']
        rng.shuffle(modes)
        for mode in modes:
            # Vary arrival phase relative to native 150ms polling, identically
            # sampled for each arm. Do not benchmark a favorable fixed phase.
            time.sleep(rng.uniform(0, 0.15))
            native_path = args.record / f'{pair}-{mode}.jsonl'
            started = time.monotonic()
            result = subprocess.run([sys.executable, __file__, '--bridge', args.bridge,
                                     '--record', str(native_path), '--one', mode], capture_output=True, text=True)
            rows.append({'pair': pair, 'mode': mode, 'wall_ms': (time.monotonic() - started) * 1000,
                         'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
    with (args.record / 'results.json').open('x') as stream:
        json.dump(rows, stream, indent=2)
    summary = {}
    for mode in ('notification', 'poll'):
        group = [r for r in rows if r['mode'] == mode]
        times = sorted(r['wall_ms'] for r in group)
        summary[mode] = {'n': len(group), 'failures': sum(r['exit'] != 0 for r in group),
                         'median_ms': statistics.median(times), 'mean_ms': statistics.mean(times),
                         'p95_ms_nearest_rank': times[28]}
    with (args.record / 'summary.json').open('x') as stream:
        json.dump(summary, stream, indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    raise SystemExit(main())
