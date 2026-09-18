#!/usr/bin/env python3
"""Historical prepared-browser read cohort; fresh Grok process per trial.

Setup must already be independently checked: same HTTP fixture, empty form,
1440x900 viewport. This does not establish persistent-session reuse, cold browser
startup or other task coverage. The underlying direct-command gate stays held.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', required=True)
    parser.add_argument('--aside-target', required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--yee-text', action='store_true', help='explicit compact-text ablation')
    parser.add_argument('--native-prefix', default='cohort', help='unique native log prefix per cohort')
    args = parser.parse_args()
    if not re.fullmatch(r'/private/tmp/yee-agent\.[A-Za-z0-9]+', args.bridge):
        parser.error('expected isolated Yee bridge')
    if not re.fullmatch(r'[A-F0-9]{32}', args.aside_target):
        parser.error('expected dedicated Aside target')
    if not re.fullmatch(r'[a-z0-9-]{1,40}', args.native_prefix):
        parser.error('native-prefix must be 1..40 lowercase letters, digits or hyphens')
    if not args.record.is_absolute():
        parser.error('record must be absolute and new')
    args.record.mkdir(mode=0o700, exist_ok=False)
    dev = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('recorder', dev / 'run-grok-recorded.py')
    recorder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recorder)
    trials = []
    for index, harness in enumerate(('yee', 'aside', 'aside', 'yee', 'yee', 'aside'), 1):
        label = f'{index}-{harness}'
        command = (f'python3 {dev}/yee-browser.py --bridge {args.bridge} --compact ' + ('--text ' if args.yee_text else '') +
                   f'--record {args.bridge}/{args.native_prefix}-{label}.jsonl observe --full' if harness == 'yee'
                   else f'zsh {dev}/aside-benchmark-observe.sh {args.aside_target}')
        prompt = ('Use your Bash tool to run the single read-only browser command below. '
                  'Task: report the last word of the long paragraph on the test page. '
                  'Use its actual output. Do not implement code, run other commands, or invent results. '
                  'If it fails report the error. Finish with only JSON containing answer or error.\n\n' + command + '\n')
        prompt_path = args.record / f'{label}.txt'
        with prompt_path.open('x', encoding='utf-8') as stream:
            stream.write(prompt)
        prompt_path.chmod(0o600)
        call_dir = args.record / label
        child = subprocess.run([sys.executable, str(dev / 'run-grok-recorded.py'),
                                '--binary', '/Users/yongjunkim/.grok/downloads/grok-macos-aarch64',
                                '--cwd', '/private/tmp/yee-model-probe.63LL8B',
                                '--model', 'grok-4.6', '--prompt', str(prompt_path),
                                '--record', str(call_dir), '--timeout', '120', '--max-turns', '4',
                                '--direct-command', command], check=False)
        trials.append({'trial': label, 'runner_exit': child.returncode})
        print(json.dumps(trials[-1]), flush=True)
    recorder.private_write(args.record / 'cohort.json', {'trials': trials,
                           'boundary': 'prepared-browser read-only; browser setup outside model task',
                           'model_process_mode': 'fresh_process_per_trial',
                           'persistent_session_reuse': False,
                           'yee_compact_text': args.yee_text,
                           'competitive_gate': 'not_evaluated'})


if __name__ == '__main__':
    main()
