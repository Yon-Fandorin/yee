"""Chromium 연결 패치의 결합 지표가 기준선보다 늘지 않게 막는다. 빌드는 실행하지 않는다."""

from collections import Counter
from dataclasses import dataclass, field
import json
from pathlib import Path, PurePosixPath
import re

from .overlay_tools import load_catalog, patch_paths, relative_path


RATCHET_FILE = 'build/patch-ratchet.json'
SCHEMA_VERSION = 1

# 검사하는 지표와 보고용 설명. 순서는 보고 순서다.
METRICS = {
    'upstream_test_lines': 'Chromium 테스트 파일에 추가한 줄',
    'shell_branches': 'IsShellEnabled 분기',
    'view_id_lookups': 'GetViewByID 조회',
    'yee_includes': 'Yee 소유 헤더 include',
    'yee_members': 'Chromium 헤더의 Yee 상태 멤버',
}
FILE_KINDS = {'prod': '제품 수정', 'test': '테스트 수정', 'new': '새 파일'}

TEST_DIRECTORIES = {'test', 'tests', 'testing', 'test_support'}
TEST_STEM = re.compile(r'(?:^|_)(?:unit|browser|ui|api)?tests?(?:_|$)')
INCLUDE = re.compile(r'^\s*#\s*(?:include|import)\s+"([^"]+)"')
# 이름 뒤의 배열 크기, 상수식 비트 필드 폭과 초기화 값을 허용한다.
MEMBER = re.compile(r'\b(\w+)\s*(?:\[[^\]]*\]\s*)*(?::\s*(?:[^:;={}]|::)+?\s*)?'
                    r'(=(?!=)[^;]*|\{[^;]*\})?;$')
STATEMENT_LIMIT = 4
TEMPLATE_ARGUMENTS = re.compile(r'<[^<>]*>')
ALIGNMENT = re.compile(r'\balignas\s*\((?:[^()]|\([^()]*\))*\)')
# `GUARDED_BY(GetLock())`처럼 이름 뒤에 이어 붙는 대문자 주석 매크로. 여러 개를 연달아 쓸 수 있다.
TRAILING_ANNOTATION = re.compile(
    r'\s+[A-Z][A-Z0-9_]*\((?:[^()]|\([^()]*\))*\)(?=\s*(?:=|\{|;|[A-Z][A-Z0-9_]*\())')
# `observation_{`처럼 이름에 붙은 중괄호는 블록이 아니라 다음 줄로 이어지는 초기화다.
BRACE_INITIALIZER = re.compile(r'\w\{$')
# `*`·`/*`·`*/`로 시작하는 블록 주석 줄. `*this,` 같은 역참조 식은 주석이 아니다.
BLOCK_COMMENT = re.compile(r'^(?:/\*|\*/|\*(?:\s|$))')
MACRO_LINE = re.compile(r'^[A-Z][A-Z0-9_]*\(.*\)$')
NOT_MEMBER_PREFIXES = ('return ', 'class ', 'struct ', 'enum ', 'union ',
                       'using ', 'typedef ', 'friend ', 'namespace ', 'template', 'case ',
                       'delete ', 'throw ', 'extern ', '@')
# 함수 선언 끝의 지정자와 `operator=`는 멤버 이름이 아니다. `kName`은 Chromium의 상수 이름이다.
NOT_MEMBER_NAMES = {'override', 'final', 'const', 'default', 'delete', 'noexcept', 'operator'}
CONSTANT_NAME = re.compile(r'^k[A-Z0-9]')
OPERATOR = re.compile(r'\boperator\b')
AUTO = re.compile(r'\bauto\b')
# `template` 줄 아래 `requires ...` `using X = ...`처럼 이어진 제약 별칭도 별칭이다.
ALIAS = re.compile(r'\b(?:using|typedef|requires)\b')
RAW_STRING_PREFIX = re.compile(r'(?:^|[^\w])(?:u8|u|U|L)?R$')
NOT_TYPE_SUFFIXES = ('->', '.', '=', '(', ',', '::', '?', ':', '&&', '||', '+', '-', '<<')


def file_kind(path, created):
    """패치가 만든 파일, Chromium 테스트 파일과 제품 파일을 구분한다."""
    if created:
        return 'new'
    name = PurePosixPath(path)
    if TEST_DIRECTORIES.intersection(name.parts[:-1]) or TEST_STEM.search(name.stem):
        return 'test'
    return 'prod'


