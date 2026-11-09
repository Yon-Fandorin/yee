"""Windows native gate의 계획과 직렬 실행, 출처·결과 기록."""

import argparse
from collections import Counter
import ctypes
from ctypes import wintypes
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from native_gate_record import save_json, utc_now
from windows_build_provenance import build_inputs
from native_test_discovery import check_test_listing, is_disabled_test, matches_pattern, parse_test_listing
from test_tiers import load_manifest, resolve_filter

BUILD_SCHEMA = 'yee.windows-native-gate-build.v3'
SERIAL_FLAGS = (
    '--single-process-tests', '--test-launcher-preserve-desktop-windows',
    '--test-launcher-jobs=1', '--test-launcher-retry-limit=0', '--gtest_repeat=1',
)
DISPLAY_FLAGS = ('--enable-pixel-output-in-tests', '--disable-gpu')
HEADLESS_BINARIES = {'yee_interactive_ui_tests', 'site_controls_browsertests', 'browser_tests'}
HEADLESS_FLAGS = ('--headless', '--screen-info={1920x1080}', '--window-size=1280,900')
FOREGROUND_BINARIES = {'yee_interactive_ui_tests', 'site_controls_browsertests'}
FOREGROUND_HANDOFF = '--yee-test-foreground-handoff'


@dataclass(frozen=True)
class GateRun:
    gate: str
    name: str
    binary: str
    target: str
    pattern: str
    arguments: tuple[str, ...]
    tiers: str
    expected_patterns: tuple[str, ...] = ()


def artifact_record(path):
    before = file_signature(path)
    with path.open('rb') as stream:
        value = hashlib.file_digest(stream, 'sha256').hexdigest()
    if file_signature(path) != before:
        raise ValueError(f'읽는 동안 파일이 바뀌었다. {path}')
    return {**before, 'sha256': value}


def file_signature(path):
    if path.resolve(strict=True) != path.absolute() or not path.is_file():
        raise ValueError(f'실제 출력 파일이 필요하다. {path}')
    stat = path.stat()
    return {'bytes': stat.st_size, 'mtime_ns': stat.st_mtime_ns}


def read_configuration(repo):
    folder = repo / 'tools/dev'
    data = json.loads((folder / 'lib/windows_gate_modes.json').read_text(encoding='utf-8'))
    if data.get('schema_version') != 1:
        raise ValueError('Windows gate 계획 형식을 확인하라.')
    names = set(data['non_native'])
    for gate in data['gates'].values():
        filename = gate.get('script')
        if filename:
            if filename in names or not re.fullmatch(r'test-[a-z-]+\.sh', filename):
                raise ValueError(f'원본 gate 연결이 잘못되었다. {filename}')
            names.add(filename)
            script = (folder / filename).read_bytes().replace(b'\r\n', b'\n')
            if hashlib.sha256(script).hexdigest() != gate['sha256']:
                raise ValueError(f'{filename} 변경을 Windows gate에 반영해야 한다.')
    if names != {path.name for path in folder.glob('test-*.sh')}:
        raise ValueError('Windows gate 기준 목록을 원본 스크립트와 맞춰야 한다.')
    return data


def live_arguments(arguments):
    # 영상 ID를 경로로 해석하지 않으며 보고서는 사례 폴더에 둔다.
    video = os.environ.get('YEE_LIVE_YOUTUBE_VIDEO', '')
    if video and not re.fullmatch(r'[A-Za-z0-9_-]{11}', video):
        raise ValueError('YouTube 영상 ID는 11자여야 한다.')
    values = {'seconds': '120', 'min-content-seconds': '60'}
    result = [argument for argument in arguments
              if not any(argument.startswith(f'--yee-live-youtube-{name}=') for name in values)]
    for name, default in values.items():
        value = os.environ.get('YEE_LIVE_YOUTUBE_' + name.upper().replace('-', '_'), default)
        if not re.fullmatch('[1-9][0-9]*', value):
            raise ValueError(f'YouTube {name} 값은 양의 정수여야 한다.')
        result.append(f'--yee-live-youtube-{name}={value}')
    if os.environ.get('YEE_LIVE_YOUTUBE_SEEK') == '1':
        result.append('--yee-live-youtube-seek')
    if video:
        result.append(f'--yee-live-youtube-video={video}')
    return result


