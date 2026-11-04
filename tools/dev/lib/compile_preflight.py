#!/usr/bin/env python3
"""Yee가 수정한 컴파일 단위를 기존 Chromium graph에서 분류하고 실패 파일을 보고한다."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / 'tools/overlay'))
from lib.overlay_tools import load_catalog, owned_source_files, patch_paths


def stale_graph_inputs(out_dir):
    """GN depfile의 입력을 재생성 stamp와 비교하며 graph를 갱신하지 않는다."""
    depfile = out_dir / 'build.ninja.d'
    if not depfile.is_file():
        return ['build.ninja.d 누락 (graph 최신 여부 확인 불가)']
    stamp = out_dir / 'build.ninja.stamp'
    if not stamp.is_file():
        stamp = out_dir / 'build.ninja'
    generated_at = stamp.stat().st_mtime_ns
    contents = depfile.read_text(encoding='utf-8').replace('\\\n', '')
    _, separator, inputs = contents.partition(':')
    if not separator or not inputs.strip():
        return ['build.ninja.d의 의존 파일 목록이 비어 있거나 잘못되었다.']
    stale = []
    # depfile의 따옴표는 경로 문자다. 셸 해석 대신 공백·역슬래시 이스케이프만 푼다.
    for token in re.findall(r'(?:\\.|[^\s\\])+', inputs):
        name = re.sub(r'\\(.)', r'\1', token).replace('$$', '$')
        dependency = out_dir / name
        try:
            if dependency.stat().st_mtime_ns > generated_at:
                stale.append(name)
        except FileNotFoundError:
            stale.append(name + ' (누락)')
    return stale


def check_graph(out_dir, allow_stale=False):
    """오래된 graph는 기록 폴더 생성 전에 막고 목록 조회만 경고 후 허용한다."""
    stale = stale_graph_inputs(out_dir)
    if not stale:
        return
    print('경고: 오래된 빌드 graph이거나 최신 여부를 확인할 수 없다:', file=sys.stderr)
    print('\n'.join('  ' + name for name in stale), file=sys.stderr)
    configure = '.\\tools\\dev\\configure.ps1' if sys.platform == 'win32' else './tools/dev/configure.sh'
    print(f'빌드 설정을 먼저 갱신하라: {configure} (같은 루트·출력 환경 유지)',
          file=sys.stderr)
    if not allow_stale:
        raise SystemExit(19)
    print('목록은 기존 graph 기준이며 플랫폼 미컴파일 분류를 확정할 수 없다.', file=sys.stderr)


def ninja_path(src):
    """현재 호스트에서 실행할 Chromium Ninja를 선택한다."""
    return src / 'third_party/ninja' / ('ninja.exe' if sys.platform == 'win32' else 'ninja')


def query_graph(ninja, out_dir, paths):
    """manifest를 재생성하지 않는 Ninja subtool로 노드의 입출력을 조회한다."""
    if not paths:
        return {}
    result = subprocess.run([str(ninja), '-C', str(out_dir), '-t', 'query', *paths],
                            capture_output=True, text=True, check=True)
    nodes = {}
    node = section = None
    for line in result.stdout.splitlines():
        if not line.startswith(' ') and line.endswith(':'):
            node = nodes.setdefault(line[:-1], {'inputs': [], 'outputs': []})
            section = None
        elif line.startswith('  input:'):
            section = 'inputs'
        elif line == '  outputs:':
            section = 'outputs'
        elif line.startswith('    ') and node is not None and section:
            node[section].append(line[4:].removeprefix('|| ').removeprefix('| '))
        else:
            section = None
    if set(nodes) != set(paths):
        raise ValueError('Ninja 조회 결과에 요청한 노드가 누락되었다.')
    return nodes


def graph_sources(ninja, out_dir, candidates):
    # 전체 graph의 소스 목록은 크므로 후보 경로만 보존한다. 조회는 빌드를 실행하지 않는다.
    found = set()
    with tempfile.TemporaryFile(mode='w+') as errors:
        with subprocess.Popen([str(ninja), '-C', str(out_dir), '-t', 'targets', 'rule'],
                              stdout=subprocess.PIPE, stderr=errors, text=True) as process:
            for line in process.stdout:
                path = line.rstrip('\n')
                if path in candidates:
                    found.add(path)
            result = process.wait()
        if result:
            errors.seek(0)
            raise RuntimeError(f'Ninja 소스 목록 조회 실패 ({result}): {errors.read()}')
    return found


def compile_plan(src, out_dir, include_unbuilt=False):
    if not (out_dir / 'build.ninja').is_file():
        raise ValueError(f'기존 build.ninja가 필요하다: {out_dir}')
    catalog = load_catalog(REPO_ROOT)
    shell_patch = next(p for p in catalog['patches'] if p['role'] == 'shell')
    patch = sorted(path for path in patch_paths(REPO_ROOT / shell_patch['file'],
                                               include_deleted=False)
                   if Path(path).suffix in ('.cc', '.mm'))
    owned = sorted(destination.relative_to(src).as_posix()
                   for source, destination in owned_source_files(src, catalog, REPO_ROOT)
                   if source.suffix in ('.cc', '.mm'))
    sources = sorted(set(patch) | set(owned))
    if not sources:
        raise ValueError('패치와 owned 소스에서 컴파일 단위를 찾지 못했다.')
    ninja = ninja_path(src)
    candidates = {'../../' + path for path in sources}
    present = graph_sources(ninja, out_dir, candidates)
    nodes = query_graph(ninja, out_dir, sorted(present))
    compiled, unbuilt, uncompiled, targets = {}, {}, [], []
    for path in sources:
        source = '../../' + path
        outputs = nodes.get(source, {}).get('outputs', [])
        objects = list(dict.fromkeys(p for p in outputs if Path(p).suffix in ('.o', '.obj')))
        if not objects:
            uncompiled.append(path)
            continue
        missing = [obj for obj in objects if not (out_dir / obj).is_file()]
        if missing:
            unbuilt[path] = missing
        selected = objects if include_unbuilt else [obj for obj in objects if obj not in missing]
        if not selected:
            continue
        compiled[path] = selected
        # Windows cmd는 ^를 소비하므로 object 경로를 직접 전달한다.
        use_source_target = sys.platform != 'win32' and outputs == objects and len(objects) == 1
        targets.extend([source + '^'] if use_source_target else selected)
    return {'chromium_src': str(src), 'out_dir': str(out_dir),
            'patch_sources': patch, 'owned_sources': owned, 'compiled': compiled,
            'include_unbuilt': include_unbuilt, 'unbuilt_objects': unbuilt,
            'uncompiled_sources': uncompiled, 'targets': list(dict.fromkeys(targets))}


def print_plan(plan):
    print(f"0001 .cc/.mm (삭제 제외): {len(plan['patch_sources'])}")
    print(f"owned .cc/.mm (목적지): {len(plan['owned_sources'])}")
    print(f"실제 컴파일 대상: {len(plan['compiled'])}개 파일 / "
          f"{sum(len(objects) for objects in plan['compiled'].values())}개 object")
    print('\n컴파일 대상 (Chromium 경로 → object):')
    for source, objects in plan['compiled'].items():
        print(f"  {source} → {', '.join(objects)}")
    unbuilt_count = sum(len(objects) for objects in plan['unbuilt_objects'].values())
    print(f"\n현재 출력에서 미빌드: {len(plan['unbuilt_objects'])}개 파일 / {unbuilt_count}개 object")
    for source, objects in plan['unbuilt_objects'].items():
        print(f"  {source} → {', '.join(objects)}")
    if unbuilt_count:
        if plan['include_unbuilt']:
            print(f'{unbuilt_count}개 미빌드 object를 --include-unbuilt로 컴파일 대상에 포함한다.')
        else:
            print(f'{unbuilt_count}개 미빌드로 생략 — 완전한 통과 아님')
    print(f"\n이 플랫폼에서 미컴파일: {len(plan['uncompiled_sources'])}개 파일")
    for source in plan['uncompiled_sources']:
        print('  ' + source)
    if plan['uncompiled_sources']:
        print('다른 플랫폼·빌드 구성에서 추가 점검이 필요하다. 현재 graph에 object가 없는 파일은 통과로 간주하지 않는다.')


def report_failures(directory):
    plan = json.loads((directory / 'plan.json').read_text(encoding='utf-8'))
    log = (directory / 'build.log').read_text(encoding='utf-8', errors='replace')
    failed_outputs = set()
    for line in re.sub(r'\x1b\[[0-9;]*m', '', log).splitlines():
        if line.startswith('FAILED:'):
            # Ninja의 출력 경로와 Siso의 UUID·출력 경로·설명을 함께 지원한다.
            description = line.removeprefix('FAILED:')
            try:
                words = shlex.split(description)
            except ValueError:
                words = description.split()
            for word in words:
                word = word.removeprefix('./')
                if Path(word).suffix in ('.o', '.obj'):
                    failed_outputs.add(word)
    failed_sources = {source for source, objects in plan['compiled'].items()
                      if failed_outputs.intersection(objects)}
    known = {obj for objects in plan['compiled'].values() for obj in objects}
    dependencies = sorted(failed_outputs - known)
    if dependencies:
        src = Path(plan['chromium_src'])
        try:
            nodes = query_graph(ninja_path(src), Path(plan['out_dir']), dependencies)
            for node in nodes.values():
                failed_sources.update(path.removeprefix('../../') for path in node['inputs']
                                      if Path(path).suffix in ('.cc', '.mm'))
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            print(f'실패 의존 파일의 소스 조회 실패: {error}', file=sys.stderr)
            failed_sources.update(f'소스 조회 불가: {path}' for path in dependencies)
    lines = sorted(failed_sources)
    if not lines:
        lines = ['실패 소스를 식별하지 못했다. build.log의 도구·생성 단계 오류를 확인하라.']
    report = '\n'.join(lines) + '\n'
    (directory / 'failed-sources.txt').write_text(report, encoding='utf-8')
    (directory / 'failed-objects.txt').write_text('\n'.join(sorted(failed_outputs)) + '\n',
                                                 encoding='utf-8')
    print('실패 파일 목록:\n' + report, file=sys.stderr, end='')


def report_success(directory):
    """빌드 전 계획에서 점검 범위를 다시 읽어 성공 로그 끝에도 생략을 남긴다."""
    plan = json.loads((directory / 'plan.json').read_text(encoding='utf-8'))
    unbuilt_count = sum(map(len, plan['unbuilt_objects'].values()))
    disposition = '컴파일 대상에 포함' if plan['include_unbuilt'] else '생략'
    print(f"현재 출력에서 미빌드: {len(plan['unbuilt_objects'])}개 파일 / "
          f'{unbuilt_count}개 object ({disposition})')
    print(f"이 플랫폼에서 미컴파일: {len(plan['uncompiled_sources'])}개 파일")
    if (unbuilt_count and not plan['include_unbuilt']) or plan['uncompiled_sources']:
        print('범위 판정: 완전한 통과 아님. 미빌드 생략·플랫폼 미컴파일 목록을 확인하라.')
    else:
        print('범위 판정: 선택한 컴파일 단위 전체 완료. 전체 앱 검증은 별도다.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    graph_parser = commands.add_parser('check-graph', help='기록 폴더 생성 전 graph 최신 여부 검사')
    graph_parser.add_argument('out_dir', type=Path)
    plan_parser = commands.add_parser('plan', help='기존 graph에서 컴파일 대상과 미컴파일 파일 분류')
    plan_parser.add_argument('chromium_src', type=Path)
    plan_parser.add_argument('out_dir', type=Path)
    plan_parser.add_argument('--list', action='store_true', help='오래된 graph도 경고 후 목록만 출력')
    plan_parser.add_argument('--include-unbuilt', action='store_true',
                             help='물리 출력이 없는 object도 컴파일 대상에 포함')
    plan_parser.add_argument('--save-to', type=Path, help='컴파일에 사용할 계획과 대상 목록을 보존할 폴더')
    failure_parser = commands.add_parser('failures', help='빌드 로그의 실패 object를 소스 파일로 보고')
    failure_parser.add_argument('directory', type=Path)
    success_parser = commands.add_parser('summary', help='보존한 계획에서 완료 범위와 생략 수를 보고')
    success_parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'check-graph':
            check_graph(args.out_dir)
        elif args.command == 'failures':
            report_failures(args.directory)
        elif args.command == 'summary':
            report_success(args.directory)
        else:
            check_graph(args.out_dir, allow_stale=args.list)
            plan = compile_plan(args.chromium_src, args.out_dir, args.include_unbuilt)
            print_plan(plan)
            if args.save_to:
                (args.save_to / 'plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2)
                                                       + '\n', encoding='utf-8')
                (args.save_to / 'targets.txt').write_text(''.join(t + '\n' for t in plan['targets']),
                                                        encoding='utf-8')
            if not plan['targets']:
                if not args.include_unbuilt and plan['unbuilt_objects']:
                    parser.exit(16, '출력이 비어 있다 — 이식 점검이면 --include-unbuilt로 실행\n')
                parser.exit(16, '이 graph에서 컴파일할 대상이 없다. 미컴파일 목록을 다른 플랫폼·구성에서 점검하라.\n')
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(3, f'사전 컴파일 조회 실패: {detail}\n')


if __name__ == '__main__':
    main()
