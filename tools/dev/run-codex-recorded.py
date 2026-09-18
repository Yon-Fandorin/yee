#!/usr/bin/env python3
"""One bounded actual Luna/Yee trial using a verified synthetic CLI profile.

Keeps normal Codex auth, changes no user configuration, never auto-approves
native browser prompts. Raw JSONL usage is actual provider-reported usage.
"""
import argparse
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import yee_trial_timeline


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


supervisor = module('codex_supervisor', 'run-kimi-recorded.py')
probe = module('codex_policy', 'probe-codex-tool-policy.py')


def normalize_usage(events):
    completed = [e for e in events if e.get('type') == 'turn.completed']
    if len(completed) != 1:
        return None
    usage = completed[0].get('usage', {})
    fields = ('input_tokens', 'output_tokens', 'cached_input_tokens',
              'cache_write_input_tokens', 'reasoning_output_tokens')
    if any(type(usage.get(k)) is not int or usage[k] < 0 for k in fields):
        return None
    if (usage['cached_input_tokens'] > usage['input_tokens']
            or usage['reasoning_output_tokens'] > usage['output_tokens']):
        return None
    # The synthetic two-response check verifies cache is reported separately
    # inside input. Do not add cached input to input again.
    return {**usage, 'total_tokens': usage['input_tokens'] + usage['output_tokens']}


def prepare(args):
    for directory in (args.policy_probe, args.bridge):
        supervisor.private_dir(directory)
    if not args.record.is_absolute() or args.record.exists():
        raise ValueError('record must be a new absolute directory')
    if not 1 <= args.timeout <= 300:
        raise ValueError('timeout must be 1..300 seconds')
    previous = json.loads((args.policy_probe/'result.json').read_text())
    checked = probe.evaluate(previous)
    expected = {'apply_patch', 'list_mcp_resource_templates', 'list_mcp_resources',
                'mcp__yee__yee_browser', 'read_mcp_resource'}
    if not checked['plumbing_probe_pass'] or set(checked['nested_tools']) != expected:
        raise ValueError('synthetic MCP, shell/write denial and no-agent inventory proof required')
    manifest = json.loads((args.policy_probe/'manifest.json').read_text())
    if supervisor.sha(args.codex) != manifest['codex_sha256']:
        raise ValueError('Codex changed since synthetic probe')
    original = manifest['command']
    # Preserve every verified flag and feature setting; change only provider,
    # owned workspace, MCP transport and task prompt.
    overrides = {}
    for index, value in enumerate(original):
        if value == '-c':
            key, setting = original[index+1].split('=', 1)
            overrides[key] = setting
    for key in list(overrides):
        if key == 'model_provider' or key.startswith(('model_providers.probe.', 'projects.')):
            del overrides[key]
    if overrides.get('agents.enabled') != 'false':
        raise ValueError('agent tools must remain disabled')
    workspace = args.record/'workspace'
    overrides[f'projects.{json.dumps(str(workspace))}.trust_level'] = '"trusted"'
    mcp = Path(__file__).with_name('yee-browser-mcp.py').resolve()
    if not args.python.is_absolute() or not args.python.is_file():
        raise ValueError('absolute MCP Python executable required')
    # Preserve the venv executable path: resolving its symlink loses site-packages.
    overrides['mcp_servers.yee.command'] = json.dumps(str(args.python))
    overrides['mcp_servers.yee.args'] = json.dumps([
        str(mcp), '--bridge', str(args.bridge), '--record', str(args.record/'native.jsonl'),
        '--calls-record', str(args.record/'mcp-calls.jsonl')])
    overrides['mcp_servers.yee.tool_timeout_sec'] = str(args.timeout)
    policy = Path(__file__).with_name('browser-agent-policy.md')
    prompt = args.prompt.read_text() + '\n\n' + policy.read_text()
    if not prompt.strip() or len(prompt.encode()) > 32768:
        raise ValueError('prompt must be 1..32768 bytes')
    command = [str(args.codex), 'exec', '--ignore-user-config', '--ephemeral', '--strict-config',
               '--sandbox', 'read-only', '--skip-git-repo-check', '--json', '--cd', str(workspace),
               '-m', 'gpt-5.6-luna', '-c', 'model_reasoning_effort="medium"']
    for key, value in overrides.items():
        command += ['-c', key + '=' + value]
    command += [prompt]
    return command, {'schema': 'yee.codex-luna-trial.v1', 'model': 'gpt-5.6-luna',
        'reasoning': 'medium', 'codex_sha256': manifest['codex_sha256'],
        'policy_probe': str(args.policy_probe), 'policy_probe_result_sha256': supervisor.sha(args.policy_probe/'result.json'),
        'runner_sha256': supervisor.sha(Path(__file__)), 'mcp_sha256': supervisor.sha(mcp),
        'prompt_sha256': supervisor.sha(args.prompt), 'command': command,
        'completion_instruction_sha256': supervisor.sha(policy),
        'effective_prompt_sha256': hashlib.sha256(prompt.encode()).hexdigest(),
        'host_skill_catalog': 'retained and included in actual usage',
        'tool_boundary': 'Yee MCP, read-only resource helpers, denied patch; no shell/agents',
        'native_approval': 'unchanged; operator must approve scope in Yee',
        'whole_task_boundary_verified': False, 'actual_provider_call': not args.prepare_only}