def plan(repo, gate_name, mode, tier, custom_filter=None):
    data = read_configuration(repo)
    manifest = load_manifest(repo / 'tests/tooling/test_tiers.json')
    if gate_name == 'required':
        if mode:
            raise ValueError('필수 전체 gate에는 별도 모드를 지정하지 않는다.')
        choices = [(name, gate['required']) for name, gate in data['gates'].items()]
    else:
        if gate_name not in data['gates']:
            raise ValueError(f'알 수 없는 Windows gate다. {gate_name}')
        gate = data['gates'][gate_name]
        if gate_name == 'youtube-live' and not mode:
            raise ValueError('YouTube 실험은 off 또는 on을 명시해야 한다.')
        choices = [(gate_name, [mode or gate['default']])]
    if custom_filter is not None and (gate_name != 'internal-pages' or not custom_filter):
        raise ValueError('사용자 필터는 Internal Pages에 비어 있지 않은 값으로 지정하라.')
    runs = []
    for name, modes in choices:
        gate = data['gates'][name]
        run_names = {}
        for selected in modes:
            if selected not in gate['modes']:
                raise ValueError(f'{name}의 모드를 확인하라. {selected}')
            run_names.update(dict.fromkeys(gate['modes'][selected]))
        for run_name in run_names:
            item = gate['runs'][run_name]
            binary = item['binary']
            pattern = ':'.join(item['include'])
            if item.get('exclude'):
                pattern += '-' + ':'.join(item['exclude'])
            if custom_filter and binary == 'site_controls_browsertests':
                pattern = custom_filter
            resolved, categories = resolve_filter(
                manifest, pattern, 'Windows', tier, binary, preserve_all=False)
            if not resolved:
                continue
            arguments = list(item.get('arguments', []))
            if tier == 'background':
                arguments.append('--yee-test-tier=background')
                if binary in HEADLESS_BINARIES:
                    arguments.extend(HEADLESS_FLAGS)
                    if item['display']:
                        # Windows Aura가 headless에서도 합성 픽셀을 그리게 한다.
                        arguments.append('--enable-pixel-output-in-tests')
                if name == 'internal-pages' and binary == 'site_controls_browsertests':
                    arguments.append('--allow-chrome-scheme-url')
            if item['display'] and tier != 'background':
                arguments.extend(DISPLAY_FLAGS)
                if not any(arg.startswith('--force-device-scale-factor=') for arg in arguments):
                    arguments.append('--force-device-scale-factor=1')
            if name == 'youtube-live':
                arguments = live_arguments(arguments)
            runs.append(GateRun(name, run_name, binary, data['targets'][binary],
                                pattern if tier == 'all' else resolved,
                                tuple(arguments), categories,
                                tuple(resolved.partition('-')[0].split(':'))))
    return runs


def artifact_name(output, path):
    relative = path.relative_to(output.parent.parent)
    return os.path.normcase(str(relative)).replace(os.sep, '/')