def statements(lines):
    """연속 추가 줄을 `;`·블록·접근 지정자 경계까지 이어 여러 줄 선언을 하나로 만든다."""
    buffer, skipping = [], False
    for line in lines:
        code = line.split('//', 1)[0].strip()
        # 주석·전처리 줄과 `METADATA_HEADER(...)`처럼 끝맺음 없는 매크로 줄은 선언을 끊는다.
        if not code or BLOCK_COMMENT.match(code) or code.startswith('#') or MACRO_LINE.match(code):
            buffer = [] if not skipping else buffer
            continue
        continues = BRACE_INITIALIZER.search(code) and not code.startswith(NOT_MEMBER_PREFIXES)
        ends = code.endswith((';', '{', '}', ':')) and not continues
        if skipping:
            skipping = not ends
            continue
        buffer.append(code)
        if ends:
            yield ' '.join(buffer)
            buffer = []
        elif len(buffer) == STATEMENT_LIMIT:
            # 긴 문장은 판정하지 않고 끝까지 건너뛴다. 중간부터 읽으면 매개변수를 멤버로 오인한다.
            buffer, skipping = [], True


def closing_quote(text, start, quote):
    """`start` 다음부터 escape를 건너뛰며 닫는 따옴표의 위치를 찾는다. 없으면 끝을 반환한다."""
    index = start + 1
    while index < len(text) and text[index] != quote:
        index += 2 if text[index] == '\\' else 1
    return min(index, len(text) - 1)


def blank_literals(statement):
    """주석·원시 문자열·문자열·문자 리터럴을 왼쪽부터 한 번에 비운다. 숫자 구분자는 유지한다.

    리터럴과 주석 속 키워드·괄호·따옴표가 선언 구조 판정에 섞이지 않게 한다.
    """
    output, index = [], 0
    while index < len(statement):
        char = statement[index]
        if statement.startswith('/*', index):
            end = statement.find('*/', index + 2)
            index = len(statement) if end < 0 else end + 2
            output.append(' ')
            continue
        if char == '"' and RAW_STRING_PREFIX.search(''.join(output[-3:])):
            # R"delimiter( ... )delimiter" 안의 따옴표는 문자열을 닫지 않는다.
            opening = statement.find('(', index)
            delimiter = statement[index + 1:opening] if opening >= 0 else ''
            end = statement.find(')' + delimiter + '"', max(opening, index))
            index = len(statement) if opening < 0 or end < 0 else end + len(delimiter) + 2
            output.append('""')
            continue
        if char == "'":
            start = index
            while start > 0 and (statement[start - 1].isalnum() or statement[start - 1] in "'_."):
                start -= 1
            # `1'000`·`.000'001`처럼 숫자 토큰 안의 작은따옴표는 문자 리터럴이 아니라 자릿수 구분자다.
            token = statement[start:index].lstrip('.')
            if token and token[0].isdigit() and start < index:
                output.append(char)
                index += 1
                continue
        if char in '"\'':
            index = closing_quote(statement, index, char) + 1
            output.append('""')
            continue
        output.append(char)
        index += 1
    return ''.join(output)


def member_declaration(statement):
    """선언문 하나가 멤버 선언이면 (이름, 선언 타입)을, 아니면 None을 근사 판정해 반환한다."""
    statement = blank_literals(statement)
    while (stripped := TRAILING_ANNOTATION.sub('', statement)) != statement:
        statement = stripped
    match = MEMBER.search(statement)
    # 전방 선언·별칭·상수·연산자와 문장은 상태가 아니므로 제외한다.
    if (not match or statement.startswith(NOT_MEMBER_PREFIXES) or 'constexpr' in statement
            or OPERATOR.search(statement) or ALIAS.search(statement)
            or match.group(1) in NOT_MEMBER_NAMES
            or CONSTANT_NAME.match(match.group(1))):
        return None
    # 괄호 짝이 맞지 않으면 함수 선언의 마지막 매개변수처럼 잘린 조각이다.
    if statement.count('(') != statement.count(')'):
        return None
    declared_type = statement[:match.start()].strip()
    # `Callback<void()>`처럼 템플릿 인자 안의 괄호는 함수 선언으로 보지 않는다.
    outer_type = ALIGNMENT.sub('', declared_type)
    while (inner := TEMPLATE_ARGUMENTS.sub('', outer_type)) != outer_type:
        outer_type = inner
    # 타입이 없는 문장은 대입문, `auto` 선언은 인라인 함수의 지역 변수로 본다.
    if (not declared_type or declared_type.endswith(NOT_TYPE_SUFFIXES)
            or '(' in outer_type or AUTO.search(outer_type)):
        return None
    return match.group(1), declared_type