def run(args):
    command, manifest = prepare(args)
    if args.prepare_only:
        print(json.dumps({'prepared': True, 'actual_provider_call': False, 'model': manifest['model']}))
        return 0
    args.record.mkdir(mode=0o700)
    (args.record/'workspace').mkdir(mode=0o700)
    supervisor.write(args.record/'manifest.json', manifest)
    supervisor.write(args.record/'source-prompt.txt', args.prompt.read_text())
    supervisor.write(args.record/'prompt.txt', command[-1])
    env = {k: v for k, v in os.environ.items()
           if k in ('PATH', 'HOME', 'USER', 'TMPDIR', 'SHELL', 'LANG')}
    if args.timeline:
        yee_trial_timeline.append(args.timeline, 'execution_started', record=args.record)
    started = time.monotonic_ns()
    with (args.record/'stdout.jsonl').open('xb') as out, (args.record/'stderr.txt').open('xb') as err:
        os.chmod(out.name, 0o600)
        os.chmod(err.name, 0o600)
        try:
            lifecycle = supervisor.execute(command, env=env, cwd=args.record/'workspace',
                                           timeout=args.timeout, stdout=out, stderr=err)
        except BaseException as error:
            if args.timeline:
                yee_trial_timeline.append(args.timeline, 'execution_finished', record=args.record,
                    outcome={'status': 'raised', 'exception_type': type(error).__name__})
            raise
    elapsed = (time.monotonic_ns()-started)/1e9
    if args.timeline:
        yee_trial_timeline.append(args.timeline, 'execution_finished', record=args.record,
                                 outcome={'status': 'returned', 'returncode': lifecycle['returncode']})
    supervisor.write(args.record/'lifecycle.json', lifecycle)
    events = [json.loads(line) for line in (args.record/'stdout.jsonl').read_text().splitlines() if line.strip()]
    summary = {'model': 'gpt-5.6-luna', 'elapsed_seconds': elapsed, 'usage': normalize_usage(events),
               'lifecycle': lifecycle, 'success_verified': False,
               'note': 'Runner return is not fixture success or whole-task performance proof.'}
    supervisor.write(args.record/'summary.json', summary)
    print(json.dumps(summary))
    return lifecycle['returncode'] or (2 if summary['usage'] is None else 0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--prompt', type=Path, required=True)
    parser.add_argument('--policy-probe', type=Path, required=True)
    parser.add_argument('--codex', type=Path, default=Path('/Users/yongjunkim/.local/bin/codex'))
    parser.add_argument('--python', type=Path, default=Path(__file__).resolve().parents[2]/'.local-build/yee-mcp-venv/bin/python')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--timeline', type=Path)
    sys.exit(run(parser.parse_args()))
