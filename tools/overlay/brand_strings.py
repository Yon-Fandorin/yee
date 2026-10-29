#!/usr/bin/env python3
"""변하지 않는 Git 블롭에서 제품명 GRD/GRDP와 번역 XTB 입력을 생성한다."""

import argparse
from collections import defaultdict
import copy
import hashlib
from html import escape
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from brand_config import REPO_ROOT, load_brand

GENERATED = PurePosixPath('chrome/app/yee_branding_strings')
MANIFEST = GENERATED / 'manifest.json'
MESSAGE = re.compile(r'<message\b[^>]*>.*?</message\s*>', re.DOTALL)
TEXT_TOKEN = re.compile(r'<!--.*?-->|<[^>]+>|[^<]+', re.DOTALL)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def xml(data):
    return ET.fromstring(data, parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))


def canonical(data):
    tree = xml(data)
    for node in tree.iter():
        if node.text is not None and not node.text.strip():
            node.text = None
        if node.tail is not None and not node.tail.strip():
            node.tail = None
    return ET.tostring(tree)


def safe_path(value):
    path = PurePosixPath(value)
    if path.is_absolute() or '..' in path.parts or '\\' in value:
        raise ValueError('Expected a confined source path: ' + value)
    return path


class GitSource:
    """cat-file 프로세스 하나로 체크아웃을 변경하지 않고 원본 입력을 읽는다."""
    def __init__(self, src):
        self.src = src
        self.revision = subprocess.check_output(
            ['git', '-C', str(src), 'rev-parse', 'HEAD'], text=True).strip()
        self.process = subprocess.Popen(['git', '-C', str(src), 'cat-file', '--batch'],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.cache = {}

    def read(self, relative, *, optional=False):
        relative = safe_path(str(relative)).as_posix()
        if relative not in self.cache:
            self.process.stdin.write(('HEAD:' + relative + '\n').encode())
            self.process.stdin.flush()
            header = self.process.stdout.readline()
            if optional and header == ('HEAD:' + relative + ' missing\n').encode('utf-8'):
                return None
            header = header.split()
            if len(header) != 3 or header[1] != b'blob':
                raise ValueError('Missing immutable Chromium input: ' + relative)
            self.cache[relative] = self.process.stdout.read(int(header[2]))
            if self.process.stdout.read(1) != b'\n':
                raise ValueError('Invalid git cat-file response')
        return self.cache[relative]

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.wait()


class Replacements:
    def __init__(self, policy, brand):
        self.brand = brand
        self.preserved = re.compile('|'.join(policy['preserved_message_patterns']))
        phrases = sorted(policy['protected_phrases'], key=len, reverse=True)
        aliases = sorted(policy['product_aliases'], key=len, reverse=True)
        self.tokens = re.compile(
            r'https?://(?:(?!&(?:lt|gt|quot|apos);)[^\s<>\"\'])+|(?P<protected>' + '|'.join(map(re.escape, phrases)) +
            r')|(?<![A-Za-z0-9_])(?P<product>' + '|'.join(map(re.escape, aliases)) +
            r')(?![A-Za-z0-9_])')

    def text(self, text):
        return self.tokens.sub(lambda m: escape(self.brand.name, quote=False)
                               if m.group('product') else m.group(), text)

    def body(self, fragment, *, attribution=False):
        # 속성값·예제·URL·플레이스홀더 마크업을 보존한다. 플레이스홀더의
        # 리터럴 제품명을 바꿔도 번역 지문에는 플레이스홀더 NAME을 사용한다.
        example_depth = 0
        project_link = False
        result = []
        for match in TEXT_TOKEN.finditer(fragment):
            token = match.group()
            if token.startswith('<'):
                if attribution and re.match(r'<ph\b', token):
                    if re.search(r'name=[\"\']BEGIN_LINK_CHROMIUM[\"\']', token):
                        project_link = True
                    elif re.search(r'name=[\"\']END_LINK_CHROMIUM[\"\']', token):
                        project_link = False
                if re.match(r'<ex(?:\s|>)', token):
                    example_depth += 1
                elif token.startswith('</ex'):
                    example_depth -= 1
                result.append(token)
            else:
                result.append(token if example_depth or project_link else self.text(token))
        return ''.join(result)


def fingerprint(node):
    """플레이스홀더 이름과 meaning을 포함한 GRIT 메시지 ID를 계산한다."""
    if node.get('use_name_for_id') == 'true':
        return node.get('name')
    text = node.text or ''
    for child in node:
        if child.tag == 'ph':
            text += child.get('name').upper()
        text += child.tail or ''
    text = text.strip()
    # GRIT의 앞뒤 공백 표시인 ''' ... ''' 구분자는 표시할 메시지와 ID
    # 계산에 포함하지 않는다.
    if text.startswith("'''") and text.endswith("'''"):
        text = text[3:-3].strip()

    def fp(value):
        value = int(hashlib.md5(value.encode('utf-8')).hexdigest()[:16], 16)
        return value - (1 << 64) if value & (1 << 63) else value

    value = fp(text)
    if node.get('meaning'):
        value = fp(node.get('meaning')) + (value << 1) + (1 if value < 0 else 0)
    return str(value & 0x7fffffffffffffff)


def product_parts_template(data):
    """Git 원본의 제품명 메시지를 생성한 GRDP 입력에 연결한다."""
    def replace(match):
        node = xml(match.group())
        names = {'IDS_PRODUCT_NAME': 'yee_product_names.grdp',
                 'IDS_SHORT_PRODUCT_NAME': None,
                 'IDS_APP_MENU_PRODUCT_NAME': 'yee_app_menu_name.grdp'}
        if node.get('name') not in names or (node.text or '').strip() != 'Chromium':
            return match.group()
        name = names[node.get('name')]
        return f'<part file="{name}" />' if name else ''
    text = MESSAGE.sub(replace, data.decode('utf-8'))
    text = re.sub(r'(<part file="yee_product_names.grdp" />)\n[ \t]*\n',
                  r'\1\n', text)
    return text.encode('utf-8')


def resolve(src, base, value):
    path = (src / base / value).resolve()
    if not path.is_relative_to(src):
        raise ValueError('GRIT input escapes Chromium: ' + value)
    return path.relative_to(src).as_posix()


def generate_plan(src, brand, policy):
    source = GitSource(src)
    replacements = Replacements(policy, brand)
    outputs, changes, preserved, originals = {}, [], [], {}

    def put(relative, data):
        if relative in outputs and outputs[relative] != data:
            raise ValueError('Conflicting generated input: ' + relative)
        outputs[relative] = data

    try:
        for root in policy['roots']:
            root = safe_path(root).as_posix()
            base = PurePosixPath(root).parent
            original_root = source.read(root)
            template = product_parts_template(original_root) if root == 'chrome/app/chromium_strings.grd' else original_root
            originals[root] = (original_root, template)
            translation_map = defaultdict(set)
            attribution_ids = set()
            visited, active = set(), set()

            def branded_destination(path):
                return (GENERATED / 'inputs' / path).as_posix()

            def rewrite_paths(text):
                def rewrite(match):
                    tag = match.group()
                    node = xml(tag if tag.endswith('/>') else tag + '</' + re.match(r'<(\w+)', tag).group(1) + '>')
                    key = 'file' if node.tag == 'part' else 'path'
                    value = node.get(key)
                    if not value or value in ('yee_product_names.grdp', 'yee_app_menu_name.grdp'):
                        return tag
                    original = resolve(src, base, value)
                    new_path = os.path.relpath(src / branded_destination(original), src / base).replace(os.sep, '/')
                    return re.sub(r'(' + key + r'\s*=\s*)([\"\']).*?\2',
                                  lambda m: m.group(1) + m.group(2) + new_path + m.group(2), tag, count=1)
                return re.sub(r'<(?:part|file)\b[^>]*>', rewrite, text)

            def process(path, data):
                if path in active:
                    raise ValueError('Cyclic GRIT part: ' + path)
                if path in visited:
                    return
                visited.add(path)
                active.add(path)
                if path != root:
                    originals[path] = (data,)
                node = xml(data)
                for part in node.iter('part'):
                    value = part.get('file')
                    if value in ('yee_product_names.grdp', 'yee_app_menu_name.grdp'):
                        continue
                    child = resolve(src, base, value)
                    process(child, source.read(child))

                def replace_message(match):
                    original = xml(match.group())
                    name = original.get('name', '')
                    attribution = name in policy.get('attribution_message_ids', [])
                    branded = name in policy.get('branded_message_ids', [])
                    if replacements.preserved.search(name) and not attribution and not branded:
                        preserved.append({'source': path, 'message_id': name})
                        return match.group()
                    text = replacements.body(match.group(), attribution=attribution)
                    if text != match.group():
                        converted = xml(text)
                        old_id, new_id = fingerprint(original), fingerprint(converted)
                        if old_id != new_id and original.get('translateable') != 'false':
                            translation_map[old_id].add(new_id)
                            if attribution:
                                attribution_ids.add(new_id)
                        changes.append({'source': path, 'message_id': name, 'old_id': old_id, 'new_id': new_id})
                    return text

                text = MESSAGE.sub(replace_message, data.decode('utf-8'))
                put(path if path == root else branded_destination(path), rewrite_paths(text).encode('utf-8'))
                active.remove(path)

            process(root, template)
            translations = xml(template).find('translations')
            if translations is not None:
                for entry in translations.iter('file'):
                    path = resolve(src, base, entry.get('path'))
                    data = source.read(path)
                    originals[path] = (data,)
                    tree = xml(data)
                    extra = []
                    seen = {(node.get('id'), node.get('gender')) for node in tree}
                    for translation in list(tree):
                        for new_id in sorted(translation_map.get(translation.get('id'), ())):
                            key = (new_id, translation.get('gender'))
                            if key in seen:
                                continue
                            seen.add(key)
                            new = copy.deepcopy(translation)
                            new.set('id', new_id)
                            # 업스트림의 언어·성별과 ph 요소를 보존하고 제품명만 바꾼다.
                            converted = replacements.body(ET.tostring(new, encoding='unicode'),
                                                          attribution=new_id in attribution_ids)
                            extra.append(xml(converted))
                    tree.extend(extra)
                    put(branded_destination(path), b'<?xml version="1.0" encoding="UTF-8"?>\n' +
                        ET.tostring(tree, encoding='utf-8') + b'\n')

            dependency_path = root + '.gritdeps'
            # 큰 generated/component 리소스 계열에만 gritdeps가 있다.
            data = source.read(dependency_path, optional=True)
            if data is not None:
                originals[dependency_path] = (data,)
                lines = data.decode('utf-8').splitlines()
                rendered = []
                for line in lines:
                    target = branded_destination(line)
                    rendered.append(target if target in outputs else line)
                put(dependency_path, ('\n'.join(rendered) + '\n').encode())

        product_root = 'chrome/app/chromium_strings.grd'
        if product_root not in outputs:
            original = source.read(product_root)
            template = product_parts_template(original)
            originals[product_root] = (original, template)
            put(product_root, template)

        manifest = {'schema_version': 1, 'revision': source.revision,
                    'name': brand.name, 'short_name': brand.short_name,
                    'policy_sha256': digest(brand.strings_policy.read_bytes()),
                    'changed_messages': changes, 'preserved_messages': preserved,
                    'files': [{'path': path, 'sha256': digest(data)} for path, data in sorted(outputs.items())]}
        put(MANIFEST.as_posix(), (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode())
        return outputs, originals, manifest
    finally:
        source.close()


def disabled_policy_status(src):
    repository = subprocess.run(
        ['git', '-C', str(src), 'rev-parse', '--is-inside-work-tree'], capture_output=True)
    arguments = ['git', '-C', str(src), 'grep']
    # 비Git 인벤토리도 검사하고, 실제 체크아웃에서는 추적된 입력만 검사한다.
    if repository.returncode or repository.stdout.strip() != b'true':
        arguments.append('--no-index')
    arguments += ['-l', '-z', '-F', '-e', GENERATED.name + '/', '--',
                  '*.grd', '*.grdp', '*.gritdeps']
    references = subprocess.run(arguments, capture_output=True, text=True, encoding='utf-8')
    if references.returncode not in (0, 1):
        raise ValueError('Cannot inspect managed brand string references: ' + references.stderr.strip())
    mismatches = [path for path in references.stdout.split('\0') if path]
    generated = src / GENERATED
    if (generated.is_symlink() or (generated.exists()
            and (not generated.is_dir() or any(generated.iterdir())))):
        mismatches.append(GENERATED.as_posix())
    status = {'enabled': False, 'applied': not mismatches, 'mismatches': sorted(set(mismatches))}
    if mismatches:
        status['error'] = ('Cannot disable strings_policy while managed brand string inputs remain. '
                           'Keep strings_policy enabled, or restore the managed Chromium GRD/GRDP/.gritdeps '
                           'inputs to their upstream state and remove ' + GENERATED.as_posix()
                           + ' before disabling it. Remaining managed inputs: '
                           + ', '.join(status['mismatches']))
    return status


def install(src, brand, check_only=False):
    src = src.resolve()
    if brand.strings_policy is None:
        status = disabled_policy_status(src)
        if not status['applied']:
            raise ValueError(status['error'])
        source = GitSource(src)
        try:
            product_root = 'chrome/app/chromium_strings.grd'
            original = source.read(product_root)
            template = product_parts_template(original)
            outputs = {product_root: template}
            originals = {product_root: (original, template)}
            manifest = None
        finally:
            source.close()
    else:
        policy = json.loads(brand.strings_policy.read_text(encoding='utf-8'))
        if policy.get('schema_version') != 1:
            raise ValueError('Unsupported brand string policy')
        outputs, originals, manifest = generate_plan(src, brand, policy)
    old_path = src / MANIFEST
    old = json.loads(old_path.read_text(encoding='utf-8')) if old_path.exists() else {'files': []}
    managed = {item['path']: item['sha256'] for item in old['files']}
    pending = []
    # 복사할 GRDP/XTB 원본의 로컬 수정도 거부한다. 생성 파일로 루트를
    # 연결하면서 체크아웃의 수정 내용을 숨기지 않도록 한다.
    for relative, baseline in originals.items():
        if relative not in outputs and (src / relative).read_bytes() not in baseline:
            raise ValueError('Unmanaged change to branding input: ' + relative)
    for relative, data in outputs.items():
        path = src / relative
        for ancestor in (path, *path.parents):
            if ancestor.is_symlink():
                raise ValueError('Brand string output traverses a symlink: ' + relative)
            if ancestor != path and ancestor.exists() and not ancestor.is_dir():
                raise ValueError('Brand string output parent is not a directory: ' + relative)
            if ancestor == src:
                break
        if path.exists() and not path.is_file():
            raise ValueError('Brand string output is not a file: ' + relative)
        current = path.read_bytes() if path.exists() else None
        if current == data:
            continue
        if relative in originals and current not in originals[relative] and digest(current or b'') != managed.get(relative):
            # 이전 제품명 연결 패치는 두 메시지 사이의 빈 줄도 지우므로
            # 공백 대신 XML 구조로 해당 변경을 비교한다.
            if relative != 'chrome/app/chromium_strings.grd' or canonical(current) != canonical(originals[relative][1]):
                raise ValueError('Unmanaged change to branding input: ' + relative)
        if current is not None and relative.startswith(GENERATED.as_posix() + '/') and relative != MANIFEST.as_posix() and relative not in managed:
            raise ValueError('Unmanaged generated string output: ' + relative)
        pending.append((path, data))
    for path, data in pending:
        if not check_only:
            path.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
                temporary.write(data)
                temporary_path = Path(temporary.name)
            try:
                temporary_path.chmod(0o644)
                temporary_path.replace(path)
            finally:
                temporary_path.unlink(missing_ok=True)
    detail = (f"; {len(manifest['changed_messages'])} messages, {len(manifest['files'])} managed files"
              if manifest is not None else '')
    print(f"{'Would install' if check_only else 'Installed'} {len(pending)} brand string inputs{detail}")


def check_inputs(src, brand):
    """설치된 문자열을 Git 원본에서 다시 생성한 결과와 비교한다."""
    src = src.resolve()
    if brand.strings_policy is None:
        return disabled_policy_status(src)
    policy = json.loads(brand.strings_policy.read_text(encoding='utf-8'))
    outputs, originals, manifest = generate_plan(src, brand, policy)
    mismatches = [relative for relative, data in outputs.items()
                  if not (src / relative).is_file() or (src / relative).read_bytes() != data]
    mismatches.extend(relative for relative, baseline in originals.items()
                      if relative not in outputs and (src / relative).read_bytes() not in baseline)
    return {'enabled': True, 'applied': not mismatches, 'mismatches': sorted(mismatches),
            'changed_messages': len(manifest['changed_messages']),
            'managed_files': len(manifest['files']), 'revision': manifest['revision']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('chromium_src', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        if not args.chromium_src.is_absolute():
            raise ValueError('Chromium source path must be absolute')
        install(args.chromium_src.resolve(), load_brand(), args.check)
    except (OSError, ValueError, KeyError, ET.ParseError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