def is_yee_member(statement):
    """멤버 선언의 이름이나 선언 타입이 Yee를 가리키는지 본다. 초기화 값은 보지 않는다."""
    declaration = member_declaration(statement)
    if not declaration:
        return False
    name, declared_type = declaration
    return 'yee' in name.lower() or 'yee::' in declared_type or 'Yee' in declared_type


@dataclass
class FileDiff:
    path: str
    kind: str
    # 수정 위치마다 이어진 추가 줄 묶음. 여러 줄 선언은 한 묶음 안에서만 잇는다.
    added_runs: list = field(default_factory=list)
    removed: int = 0
    edit_sites: int = 0
    hunks: int = 0

    @property
    def added(self):
        return [line for run in self.added_runs for line in run]


def file_diffs(patch_file):
    """검증한 패치를 파일별 추가 줄·삭제 수·연속 수정 위치로 나눈다."""
    patch_paths(patch_file)
    text = Path(patch_file).read_text(encoding='utf-8')
    diffs = []
    for section in re.split(r'(?m)(?=^diff --git )', text)[1:]:
        lines = section.split('\n')
        path = re.fullmatch(r'diff --git a/(\S+) b/\S+', lines[0])[1]
        created = in_hunk = in_edit = False
        runs, removed, edit_sites, hunks = [], 0, 0, 0
        for line in lines[1:]:
            if line.startswith('@@'):
                in_hunk, in_edit = True, False
                hunks += 1
            elif not in_hunk:
                created = created or line == '--- /dev/null' or line.startswith('new file mode ')
            elif line.startswith(('+', '-')):
                # 문맥 줄 없이 이어진 추가·삭제는 하나의 수정 위치로 센다.
                if not in_edit:
                    edit_sites += 1
                    runs.append([])
                in_edit = True
                if line.startswith('+'):
                    runs[-1].append(line[1:])
                else:
                    removed += 1
            elif not line.startswith('\\'):
                in_edit = False
        runs = [run for run in runs if run]
        diffs.append(FileDiff(path, file_kind(path, created), runs, removed, edit_sites, hunks))
    return diffs


def owned_include_matcher(catalog):
    """overlay가 소유한 소스 경로와 새 연결 헤더를 가리키는 include인지 판정한다."""
    prefixes = tuple(binding['destination'] + '/' for binding in catalog['source_roots'])
    glue_headers = {path for path in catalog['new_glue'] if path.endswith('.h')}
    return lambda target: target.startswith(prefixes) or target in glue_headers


@dataclass
class Measurement:
    counts: dict
    sizes: dict


def measure(patch_file, catalog):
    """패치가 수정한 Chromium 파일별 지표와 보고용 크기를 센다. 새 파일은 지표에서 제외한다."""
    owned = owned_include_matcher(catalog)
    counts = {metric: Counter() for metric in METRICS}
    sizes = {kind: Counter() for kind in FILE_KINDS}
    for diff in file_diffs(patch_file):
        size = sizes[diff.kind]
        size.update(files=1, edit_sites=diff.edit_sites, added=len(diff.added),
                    removed=diff.removed, hunks=diff.hunks)
        if diff.kind == 'new':
            continue
        found = Counter()
        if diff.kind == 'test':
            found['upstream_test_lines'] = len(diff.added)
        for line in diff.added:
            include = INCLUDE.match(line)
            found.update(shell_branches=int('IsShellEnabled(' in line),
                         view_id_lookups=int('GetViewByID(' in line),
                         yee_includes=int(bool(include and owned(include[1]))))
        if diff.path.endswith('.h'):
            found['yee_members'] = sum(is_yee_member(statement)
                                       for run in diff.added_runs
                                       for statement in statements(run))
        for metric, count in found.items():
            if count:
                counts[metric][diff.path] = count
    return Measurement(counts, sizes)


