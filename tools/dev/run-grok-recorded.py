#!/usr/bin/env python3
"""Run one bounded Grok CLI call, preserving raw output locally.

Requires explicit prompt, isolated cwd, binary and a NEW record directory.
Never forwards usage from past calls. Does not interpret/execute model output.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import sys
from pathlib import Path
import subprocess
import time
import uuid


def normalize_usage(result):
    usage = result.get('usage')
    if not isinstance(usage, dict):
        return None
    required = ('input_tokens', 'cache_read_input_tokens', 'cache_creation_input_tokens',
                'output_tokens', 'total_tokens')
    if any(type(usage.get(k)) is not int or usage[k] < 0 for k in required):
        return None
    inclusive = sum(usage[k] for k in required[:3])
    if inclusive + usage['output_tokens'] != usage['total_tokens']:
        return None
    reasoning = usage.get('reasoning_tokens')
    if reasoning is not None and (type(reasoning) is not int or not 0 <= reasoning <= usage['output_tokens']):
        return None
    return {'input_tokens': inclusive, 'output_tokens': usage['output_tokens'],
            'cached_input_tokens': usage['cache_read_input_tokens'],
            'reasoning_tokens': reasoning, 'total_tokens': usage['total_tokens']}


def private_write(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def resume_session(parent, identity):
    """Only resume a measured session with identical explicitly scoped setup."""
    for path, directory in ((parent, True), (parent / 'summary.json', False)):
        info = path.lstat()
        if (info.st_uid != os.getuid() or info.st_mode & 0o077 or
                not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode))):
            raise ValueError('resume record must be owned, private and non-symlink')
    previous = json.loads((parent / 'summary.json').read_text())
    if (not isinstance(previous, dict) or previous.get('session_identity') != identity or previous.get('returncode') != 0 or
            previous.get('timed_out') is not False or not previous.get('usage') or
            previous.get('authority_audit', {}).get('passed') is not True):
        raise ValueError('resume requires a successful scoped record with unchanged identity')
    session = previous.get('session_id')
    if not isinstance(session, str) or str(uuid.UUID(session)) != session:
        raise ValueError('resume requires an explicit canonical session UUID')
    return session


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def direct_arguments(commands, max_turns):
    """Exact shell grants, never a catch-all or shell composition grant."""
    if not commands or not 2 <= max_turns <= 30:
        raise ValueError('direct testing requires exact commands and 2..30 turns')
    for command in commands:
        if not re.fullmatch(r'[A-Za-z0-9_./ =,:@+-]+', command):
            raise ValueError('direct command contains unsupported shell/rule syntax')
    result = ['--tools', 'Bash', '--permission-mode', 'dontAsk']
    for command in commands:
        result += ['--allow', 'Bash(' + command + ')']
    return result


def plan_file_arguments(path, python_plan=False):
    extension = 'py' if python_plan else 'jsonl'
    if not re.fullmatch(r'/private/tmp/yee-agent\.[A-Za-z0-9_]+/[A-Za-z0-9_-]+\.' + extension, str(path)):
        raise ValueError('plan file must have the expected extension inside a private test directory')
    for target, directory in ((path.parent, True), (path, False)):
        info = target.lstat()
        regular = stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)
        if not regular or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('plan file and directory must be owned, private, non-symlink paths')
    return ['--allow', f'Edit({path})', '--allow', f'Read({path})']


def mcp_arguments(name, max_turns, handoff=False):
    if name not in ('yee__yee_browser', 'aside__repl') or not 2 <= max_turns <= 30:
        raise ValueError('MCP trial requires an exact supported browser tool and 2..30 turns')
    if handoff and name != 'aside__repl':
        raise ValueError('host handoff is supported only with Aside repl')
    result = ['--tools', 'search_tool,use_tool', '--permission-mode', 'dontAsk',
              '--allow', f'MCPTool({name})']
    if handoff:
        result += ['--allow', 'MCPTool(trial_host__request_user)']
    return result


def form_ready(value):
    if not isinstance(value, dict):
        return False
    snapshot = value.get('snapshot', '')
    if not isinstance(snapshot, str):
        return False
    return (value.get('ok') is True and value.get('truncated') is False
            and value.get('viewport') == {'width': 1440, 'height': 900}
            and isinstance(value.get('document'), str)
            and 'origin="http://127.0.0.1:8766"' in snapshot.split('\n', 1)[0]
            and re.search(r'^\+@\d+ field "Name" value=""$', snapshot, re.M) is not None
            and re.search(r'^\+@\d+ text "Waiting for input"$', snapshot, re.M) is not None)


def require_yee_ready(bridge, record):
    if not re.fullmatch(r'/private/tmp/yee-agent\.[A-Za-z0-9]+', str(bridge)):
        raise ValueError('readiness requires an explicit private test bridge')
    started = time.time()
    result = subprocess.run([sys.executable, str(Path(__file__).with_name('yee-browser.py')),
        '--bridge', str(bridge), '--timeout', '5', '--compact',
        '--record', str(record / 'readiness-native.jsonl'), 'observe', '--full'],
        capture_output=True, timeout=10, check=False)
    try:
        value = json.loads(result.stdout)
    except ValueError:
        value = None
    ready = result.returncode == 0 and form_ready(value)
    private_write(record / 'readiness.json', {'ready': ready, 'elapsed_seconds': time.time() - started,
                  'returncode': result.returncode, 'observation': value,
                  'stderr': result.stderr.decode('utf-8', errors='replace')})
    return ready


def parse_grok_output(data, direct=False):
    if not direct:
        return json.loads(data)
    events = [json.loads(line) for line in data.splitlines() if line.strip()]
    terminal = [e for e in events if isinstance(e, dict) and e.get('type') == 'result']
    if len(terminal) != 1:
        return {}
    result = terminal[0]
    usage = result.get('usage')
    # Messages-stream totals omit total_tokens. Validate against every assistant
    # turn before deriving arithmetic total; do not reinterpret absent usage as 0.
    fields = ('input_tokens', 'output_tokens', 'cache_read_input_tokens',
              'cache_creation_input_tokens')
    turns = [e['message'].get('usage') for e in events
             if isinstance(e, dict) and e.get('type') == 'assistant'
             and isinstance(e.get('message'), dict)]
    if (isinstance(usage, dict) and turns
            and all(isinstance(u, dict) and all(type(u.get(k)) is int and u[k] >= 0
                    for k in fields) for u in [usage, *turns])
            and all(usage[k] == sum(u[k] for u in turns) for k in fields)):
        derived = dict(usage)
        derived.setdefault('total_tokens', sum(usage[k] for k in fields))
        result = {**result, 'usage': derived}
    return result


def audit_authority(data, commands, editable_plan=None, mcp_tool=None, handoff=False):
    """Check executed/requested tool scope; CLI allow flags are not isolation.

    Only inspect tool names/arguments, never copy thinking or tool outputs.
    This retrospective gate cannot undo a call or prevent data disclosure.
    """
    violations = []
    count = 0
    for line in data.splitlines():
        if not line.strip():
            continue
        event = json.loads(line)
        for block in event.get('message', {}).get('content', []):
            if block.get('type') != 'tool_use':
                continue
            count += 1
            name, args = block.get('name'), block.get('input', {})
            allowed = False
            if isinstance(args, dict):
                if mcp_tool:
                    permitted = {mcp_tool}
                    if handoff and mcp_tool == 'aside__repl':
                        permitted.add('trial_host__request_user')
                    allowed = (name == 'search_tool' or
                               (name == 'use_tool' and args.get('tool_name') in permitted))
                elif name == 'run_terminal_command':
                    allowed = args.get('command') in commands
                elif editable_plan and name in ('read_file', 'search_replace'):
                    key = 'target_file' if name == 'read_file' else 'file_path'
                    allowed = args.get(key) == str(editable_plan)
            if not allowed:
                violations.append({'tool_call_id': block.get('id'), 'tool': name})
    return {'passed': not violations, 'tool_calls': count, 'violations': violations,
            'scope': 'retrospective requested tool scope, not runtime isolation'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--cwd', required=True, type=Path)
    parser.add_argument('--prompt', required=True, type=Path)
    parser.add_argument('--record', required=True, type=Path)
    parser.add_argument('--model', required=True, help='explicit Grok CLI model ID')
    parser.add_argument('--provider', choices=('grok', 'kimi'), default='grok')
    parser.add_argument('--direct-command', action='append', default=[],
                        help='exact shell command the Grok tester may execute; repeatable')
    parser.add_argument('--mcp-tool', help='exact browser tool in a task-local MCP configuration')
    parser.add_argument('--host-handoff', action='store_true',
                        help='also allow the exact trial_host request_user tool with Aside')
    parser.add_argument('--mcp-wire-record', type=Path,
                        help='private raw relay journal for a scoped Aside MCP trial')
    parser.add_argument('--trust-project', action='store_true',
                        help='grant Grok folder trust only for this isolated test workspace')
    parser.add_argument('--require-yee-ready', type=Path,
                        help='abort before model call unless this test bridge has the exact empty 1440x900 form')
    parser.add_argument('--max-turns', type=int, default=1)
    parser.add_argument('--plan-file', type=Path,
                        help='existing private test-plan file; permit only its read/edit, never product source')
    parser.add_argument('--python-plan', type=Path,
                        help='private Browser Use Python plan; code-execution authority, not a sandbox')
    parser.add_argument('--timeout', type=int, default=120, choices=range(1, 601), metavar='1..600')
    parser.add_argument('--resume-record', type=Path,
                        help='explicit previous private record; same model/binary/cwd/MCP config required')
    parser.add_argument('--kimi-review-profile', type=Path,
                        help='fixed no-tools Kimi review profile, not a browser execution benchmark')
    args = parser.parse_args()
    if args.python_plan:
        parser.error('Python-plan model trials disabled: Grok file/shell grants did not enforce '
                     'the intended scope in live validation; use only after authority isolation is verified')
    if args.direct_command:
        parser.error('file/shell model trials disabled pending verified effective authority isolation; '
                     'exact allow rules are not a proven exclusive boundary')
    if args.require_yee_ready and args.mcp_tool != 'yee__yee_browser':
        parser.error('Yee readiness requires Yee MCP mode')
    if args.trust_project and not args.mcp_tool:
        parser.error('--trust-project is permitted only for a scoped MCP trial')
    if args.mcp_wire_record:
        if (args.mcp_tool != 'aside__repl' or not args.mcp_wire_record.is_absolute()
                or not str(args.mcp_wire_record).startswith('/private/tmp/')):
            parser.error('--mcp-wire-record requires an absolute private Aside MCP journal path')
    if args.resume_record and (not args.mcp_tool or not args.resume_record.is_absolute()):
        parser.error('resume-record requires MCP mode and an absolute record path')
    if args.resume_record:
        parser.error('resume execution held pending per-instance native logs and incremental/replayed '
                     'usage validation; identity validation scaffolding is not a runtime gate')
    if args.kimi_review_profile:
        expected_profile = ('---\nname: yee-review\ndescription: Review supplied Yee evidence only\n'
                            'tools: []\nsubagents: []\n---\n${base_prompt}\n')
        if (args.provider != 'kimi' or not args.kimi_review_profile.is_absolute() or
                args.kimi_review_profile.read_text() != expected_profile):
            parser.error('Kimi review requires the exact no-tools profile preserving base_prompt')
    direct_args = []
    if args.host_handoff and args.mcp_tool != 'aside__repl':
        parser.error('host handoff requires the exact Aside repl tool')
    if args.mcp_tool:
        if args.direct_command or args.plan_file or args.python_plan or args.provider != 'grok':
            parser.error('MCP mode cannot combine with shell/file grants or Kimi')
        try:
            direct_args = mcp_arguments(args.mcp_tool, args.max_turns, args.host_handoff)
        except ValueError as exc:
            parser.error(str(exc))
    elif args.direct_command:
        if args.provider != 'grok':
            parser.error('direct Kimi test runner is not yet implemented')
        try:
            direct_args = direct_arguments(args.direct_command, args.max_turns)
        except ValueError as exc:
            parser.error(str(exc))
    elif args.max_turns != 1:
        parser.error('multiple turns require explicit direct-command grants')
    if args.plan_file and args.python_plan:
        parser.error('choose one plan format')
    editable_plan = args.plan_file or args.python_plan
    if editable_plan:
        if not direct_args:
            parser.error('plan-file requires a scoped direct Grok test')
        try:
            direct_args += plan_file_arguments(editable_plan, bool(args.python_plan))
        except (ValueError, OSError) as exc:
            parser.error(str(exc))
        direct_args[1] = 'Bash,search_replace,read_file'
    for path in (args.binary, args.cwd, args.prompt, args.record):
        if not path.is_absolute():
            parser.error('all paths must be absolute')
    if not args.cwd.is_dir() or (args.cwd / '.git').exists():
        parser.error('cwd must be an existing isolated, non-repository directory')
    identity = None
    resumed_session = None
    if args.mcp_tool:
        config = args.cwd / '.grok' / 'config.toml'
        identity = {'cwd': str(args.cwd), 'binary': str(args.binary), 'model': args.model,
                    'tool': args.mcp_tool,
                    'binary_sha256': file_sha256(args.binary),
                    'mcp_config_sha256': hashlib.sha256(config.read_bytes()).hexdigest()}
        if args.host_handoff:
            identity['host_tool'] = 'trial_host__request_user'
        if args.resume_record:
            try:
                resumed_session = resume_session(args.resume_record, identity)
            except (ValueError, OSError, TypeError) as exc:
                parser.error(str(exc))
    prompt = args.prompt.read_text(encoding='utf-8')
    if len(prompt.encode('utf-8')) > 65536:
        parser.error('prompt exceeds 64 KiB')
    args.record.mkdir(mode=0o700, parents=False, exist_ok=False)
    # Persist the exact local task input, not credentials or previous telemetry.
    private_write(args.record / 'input.json', {'prompt': prompt})
    if args.require_yee_ready:
        try:
            ready = require_yee_ready(args.require_yee_ready, args.record)
        except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
            private_write(args.record / 'readiness-error.json', {'error': str(exc)})
            ready = False
        if not ready:
            rejected = {'model_invoked': False, 'preflight_failed': True, 'usage': None,
                        'returncode': None, 'timed_out': False,
                        'coverage': 'setup failed; no model invocation; not a browser task success'}
            private_write(args.record / 'summary.json', rejected)
            print(json.dumps({'record': str(args.record), **rejected}))
            return 2
    argv = [str(args.binary), '--model', args.model, '--no-auto-update', '--no-subagents', '--disable-web-search',
            '--max-turns', str(args.max_turns), '--output-format',
            'streaming-messages-json' if direct_args else 'json', *direct_args, '-p', prompt]
    if args.trust_project:
        # Grok records this one private folder in its folder-trust store.  It is
        # needed for the project-local MCP definition, never for the product
        # checkout or a global MCP configuration.
        argv.insert(1, '--trust')
    if resumed_session:
        argv.extend(['--resume', resumed_session])
    if args.provider == 'kimi':
        argv = [str(args.binary), '--model', args.model, '--skills-dir', str(args.cwd),
                '--output-format', 'stream-json', '-p', prompt]
        if args.kimi_review_profile:
            argv.extend(['--agent-file', str(args.kimi_review_profile)])
    process_started_monotonic_ns = time.monotonic_ns()
    process_started_wall_ns = time.time_ns()
    started = process_started_wall_ns / 1_000_000_000
    returncode, timed_out = None, False
    out_fd = os.open(args.record / 'stdout.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    err_fd = os.open(args.record / 'stderr.txt', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(out_fd, 'wb') as out, os.fdopen(err_fd, 'wb') as err:
        try:
            child = subprocess.run(argv, cwd=args.cwd, stdout=out, stderr=err,
                                   stdin=subprocess.DEVNULL, timeout=args.timeout, check=False)
            returncode = child.returncode
        except subprocess.TimeoutExpired:
            timed_out = True
        except OSError as exc:
            err.write(str(exc).encode('utf-8'))
    process_finished_monotonic_ns = time.monotonic_ns()
    process_finished_wall_ns = time.time_ns()
    process_timing = {
        'clock': 'monotonic_ns',
        'started_monotonic_ns': process_started_monotonic_ns,
        'finished_monotonic_ns': process_finished_monotonic_ns,
        'elapsed_seconds': (process_finished_monotonic_ns - process_started_monotonic_ns) / 1_000_000_000,
        'started_wall_ns': process_started_wall_ns,
        'finished_wall_ns': process_finished_wall_ns,
    }
    result = None
    try:
        with (args.record / 'stdout.json').open('rb') as stream:
            data = stream.read(8 * 1024 * 1024 + 1)
        if len(data) <= 8 * 1024 * 1024:
            result = parse_grok_output(data, bool(direct_args))
    except (ValueError, UnicodeError):
        pass
    if not isinstance(result, dict):
        result = {}
    if args.provider == 'kimi' and len(data) <= 8 * 1024 * 1024:
        try:
            events = [json.loads(line) for line in data.splitlines() if line.strip()]
            messages = [event for event in events if event.get('role') == 'assistant'
                        and isinstance(event.get('content'), str) and event['content']]
            result = {'text': messages[0]['content']} if len(messages) == 1 else {}
        except (ValueError, UnicodeError, AttributeError):
            result = {}
    summary = {'binary': str(args.binary), 'provider': args.provider,
               'model_invoked': True, 'preflight_failed': False,
               'required_yee_readiness': str(args.require_yee_ready) if args.require_yee_ready else None,
               'requested_model': args.model, 'started_unix': started,
               'model_process_timing': process_timing,
               'session_identity': identity, 'session_id': result.get('session_id'),
               'resume_parent_record': str(args.resume_record) if args.resume_record else None,
               'requested_resume_session': resumed_session,
               'kimi_review_only': bool(args.kimi_review_profile),
               'elapsed_seconds': process_timing['elapsed_seconds'], 'returncode': returncode,
               'timed_out': timed_out, 'usage': normalize_usage(result),
               'reported_cost_usd': result.get('total_cost_usd'),
               'model_usage': result.get('modelUsage'),
               'coverage': ('direct tool-enabled trial; success requires independent trace validation'
                            if direct_args else 'one CLI invocation only; not a complete browser task'),
               'allowed_commands': args.direct_command,
               'allowed_mcp_tool': args.mcp_tool,
               'allowed_host_tool': 'trial_host__request_user' if args.host_handoff else None,
               'mcp_wire_record': str(args.mcp_wire_record) if args.mcp_wire_record else None,
               'project_trust_granted': args.trust_project,
               'editable_test_plan': str(editable_plan) if editable_plan else None,
               'test_plan_format': 'python' if args.python_plan else ('jsonl' if args.plan_file else None),
               'max_turns': args.max_turns,
               'billing_reconciled': False}
    if direct_args:
        try:
            if len(data) > 8 * 1024 * 1024:
                raise ValueError('oversized evidence')
            summary['authority_audit'] = audit_authority(
                data, args.direct_command, editable_plan, args.mcp_tool, args.host_handoff)
        except (ValueError, TypeError, AttributeError):
            summary['authority_audit'] = {'passed': False, 'error': 'incomplete or malformed tool evidence'}
    private_write(args.record / 'summary.json', summary)
    print(json.dumps({'record': str(args.record), **summary}, ensure_ascii=False))
    return 0 if (returncode == 0 and result and
                 summary.get('authority_audit', {}).get('passed', True)) else 1


if __name__ == '__main__':
    raise SystemExit(main())
