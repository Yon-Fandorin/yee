"""Windows 빌드를 overlay·Chromium·활성 Git 의존 소스와 연결한다."""

import ast
import hashlib
from pathlib import Path, PurePosixPath
import re
import subprocess

def git(root, *arguments):
    return subprocess.check_output([
        'git', '--no-optional-locks', '-c', 'safe.directory=' + root.as_posix(),
        '-C', str(root), *arguments])


def file_sha256(path):
    digest = hashlib.sha256()
    before = path.stat()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f'지문을 읽는 동안 파일이 변경되었다: {path}')
    return digest.hexdigest()


def relative_path(value):
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or path.as_posix() != value or
            any(part in ('.', '..') for part in path.parts) or
            '\\' in value or ':' in value):
        raise ValueError(f'정규화된 상대 경로가 아니다: {value}')
    return path


def exact_child(root, relative):
    path = root.joinpath(*relative_path(relative).parts)
    resolved = path.resolve()
    if not resolved.is_relative_to(root) or resolved != path.absolute():
        raise ValueError(f'다른 경로로 연결된 checkout은 허용하지 않는다: {path}')
    return resolved


def fingerprint(root):
    root = root.resolve()
    top = Path(git(root, 'rev-parse', '--show-toplevel').decode().strip()).resolve()
    if top != root:
        raise ValueError(f'정확한 Git checkout 루트가 아니다: {root}; {top}')
    head = git(root, 'rev-parse', 'HEAD').decode().strip()
    index = git(root, 'ls-files', '--stage', '-z')
    gitlinks = {}
    for entry in index.split(b'\0'):
        if not entry:
            continue
        metadata, encoded = entry.split(b'\t', 1)
        mode, revision, stage = metadata.split()
        if stage != b'0':
            raise ValueError(f'해결되지 않은 인덱스 충돌이 있다: {root}')
        if mode == b'160000':
            relative = encoded.decode('utf-8')
            relative_path(relative)
            gitlinks[relative] = revision.decode()
    options = ('--no-ext-diff', '--no-textconv', '--no-color', '--no-renames',
               '--full-index', '--binary', '--ignore-submodules=all')
    staged = git(root, 'diff', *options, '--cached', 'HEAD')
    worktree = git(root, 'diff', *options)
    untracked = sorted(set(git(root, 'ls-files', '--others', '--exclude-standard', '-z').split(b'\0')) - {b''})
    digest = hashlib.sha256()
    for encoded in untracked:
        path = exact_child(root, encoded.decode('utf-8'))
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'미추적 소스는 일반 파일이어야 한다: {path}')
        digest.update(encoded + b'\0' + bytes.fromhex(file_sha256(path)))
    return {'head': head, 'index_sha256': hashlib.sha256(index).hexdigest(),
            'staged_sha256': hashlib.sha256(staged).hexdigest(),
            'worktree_sha256': hashlib.sha256(worktree).hexdigest(),
            'untracked_sha256': digest.hexdigest(), 'untracked_files': len(untracked),
            'gitlinks': gitlinks}


def git_dependencies(native):
    filename = native.parent / '.gclient_entries'
    tree = ast.parse(filename.read_text(encoding='utf-8'))
    assignments = [node for node in tree.body if isinstance(node, ast.Assign)
                   and any(isinstance(name, ast.Name) and name.id == 'entries'
                           for name in node.targets)]
    if len(assignments) != 1:
        raise ValueError('.gclient_entries에는 entries 리터럴 하나가 필요하다.')
    entries = ast.literal_eval(assignments[0].value)
    if not isinstance(entries, dict):
        raise ValueError('.gclient_entries의 entries가 사전이 아니다.')
    dependencies = {}
    for relative, url in entries.items():
        if not isinstance(relative, str):
            raise ValueError('의존 소스 경로는 문자열이어야 한다.')
        if relative == 'src' or not isinstance(url, str) or not url.startswith('https://'):
            continue
        if url.startswith('https://chrome-infra-packages.appspot.com/'):
            continue  # 패키지 식별자는 설정 해시에 포함하되 Git으로 탐색하지 않는다.
        upstream, separator, revision = url.rpartition('@')
        if not separator or not re.fullmatch('[0-9a-f]{40}', revision):
            raise ValueError(f'활성 Git 의존 소스에는 고정 revision이 필요하다: {relative}')
        parts = relative_path(relative).parts
        if len(parts) < 2 or parts[0] != 'src':
            raise ValueError(f'활성 Git 의존 소스가 src 밖에 있다: {relative}')
        local = PurePosixPath(*parts[1:]).as_posix()
        root = exact_child(native, local)
        if not (root / '.git').exists():
            raise ValueError(f'활성 Git 의존 checkout이 없다: {relative}')
        dependencies[local] = {'revision': revision, 'url': upstream}
    return dependencies


def build_inputs(repo, native, output):
    repo, native, output = repo.resolve(), native.resolve(), output.resolve()
    expected = git_dependencies(native)
    native_state = fingerprint(native)
    pending = {relative: exact_child(native, relative) for relative in expected}
    seen = {native}
    dependencies = {}

    def add_populated_gitlinks(root, state):
        for relative in state['gitlinks']:
            child = exact_child(root, relative)
            if (child / '.git').exists() and child not in seen:
                pending[child.relative_to(native).as_posix()] = child

    add_populated_gitlinks(native, native_state)
    while pending:
        relative = min(pending)
        root = pending.pop(relative)
        if root in seen:
            continue
        seen.add(root)
        state = fingerprint(root)
        entry = expected.get(relative)
        if entry and state['head'] != entry['revision']:
            raise ValueError(f'의존 소스 HEAD가 gclient 기준과 다르다: {relative}')
        dependencies[relative] = {'expected': entry, **state}
        add_populated_gitlinks(root, state)
    return {'schema': 'yee.windows-native-source.v1', 'repository': fingerprint(repo), 'native': native_state,
            'dependencies': dependencies,
            'dependency_config_sha256': {
                name: file_sha256(native.parent / name)
                for name in ('.gclient', '.gclient_entries')},
            'args_sha256': file_sha256(output / 'args.gn'),
            'paths': {'repository': str(repo), 'chromium': str(native), 'output': str(output)}}