def validate_ratchet(data):
    """기준선은 지표별 경로·양수 개수만, 예외는 지표·경로·허용 수·이유를 갖는지 검사한다."""
    if (not isinstance(data, dict) or set(data) != {'schema_version', 'roles'}
            or type(data['schema_version']) is not int
            or data['schema_version'] != SCHEMA_VERSION or not isinstance(data['roles'], dict)):
        raise ValueError(f'Unsupported {RATCHET_FILE} schema')
    for role, config in data['roles'].items():
        if not isinstance(config, dict) or set(config) != {'baseline', 'exceptions'}:
            raise ValueError(f'Expected baseline and exceptions for role {role}')
        baseline = config['baseline']
        if not isinstance(baseline, dict) or set(baseline) != set(METRICS):
            raise ValueError(f'Expected every ratchet metric in the {role} baseline')
        for metric, paths in baseline.items():
            if not isinstance(paths, dict):
                raise ValueError(f'Expected path counts for {role}.{metric}')
            for path, count in paths.items():
                relative_path(path)
                if type(count) is not int or count <= 0:
                    raise ValueError(f'Expected a positive count for {role}.{metric} {path}')
        exceptions = config['exceptions']
        if not isinstance(exceptions, list):
            raise ValueError(f'Expected an exception list for role {role}')
        seen = set()
        for entry in exceptions:
            if (not isinstance(entry, dict) or set(entry) != {'metric', 'path', 'allowed', 'reason'}
                    or not isinstance(entry['metric'], str) or entry['metric'] not in METRICS
                    or type(entry['allowed']) is not int or entry['allowed'] <= 0
                    or not isinstance(entry['reason'], str) or not entry['reason'].strip()):
                raise ValueError(f'Expected metric, path, allowed and reason in {role} exceptions')
            key = (entry['metric'], relative_path(entry['path']))
            if key in seen:
                raise ValueError(f'Duplicate {role} exception for {key[0]} {key[1]}')
            seen.add(key)
    return data


def unique_object(pairs):
    """같은 JSON 객체 안의 중복 키가 앞의 기준선·예외 값을 덮어쓰지 못하게 거부한다."""
    keys = [key for key, _ in pairs]
    duplicates = sorted({key for key in keys if keys.count(key) > 1})
    if duplicates:
        raise ValueError(f'Duplicate keys in {RATCHET_FILE}: {", ".join(duplicates)}')
    return dict(pairs)


def load_ratchet(path):
    text = Path(path).read_text(encoding='utf-8')
    return validate_ratchet(json.loads(text, object_pairs_hook=unique_object))


def write_ratchet(path, data):
    validate_ratchet(data)
    Path(path).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


@dataclass(frozen=True)
class Finding:
    kind: str
    metric: str
    path: str
    current: int
    limit: int


def evaluate(measurement, config):
    """증가, 낮추지 않은 기준선과 쓰지 않는 예외 허용량을 모두 위반으로 보고한다."""
    exceptions = {(entry['metric'], entry['path']): entry['allowed']
                  for entry in config['exceptions']}
    findings = []
    for metric in METRICS:
        current = measurement.counts[metric]
        baseline = config['baseline'][metric]
        excepted = {path for name, path in exceptions if name == metric}
        for path in sorted(set(current) | set(baseline) | excepted):
            now, base = current.get(path, 0), baseline.get(path, 0)
            allowed = exceptions.get((metric, path))
            limit = max(base, allowed or 0)
            if now > limit:
                findings.append(Finding('growth', metric, path, now, limit))
                continue
            if now < base:
                findings.append(Finding('stale_baseline', metric, path, now, base))
            if allowed is not None and (allowed <= base or now < allowed):
                findings.append(Finding('stale_exception', metric, path, now, allowed))
    return findings


def initial_baseline(measurement):
    return {metric: dict(sorted(measurement.counts[metric].items())) for metric in METRICS}


def lowered_baseline(measurement, baseline):
    """줄어든 값만 기준선에 반영한다. 늘어난 값은 예외로만 허용하므로 올리지 않는다."""
    lowered = {}
    for metric in METRICS:
        current = measurement.counts[metric]
        lowered[metric] = {path: min(count, current.get(path, 0))
                           for path, count in sorted(baseline[metric].items())
                           if current.get(path, 0) > 0}
    return lowered