def is_reparse_point(path):
    return bool(getattr(path.lstat(), 'st_file_attributes', 0)
                & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def component_dlls(output):
    return {path for path in output.iterdir() if path.suffix.lower() == '.dll'}


def runtime_leaves(path):
    if not path.is_dir():
        return (path,)
    if is_reparse_point(path) or path.resolve() != path:
        raise ValueError(f'GN 런타임 의존 폴더가 다른 경로를 가리킨다. {path}')

    def fail_walk(error):
        raise error

    result = []
    for root, directories, files in os.walk(path, onerror=fail_walk):
        directory = Path(root)
        for name in directories:
            child = directory / name
            if is_reparse_point(child) or child.resolve() != child:
                raise ValueError(f'GN 런타임 의존 폴더가 다른 경로를 가리킨다. {child}')
        result.extend(directory / name for name in files)
    return result


def artifact_scope(output, targets):
    # 정적 Git checkout의 데이터와 별도로 설치된 런타임 도구도 GN 목록을 따른다.
    files = component_dlls(output)
    chromium = output.parent.parent
    expanded = {}
    membership = {}
    for name in targets:
        runtime = output / (name + '.runtime_deps')
        file_signature(runtime)
        selected = {runtime, output / (name + '.exe')}
        for entry in runtime.read_text(encoding='utf-8').splitlines():
            if not entry:
                continue
            relative = Path(entry)
            if relative.drive or relative.root:
                raise ValueError(f'GN 런타임 의존 경로가 상대 경로가 아니다. {entry}')
            path = Path(os.path.abspath(output / relative))
            if (path in (chromium, output) or not path.is_relative_to(chromium)
                    or (path.is_relative_to(output.parent) and not path.is_relative_to(output))):
                raise ValueError(f'GN 런타임 의존 경로가 다른 출력 또는 checkout을 가리킨다. {entry}')
            if path not in expanded:
                # 출력의 디버그 심볼·import library는 실행에서 읽지 않는다.
                expanded[path] = tuple(child for child in runtime_leaves(path)
                                       if not (child.is_relative_to(output)
                                               and child.suffix.lower() in ('.pdb', '.lib')))
            selected.update(expanded[path])
        files.update(selected)
        names = '\0'.join(sorted(artifact_name(output, path) for path in selected))
        membership[name] = hashlib.sha256(names.encode('utf-8')).hexdigest()
    return sorted(files), membership


def artifacts(output, files):
    return {artifact_name(output, path): artifact_record(path) for path in files}


def require_artifacts(output, manifest):
    for name, expected in manifest.items():
        path = output.parent.parent / name
        stat = path.stat()
        if (stat.st_size, stat.st_mtime_ns) != (expected['bytes'], expected['mtime_ns']):
            raise RuntimeError(f'검사 중 런타임 파일이 바뀌었다. {path}')


def require_artifact_scope(output, targets, paths, membership, manifest):
    current_paths, current_membership = artifact_scope(output, targets)
    if paths != current_paths or membership != current_membership:
        raise RuntimeError('출처를 확인하는 동안 런타임 파일 집합이 바뀌었다.')
    require_artifacts(output, manifest)


def require_idle(output):
    script = (
        "$ErrorActionPreference='Stop'; "
        "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); "
        "$active=@(Get-CimInstance Win32_Process -Property ProcessId,ExecutablePath | "
        "Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith("
        "$env:YEE_NATIVE_GATE_OUT,[StringComparison]::OrdinalIgnoreCase) } | "
        "Select-Object ProcessId,ExecutablePath); "
        "ConvertTo-Json -InputObject $active -Compress"
    )
    powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    result = subprocess.run(
        [str(powershell), '-NoProfile', '-NonInteractive', '-Command', script],
        creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True, check=True,
        env={**os.environ, 'YEE_NATIVE_GATE_OUT': str(output) + os.sep})
    active = json.loads(result.stdout or b'[]')
    if active:
        raise RuntimeError('검사 출력의 프로세스가 남아 있다. ' + json.dumps(active, ensure_ascii=False))


def create_build_evidence(args, inputs, runs, directory, output):
    targets = list(dict.fromkeys(run.target for run in runs))
    binary_names = {run.binary for run in runs}
    if args.provenance:
        encoded = args.provenance.read_bytes()
        record = json.loads(encoded)
        if (record.get('schema') != BUILD_SCHEMA or record.get('inputs') != inputs
                or record.get('exit_code') != 0 or not set(targets) <= set(record['targets'])):
            raise ValueError('선택한 소스·출력과 빌드 성공 기록이 다르다.')
        paths, membership = artifact_scope(output, binary_names)
        if any(record['runtime_membership'].get(name) != value for name, value in membership.items()):
            raise ValueError('빌드 성공 기록과 선택한 런타임 파일 집합이 다르다.')
        current = artifacts(output, paths)
        prefix = artifact_name(output, output) + '/'
        recorded_dlls = {name for name in record['artifacts']
                         if name.startswith(prefix) and '/' not in name[len(prefix):]
                         and name.lower().endswith('.dll')}
        current_dlls = {artifact_name(output, path) for path in component_dlls(output)}
        if recorded_dlls != current_dlls:
            raise ValueError('빌드 성공 기록과 현재 DLL 집합이 다르다.')
        if any(record['artifacts'].get(name) != info for name, info in current.items()):
            raise ValueError('빌드 성공 기록과 현재 검사 런타임 파일이 다르다.')
        require_artifact_scope(output, binary_names, paths, membership, current)
        save_json(directory / 'build-reference.json', {
            'path': str(args.provenance.resolve()), 'sha256': hashlib.sha256(encoded).hexdigest()})
        return current
    command = [sys.executable, str(args.depot_tools / 'autoninja.py'),
               '-C', str(output), '-j', str(args.jobs), *targets]
    started = utc_now()
    with (directory / 'build.log').open('wb') as stream:
        subprocess.run(command, cwd=args.chromium, stdout=stream, stderr=subprocess.STDOUT, check=True)
    paths, membership = artifact_scope(output, binary_names)
    current = artifacts(output, paths)
    if build_inputs(args.repo, args.chromium, output) != inputs:
        raise RuntimeError('검사 대상 빌드 중 소스 또는 GN 인수가 바뀌었다.')
    require_artifact_scope(output, binary_names, paths, membership, current)
    save_json(directory / 'build-provenance.json', {
        'schema': BUILD_SCHEMA, 'inputs': inputs, 'targets': targets, 'command': command,
        'started_utc': started, 'finished_utc': utc_now(), 'exit_code': 0,
        'runtime_membership': membership, 'artifacts': current})
    return current


def small_key(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()[:16]


def family_and_depth(name):
    suite, case = name.split('.', 1)
    depth = 0
    while case.startswith('PRE_'):
        case = case[4:]
        depth += 1
    return suite + '.' + case, depth


def discover(listing, pattern, arguments, expected_patterns=()):
    disabled = '--gtest_also_run_disabled_tests' in arguments
    report = check_test_listing(listing, pattern, also_run_disabled_tests=disabled)
    positive, _, negative = pattern.partition('-')
    included = (positive or '*').split(':')
    excluded = negative.split(':') if negative else []
    names = sorted(
        (name for name in parse_test_listing(listing)
         if (disabled or not is_disabled_test(name))
         and any(matches_pattern(name, part) for part in included)
         and not any(matches_pattern(name, part) for part in excluded)),
        key=lambda name: (family_and_depth(name)[0], -family_and_depth(name)[1]),
    )
    if len(names) != report['selected_tests'] or any(
            not re.fullmatch(r'[A-Za-z0-9_/]+\.[A-Za-z0-9_/]+', name) for name in names):
        raise ValueError('실행할 GTest 사례의 정확한 이름을 만들 수 없다.')
    missing = [part for part in expected_patterns
               if (disabled or not is_disabled_test(part))
               and not any(matches_pattern(name, part) for name in names)]
    return {**report, 'ok': report['ok'] and not missing, 'cases': names,
            'missing_manifest_patterns': missing}



def prepare_profile(profile):
    if profile.resolve() != profile.absolute():
        raise ValueError('검사 프로필 경로에 다른 위치로 향하는 연결이 있다.')
    owner = profile / '.yee-native-gate-profile.json'
    expected = {'schema': 'yee.windows-native-profile.v1', 'profile': str(profile)}
    if profile.exists():
        if not owner.is_file() or json.loads(owner.read_text(encoding='utf-8')) != expected:
            raise ValueError('기존 폴더에 이 검사의 프로필 소유 기록이 없다.')
        return
    profile.mkdir(parents=True, exist_ok=False)
    save_json(owner, expected)


def uses_desktop(run):
    return ('--enable-pixel-output-in-tests' in run.arguments
            and '--headless' not in run.arguments)


def wait_for_desktop_start():
    # 이 실행기의 창에서 시작해 Windows 전면화 권한을 자식에 전달한다.
    message_box = ctypes.WinDLL('user32', use_last_error=True).MessageBoxW
    message_box.argtypes = (wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.UINT)
    message_box.restype = ctypes.c_int
    print('Yee Windows native gate 시작 창에서 확인을 누르면 전면 검사를 실행한다.', flush=True)
    ok_cancel = 0x00000001
    set_foreground = 0x00010000
    result = message_box(None, '격리 프로필에서 Yee 전면 검사를 시작합니다.',
                         'Yee Windows native gate 시작', ok_cancel | set_foreground)
    if not result:
        raise ctypes.WinError()
    if result != 1:
        raise RuntimeError('사용자가 전면 검사 시작을 취소했다.')


def invoke(run, pattern, directory, profile, temporary, output, identities, *, listing=False):
    require_idle(output)
    require_artifacts(output, identities)
    if directory.resolve() != directory.absolute() or temporary.resolve() != temporary.absolute():
        raise ValueError('검사 기록 또는 임시 경로가 다른 위치로 연결되어 있다.')
    directory.mkdir(parents=True, exist_ok=False)
    temporary.mkdir(parents=True, exist_ok=False)
    prepare_profile(profile)
    outputs = {
        '--yee-live-youtube-report': directory / 'youtube-live.json',
        '--yee-live-window-capture': directory / 'live-window.png',
    }
    flags = []
    for flag in run.arguments:
        name = flag.partition('=')[0]
        if name in outputs:
            flag = f'{name}={outputs[name]}'
        flags.append(flag)
    handoff = not listing and run.binary in FOREGROUND_BINARIES and uses_desktop(run)
    if handoff:
        flags.append(FOREGROUND_HANDOFF)
        grant = ctypes.WinDLL('user32').AllowSetForegroundWindow
        grant.argtypes = (wintypes.DWORD,)
        grant.restype = wintypes.BOOL
    command = [str(output / (run.binary + '.exe')), *SERIAL_FLAGS, *flags,
               f'--user-data-dir={profile}', f'--gtest_filter={pattern}',
               f'--gtest_output=xml:{directory / "results.xml"}']
    if listing:
        command.append('--gtest_list_tests')
    record = {'command': command, 'profile': str(profile), 'temporary': str(temporary),
              'started_utc': utc_now(), 'exit_code': None}
    log = directory / 'stdout.log'
    inherited = {key: value for key, value in os.environ.items()
                 if not key.upper().startswith(('GTEST_', 'TEST_SHARD_'))}
    env = {**inherited, 'TEMP': str(temporary), 'TMP': str(temporary), 'TMPDIR': str(temporary)}
    timeout = 60 if listing else (2400 if run.gate == 'youtube-live' else 300)
    try:
        with log.open('wb') as stream:
            child = subprocess.Popen(command, cwd=output, env=env, stdout=stream,
                                     stderr=subprocess.STDOUT,
                                     stdin=subprocess.PIPE if handoff else None)
            record['pid'] = child.pid
            if handoff:
                try:
                    record['foreground_granted'] = bool(grant(child.pid))
                    child.communicate(input=b'Y', timeout=timeout)
                    record['exit_code'] = child.returncode
                finally:
                    if not child.stdin.closed:
                        child.stdin.close()
            else:
                record['exit_code'] = child.wait(timeout=timeout)
    finally:
        record['finished_utc'] = utc_now()
        save_json(directory / 'process.json', record)
    require_idle(output)
    require_artifacts(output, identities)
    return record['exit_code'], log


def xml_result(path, name, exit_code):
    root = ET.parse(path).getroot()
    suites = root.findall('testsuite')
    cases = list(root.iter('testcase'))
    if (root.tag != 'testsuites' or len(suites) != 1 or len(cases) != 1
            or suites[0].findall('testcase') != cases):
        raise ValueError('GTest XML에 요청한 suite와 사례 한 개가 있어야 한다.')
    suite = suites[0]
    case = cases[0]
    actual = case.get('classname', '') + '.' + case.get('name', '')
    if actual != name or suite.get('name') != case.get('classname'):
        raise ValueError(f'XML 사례 이름이 다르다. {actual}')
    failed = case.find('failure') is not None or case.find('error') is not None
    skipped = case.find('skipped') is not None or case.get('result') in ('skipped', 'suppressed')
    declared = {'failures': int(case.find('failure') is not None),
                'errors': int(case.find('error') is not None), 'skipped': int(skipped)}
    for summary in (root, suite):
        if summary.get('tests') != '1' or any(
                key in summary.attrib and int(summary.get(key)) != value
                for key, value in declared.items()):
            raise ValueError('XML 결과 집계와 사례가 다르다.')
    if exit_code or failed or (not skipped and (
            case.get('status') != 'run' or case.get('result', 'completed') != 'completed')):
        return 'failed'
    return 'skipped' if skipped else 'passed'


def execute_run(run, directory, runtime, output, identities, results, *, list_only):
    directory.mkdir()
    code, log = invoke(run, run.pattern, directory / 'list', runtime / 'list-profile',
                       runtime / 'list-temp', output, identities, listing=True)
    discovery = discover(log.read_text(encoding='utf-8-sig', errors='replace'),
                         run.pattern, run.arguments, run.expected_patterns)
    discovery['exit_code'] = code
    save_json(directory / 'discovery.json', discovery)
    if code or not discovery['ok']:
        raise ValueError(f'{run.gate}/{run.name}의 필수 패턴을 발견하지 못했다.')
    if list_only:
        results['listed'] = len(discovery['cases'])
        return
    failing_families = set()
    for name in discovery['cases']:
        family, _ = family_and_depth(name)
        key = small_key(name)
        case = directory / key
        if family in failing_families:
            case.mkdir()
            save_json(case / 'result.json', {
                'case': name, 'status': 'blocked', 'reason': 'PRE family did not pass'})
            results['blocked'] += 1
            continue
        code, _ = invoke(run, name, case, runtime / 'p' / small_key(family),
                         runtime / 't' / key, output, identities)
        try:
            result = xml_result(case / 'results.xml', name, code)
        except (OSError, ValueError, ET.ParseError) as error:
            result = 'failed'
            save_json(case / 'error.json', {'reason': str(error)})
        save_json(case / 'result.json', {'case': name, 'status': result})
        results[result] += 1
        if result != 'passed':
            failing_families.add(family)
        print(f'{run.gate}/{run.name} {name} {result}', flush=True)


def execute(args, runs):
    if os.name != 'nt':
        raise ValueError('Windows native 검사는 Windows에서 실행한다.')
    selected_output = args.chromium / 'out' / args.out_name
    output = selected_output.resolve(strict=True)
    if output != selected_output or is_reparse_point(selected_output):
        raise ValueError(f'검사 출력이 다른 경로를 가리킨다. {selected_output}')
    require_idle(output)
    inputs = build_inputs(args.repo, args.chromium, output)
    identifier = uuid.uuid4().hex
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    directory = args.local_build / 'logs/native-gates' / (stamp + '-' + identifier[:12])
    runtime = args.local_build / 'wg' / identifier[:12]
    directory.mkdir(parents=True, exist_ok=False)
    runtime.mkdir(parents=True, exist_ok=False)
    print(f'Windows native 검사 기록 {directory}', flush=True)
    save_json(directory / 'plan.json', [asdict(run) for run in runs])
    results = Counter()
    status = 'running'
    try:
        identities = create_build_evidence(args, inputs, runs, directory, output)
        # 사례마다 확인하는 파일은 로드되는 공용 출력과 언어 리소스다.
        # GN이 나열한 나머지 데이터는 gate 경계와 종료 시 확인한다.
        prefix = artifact_name(output, output) + '/'
        case_identities = {name: info for name, info in identities.items()
                           if name.startswith(prefix)
                           and ('/' not in name[len(prefix):] or name.startswith(prefix + 'locales/'))}
        start_on_desktop = args.start_on_desktop
        for index, run in enumerate(runs):
            if start_on_desktop and uses_desktop(run):
                wait_for_desktop_start()
                start_on_desktop = False
            counts = Counter()
            run_directory = directory / str(index)
            try:
                execute_run(run, run_directory, runtime / str(index), output, case_identities, counts,
                            list_only=args.list_only)
                require_artifacts(output, identities)
            finally:
                results.update(counts)
                save_json(run_directory / 'summary.json', dict(counts))
        binary_names = {run.binary for run in runs}
        paths, membership = artifact_scope(output, binary_names)
        current = artifacts(output, paths)
        if current != identities:
            raise RuntimeError('검사 중 바이너리 집합이 바뀌었다.')
        if build_inputs(args.repo, args.chromium, output) != inputs:
            raise RuntimeError('검사 중 소스 또는 GN 인수가 바뀌었다.')
        require_artifact_scope(output, binary_names, paths, membership, current)
        if results['failed'] or results['blocked']:
            status = 'failed'
        elif args.list_only:
            status = 'listed'
        elif results['skipped']:
            status = 'complete-with-skips' if results['passed'] else 'skipped'
        else:
            status = 'complete'
    except BaseException as error:
        status = 'blocked'
        save_json(directory / 'error.json', {'type': type(error).__name__, 'message': str(error)})
        raise
    finally:
        save_json(directory / 'summary.json', {
            'status': status, 'counts': dict(results),
            'all_selected_cases_passed': status == 'complete'})
    return 1 if status == 'failed' else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--gate', default='required')
    parser.add_argument('--mode')
    parser.add_argument('--tier', choices=('background', 'foreground', 'all'), default='background')
    parser.add_argument('--filter')
    parser.add_argument('--print-filters', action='store_true')
    parser.add_argument('--list-only', action='store_true')
    parser.add_argument('--start-on-desktop', action='store_true')
    parser.add_argument('--chromium', type=Path)
    parser.add_argument('--local-build', type=Path)
    parser.add_argument('--depot-tools', type=Path)
    parser.add_argument('--out-name')
    parser.add_argument('--jobs', type=int)
    parser.add_argument('--provenance', type=Path)
    args = parser.parse_args(argv)
    try:
        args.repo = args.repo.resolve(strict=True)
        runs = plan(args.repo, args.gate, args.mode, args.tier, args.filter)
        if args.start_on_desktop and (args.print_filters or args.list_only
                                      or not any(map(uses_desktop, runs))):
            raise ValueError('전면 검사 실행에만 --start-on-desktop을 지정한다.')
        if args.print_filters:
            print(json.dumps([asdict(run) for run in runs], ensure_ascii=False, indent=2))
            return 0
        if not runs:
            print('선택한 tier에서 실행하는 사례가 없다.')
            return 0
        if not all((args.chromium, args.local_build, args.depot_tools, args.out_name, args.jobs)):
            raise ValueError('Chromium·출력·로컬 경로·빌드 작업 수가 필요하다.')
        if args.jobs < 1 or not re.fullmatch(r'[A-Za-z0-9._-]+', args.out_name) or args.out_name in ('.', '..'):
            raise ValueError('빌드 작업 수 또는 출력 이름을 확인하라.')
        args.chromium = args.chromium.resolve(strict=True)
        args.local_build = args.local_build.resolve()
        return execute(args, runs)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Windows native 검사 실패. {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
