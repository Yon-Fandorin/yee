#!/usr/bin/env python3
"""선택한 native 검사의 소스·바이너리·발견 수·XML 결과를 기록한다."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from native_test_discovery import check_test_listing
from background_test import BackgroundObserver, run_background_command


XML_MISMATCH = 21
RECORD_ERROR = 22
OBSERVATION_FAILED = 23


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace('+00:00', 'Z')


def save_json(path, value):
    temporary = path.with_name(path.name + f'.{os.getpid()}.tmp')
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def binary_identity(binary, directory):
    """같은 절대 경로·크기·ns mtime의 바이너리는 저장한 해시를 재사용한다."""
    stat = binary.stat()
    identity = {'path': str(binary), 'size_bytes': stat.st_size, 'mtime_ns': stat.st_mtime_ns}
    cache_dir = directory / '.binary-sha256'
    cache_dir.mkdir(exist_ok=True)
    cache_file = cache_dir / (hashlib.sha256(os.fsencode(binary)).hexdigest() + '.json')
    try:
        cached = json.loads(cache_file.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        cached = {}
    reused = (isinstance(cached, dict) and all(cached.get(key) == value for key, value in identity.items())
              and isinstance(cached.get('sha256'), str)
              and re.fullmatch(r'[a-f0-9]{64}', cached['sha256']) is not None)
    if reused:
        digest = cached['sha256']
    else:
        digest = hashlib.sha256()
        with binary.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        digest = digest.hexdigest()
        current = binary.stat()
        if (current.st_size, current.st_mtime_ns) != (stat.st_size, stat.st_mtime_ns):
            raise ValueError('해시 계산 중 바이너리가 변경되었다.')
        save_json(cache_file, {**identity, 'sha256': digest})
    return {**identity, 'mtime_utc': datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
            'sha256': digest, 'sha256_cached': reused}


def source_identity(root):
    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(root), *arguments])

    head = git('rev-parse', 'HEAD').decode().strip()
    diff = git('diff', '--no-ext-diff', '--no-textconv', '--no-color', 'HEAD', '--binary')
    paths = sorted(path for path in git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')
                   if path)
    untracked = b''.join(path + b'\0' for path in paths)
    contents = hashlib.sha256()
    for path in paths:
        source = root / os.fsdecode(path)
        data = os.fsencode(os.readlink(source)) if source.is_symlink() else source.read_bytes()
        contents.update(path + b'\0' + hashlib.sha256(data).digest())
    return {'head': head, 'diff_head_binary_sha256': hashlib.sha256(diff).hexdigest(),
            'untracked_paths_sha256': hashlib.sha256(untracked).hexdigest(),
            'untracked_contents_sha256': contents.hexdigest(),
            'untracked_paths': [os.fsdecode(path) for path in paths]}


def output_location(arguments, directory):
    """GTest의 파일·디렉터리 출력 지정과 기본 XML 경로를 해석한다."""
    outputs = []
    for index, argument in enumerate(arguments):
        if argument.startswith('--gtest_output='):
            outputs.append(argument.partition('=')[2])
        elif argument == '--gtest_output':
            if index + 1 == len(arguments) or arguments[index + 1].startswith('--'):
                raise ValueError('--gtest_output의 값이 필요하다.')
            outputs.append(arguments[index + 1])
    if len(outputs) > 1:
        raise ValueError('--gtest_output은 하나만 지정할 수 있다.')
    if not outputs:
        path = directory / 'results.xml'
        arguments.append(f'--gtest_output=xml:{path}')
        return path, False
    output_format, separator, destination = outputs[0].partition(':')
    if output_format != 'xml':
        return None, False
    if separator and not destination:
        raise ValueError('--gtest_output=xml:에는 파일 또는 디렉터리 경로가 필요하다. '
                         '기록 폴더의 기본 XML을 쓰려면 --gtest_output을 생략하라.')
    path = Path(destination if separator else 'test_detail.xml').absolute()
    return path, bool(separator and destination.endswith('/'))


def xml_files(location, is_directory, binary):
    if location is None:
        return {}
    paths = ([path for path in location.glob('*.xml')
              if re.fullmatch(re.escape(binary.name) + r'(?:_\d+)?\.xml', path.name)]
             if is_directory else [location])
    found = {}
    for path in paths:
        try:
            stat = path.stat()
            found[str(path)] = [stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_ino]
        except FileNotFoundError:
            pass
    return found


def summarize_xml(location, is_directory, binary, before):
    after = xml_files(location, is_directory, binary)
    changed = [path for path, stat in after.items() if before.get(path) != stat]
    summary = {'path': str(location) if location else None, 'exists': bool(after),
               'fresh': len(changed) == 1}
    if len(changed) != 1:
        summary['error'] = '이번 실행이 작성한 XML 파일 하나가 필요하다.'
        return summary
    summary['path'] = changed[0]
    try:
        root = ET.parse(changed[0]).getroot()
        if root.tag not in ('testsuites', 'testsuite'):
            raise ValueError('GTest testsuites 또는 testsuite 루트가 필요하다.')
        suites = [suite for suite in root.iter('testsuite') if not suite.findall('testsuite')]
        totals = {key: sum(int(suite.attrib[key] if key == 'tests' else suite.get(key, '0'))
                           for suite in suites or [root]) for key in ('tests', 'failures', 'errors')}
        if any(value < 0 for value in totals.values()):
            raise ValueError('XML 사례 수는 음수일 수 없다.')
        if any(key in root.attrib and int(root.attrib[key]) != value for key, value in totals.items()):
            raise ValueError('XML 루트와 스위트의 사례 수가 다르다.')
        summary.update(totals)
    except (OSError, ET.ParseError, KeyError, ValueError) as error:
        summary['error'] = str(error)
    return summary


def run_gate(args):
    arguments = list(args.arguments)
    filters = [arg.partition('=')[2] for arg in arguments if arg.startswith('--gtest_filter=')]
    if len(filters) != 1:
        print('Native 검사는 명시한 --gtest_filter= 인수 하나가 필요하다.', file=sys.stderr)
        return 2
    binary = args.binary.resolve(strict=True)
    parent = args.build_root.resolve() / 'logs/native-gates'
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    directory = parent / f'{stamp}-{binary.name}-{args.pid}'
    # 인수 오류는 기록 폴더나 바이너리 실행 전에 보고한다.
    try:
        location, is_directory = output_location(arguments, directory)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2
    directory.mkdir(parents=True)
    print(f'Native 검사 기록: {directory}', file=sys.stderr)
    record = {'schema_version': 1, 'started_at_utc': utc_now(), 'ended_at_utc': None,
              'pid': args.pid, 'build_root': str(args.build_root.resolve()),
              'out_name': args.out_name, 'out_dir': str(args.out_dir.resolve()),
              'build_root_explicit': args.root_explicit == 'true',
              'out_name_explicit': args.out_explicit == 'true', 'cwd': str(Path.cwd()),
              'binary': {'path': str(binary)}, 'arguments': arguments,
              'gtest_filter': filters[0], 'discovery': None,
              'exit_code': None, 'gate_exit_code': RECORD_ERROR, 'xml': None, 'phase': 'prepare',
              'interrupted': False}
    background = args.tier == 'background'
    runtime = None
    record.update(tier=args.tier, backend='headless' if '--headless' in arguments else 'native',
                  isolated_runtime=None,
                  observation=None)
    observer = None
    result = RECORD_ERROR

    def record_interrupt(signum, frame):
        # background 전용 세션도 정리한 뒤 종료 코드와 관찰 결과를 기록한다.
        record['interrupted'] = True

    previous_handler = signal.signal(signal.SIGINT, record_interrupt)
    previous_terminate_handler = signal.signal(signal.SIGTERM, record_interrupt) if background else None
    try:
        record['source'] = source_identity(args.repo_root)
        record['binary'] = binary_identity(binary, parent)
        save_json(directory / 'record.json', record)
        if record['interrupted']:
            return 130
        if background:
            # 긴 로그 경로를 TMPDIR로 쓰면 macOS의 Unix socket 경로 한도를 넘을 수 있다.
            runtime = Path(tempfile.mkdtemp(prefix='yee-bg-', dir='/private/tmp' if sys.platform == 'darwin' else None))
            record['isolated_runtime'] = str(runtime)
        if args.observe_background:
            observer = BackgroundObserver(args.repo_root / 'tools/dev/observe-background.swift',
                                          args.build_root.resolve(), directory)
        if record['interrupted']:
            return 130
        record['phase'] = 'discovery'
        if background:
            code, listing_stdout = run_background_command(
                [str(binary), *arguments, '--gtest_list_tests'], runtime / 'discovery',
                lambda: record['interrupted'], capture=True, observer=observer)
        else:
            listing = subprocess.Popen([str(binary), *arguments, '--gtest_list_tests'],
                                       stdout=subprocess.PIPE, text=True, encoding='utf-8-sig', errors='replace')
            listing_stdout, _ = listing.communicate()
            code = listing.returncode
        record['listing_exit_code'] = shell_exit_code(code)
        (directory / 'listing.txt').write_text(listing_stdout, encoding='utf-8')
        if record['interrupted']:
            record['exit_code'] = record['listing_exit_code']
            return record['exit_code'] or 130
        report = check_test_listing(listing_stdout, filters[0])
        record['discovery'] = report
        print(json.dumps(report, ensure_ascii=False, separators=(',', ':')), flush=True)
        result = record['listing_exit_code'] or (0 if report['ok'] else 1)
        if result:
            record['exit_code'] = result
            print(f'Native 목록 검사 실패 ({result}): {binary}', file=sys.stderr)
        else:
            result = RECORD_ERROR
            record['phase'] = 'execution'
            before = xml_files(location, is_directory, binary)
            record['xml_before'] = before
            save_json(directory / 'record.json', record)
            if record['interrupted']:
                return 130
            if background:
                code, _ = run_background_command([str(binary), *arguments], runtime / 'execution',
                                                  lambda: record['interrupted'], observer=observer)
            else:
                execution = subprocess.Popen([str(binary), *arguments])
                code = execution.wait()
            result = record['exit_code'] = shell_exit_code(code)
            record['xml'] = summarize_xml(location, is_directory, binary, before)
            xml = record['xml']
            if result == 0 and (xml.get('error') or not xml.get('tests')
                                or xml['failures'] + xml['errors'] > 0):
                result = XML_MISMATCH
                print(f'Native 종료 코드 0과 XML 결과 불일치: {json.dumps(xml, ensure_ascii=False)}',
                      file=sys.stderr)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        record['error'] = str(error)
        print(f'Native 검사 기록 실패: {error}', file=sys.stderr)
        if not record['exit_code']:
            result = RECORD_ERROR
    finally:
        if runtime and not any(runtime.iterdir()):
            shutil.rmtree(runtime)
        if observer:
            try:
                record['observation'] = observer.finish()
                for note in record['observation'].get('notes', []):
                    print(f'background observation note: {note}', file=sys.stderr)
                if not record['observation']['passed']:
                    status = 'tests passed' if result == 0 else f'native gate failed ({result})'
                    print(f'{status}; {observation_failure(record["observation"])}', file=sys.stderr)
                if not record['observation']['passed'] and result == 0:
                    result = OBSERVATION_FAILED
            except (OSError, ValueError) as error:
                record['observation'] = {'error': str(error), 'passed': False}
                status = 'tests passed' if result == 0 else f'native gate failed ({result})'
                print(f'{status}; observation incomplete: {error}', file=sys.stderr)
                if result == 0:
                    result = OBSERVATION_FAILED
        if record['interrupted']:
            if record['exit_code'] is None:
                record['exit_code'] = 130
            result = record['exit_code'] or 130
        record['ended_at_utc'] = utc_now()
        record['gate_exit_code'] = result
        try:
            save_json(directory / 'record.json', record)
        except OSError as error:
            print(f'Native 최종 기록 저장 실패: {error}', file=sys.stderr)
            if result == 0:
                result = RECORD_ERROR
        finally:
            signal.signal(signal.SIGINT, previous_handler)
            if background:
                signal.signal(signal.SIGTERM, previous_terminate_handler)
    return result


def observation_failure(summary):
    if not summary.get('observation_complete'):
        reasons = list(summary.get('incomplete_reasons', []))
        return 'observation incomplete: ' + '; '.join(reasons or ['observer coverage unavailable'])
    return 'background interference observed'


def shell_exit_code(returncode):
    return returncode if returncode >= 0 else 128 - returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repo-root', 'build-root', 'out-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--out-name', required=True)
    parser.add_argument('--root-explicit', choices=('true', 'false'), required=True)
    parser.add_argument('--out-explicit', choices=('true', 'false'), required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--tier', choices=('background', 'foreground', 'all'), default='foreground')
    parser.add_argument('--observe-background', action='store_true')
    parser.add_argument('binary', type=Path)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.observe_background and args.tier != 'background':
        parser.error('--observe-background는 --tier=background에서만 사용할 수 있다.')
    try:
        return run_gate(args)
    except (OSError, ValueError) as error:
        parser.exit(RECORD_ERROR, f'Native 검사 준비·기록 실패: {error}\n')


if __name__ == '__main__':
    sys.exit(main())