FINDING_LABELS = {
    'growth': ('증가', '허용'),
    'stale_baseline': ('기준선 낮춤', '기준선'),
    'stale_exception': ('예외 정리', '예외 허용'),
}
FINDING_HINTS = {
    'growth': '늘어난 연결을 줄이거나, 검토한 경로·이유를 예외에 추가한다.',
    'stale_baseline': '줄어든 값을 잠그도록 `check-patch-ratchet.py --update`를 실행한다.',
    'stale_exception': '예외의 allowed를 현재 값으로 줄이거나, 기준선 이하면 예외를 지운다.',
}


def report(role, patch, measurement, config, findings):
    """지표 합계와 보고용 크기, 위반 목록을 사람이 읽는 문장으로 만든다."""
    exceptions = {(entry['metric'], entry['path']): entry['allowed']
                  for entry in config['exceptions']}
    lines = [f'패치 래칫: {role} ({patch})', f'  {"지표":<44}{"현재":>8}{"허용":>8}']
    for metric, label in METRICS.items():
        baseline = config['baseline'][metric]
        paths = set(baseline) | {path for name, path in exceptions if name == metric}
        limit = sum(max(baseline.get(path, 0), exceptions.get((metric, path), 0))
                    for path in paths)
        current = sum(measurement.counts[metric].values())
        lines.append(f'  {metric + " (" + label + ")":<44}{current:>8}{limit:>8}')
    lines.append('  크기 (보고용, 검사하지 않음)')
    for kind, label in FILE_KINDS.items():
        size = measurement.sizes[kind]
        lines.append(f'    {label}: 파일 {size["files"]} · 수정 위치 {size["edit_sites"]}'
                     f' · +{size["added"]}/-{size["removed"]} · hunk {size["hunks"]}')
    if not findings:
        lines.append('결과: 통과')
        return '\n'.join(lines)
    lines.append(f'결과: 실패 {len(findings)}건')
    for finding in findings:
        label, limit_label = FINDING_LABELS[finding.kind]
        lines.append(f'  [{label}] {finding.metric} {finding.path}: '
                     f'현재 {finding.current}, {limit_label} {finding.limit}')
    for kind in dict.fromkeys(finding.kind for finding in findings):
        lines.append(f'  - {FINDING_LABELS[kind][0]}: {FINDING_HINTS[kind]}')
    return '\n'.join(lines)


def role_patch(catalog, role):
    for patch in catalog['patches']:
        if patch['role'] == role:
            return patch['file']
    raise ValueError(f'Unknown patch role: {role}')


def run(repo_root, role='shell', mode='check', patch=None, missing_ok=False, out=print):
    """기준선을 처음 기록하거나 낮추고 검사 결과를 출력한다. 위반이 있으면 1을 반환한다."""
    repo_root = Path(repo_root)
    catalog = load_catalog(repo_root)
    patch = Path(patch) if patch else repo_root / role_patch(catalog, role)
    ratchet_path = repo_root / RATCHET_FILE
    data = (load_ratchet(ratchet_path) if ratchet_path.is_file()
            else {'schema_version': SCHEMA_VERSION, 'roles': {}})
    if mode != 'init' and role not in data['roles']:
        if missing_ok:
            out(f'패치 래칫: {role} 기준선이 없어 검사를 건너뛴다.')
            return 0
        raise ValueError(f'No {role} baseline in {RATCHET_FILE}; record it once with --init')
    if mode == 'init' and role in data['roles']:
        raise ValueError(f'{role} baseline already exists; lower it with --update')
    measurement = measure(patch, catalog)
    if mode == 'init':
        data['roles'][role] = {'baseline': initial_baseline(measurement), 'exceptions': []}
        write_ratchet(ratchet_path, data)
    elif mode == 'update':
        config = data['roles'][role]
        config['baseline'] = lowered_baseline(measurement, config['baseline'])
        write_ratchet(ratchet_path, data)
    config = data['roles'][role]
    findings = evaluate(measurement, config)
    try:
        shown = patch.resolve().relative_to(repo_root.resolve())
    except ValueError:
        shown = patch
    out(report(role, shown, measurement, config, findings))
    return 1 if findings else 0
