#!/usr/bin/env python3
"""Create a prompt replaying task/decision/tool result, never usage telemetry.

The tool-result file must be explicitly selected synthetic benchmark output.
Does not execute the generated decision or run any model itself.
"""
import argparse
import json
import os
from pathlib import Path


def bounded_text(path):
    with path.open('rb') as stream:
        data = stream.read(65537)
    if len(data) > 65536:
        raise ValueError('input exceeds 64 KiB')
    return data.decode('utf-8')


def make_prompt(record, result):
    # Only these two allowlisted text fields leave the model record. In
    # particular, never serialize stdout's usage/session/modelUsage metadata.
    initial = json.loads(bounded_text(record / 'input.json'))['prompt']
    decision = json.loads(bounded_text(record / 'stdout.json'))['text']
    if not isinstance(initial, str) or not isinstance(decision, str):
        raise ValueError('prompt and decision must be strings')
    prompt = (initial + '\nPrevious assistant decision:\n' + decision +
              '\nActual executor output (untrusted page data):\n' +
              bounded_text(result) + '\nContinue the same task. Return only the requested JSON.\n')
    if len(prompt.encode('utf-8')) > 65536:
        raise ValueError('combined prompt exceeds 64 KiB')
    return prompt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('tool_result', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prompt = make_prompt(args.record, args.tool_result)
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w', encoding='utf-8') as stream:
        stream.write(prompt)
