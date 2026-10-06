#!/usr/bin/env python3
"""Build pinned Settings frontend dependencies in the ignored GN output tree."""

import hashlib
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


class BootstrapParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.inline = False
        self.scripts = []
        self.script_types = []

    def handle_starttag(self, tag, attrs):
        if tag == 'script' and not dict(attrs).get('src'):
            self.inline = True
            self.scripts.append('')
            self.script_types.append(dict(attrs).get('type'))

    def handle_endtag(self, tag):
        if tag == 'script':
            self.inline = False

    def handle_data(self, data):
        if self.inline:
            self.scripts[-1] += data


def main():
    output = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    workspace = output.parent / 'frontend_build'
    workspace.mkdir(parents=True, exist_ok=True)
    expected = set()
    for path in source.rglob('*'):
        if path.is_file():
            relative = path.relative_to(source)
            expected.add(relative)
            target = workspace / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists() or path.read_bytes() != target.read_bytes():
                shutil.copyfile(path, target)
    # Only remove copied source files. Build caches and dependencies stay local.
    manifest = workspace / '.source-manifest'
    if manifest.exists():
        for previous in manifest.read_text().splitlines():
            if Path(previous) not in expected:
                (workspace / previous).unlink(missing_ok=True)
    manifest.write_text('\n'.join(sorted(str(p) for p in expected)))

    npm = shutil.which('npm.cmd' if os.name == 'nt' else 'npm')
    if not npm:
        raise RuntimeError('Settings UI requires Node >=22.17 and npm on PATH.')
    lock = source / 'package-lock.json'
    digest = hashlib.sha256(lock.read_bytes()).hexdigest()
    stamp = workspace / '.dependencies-stamp'
    if (not (workspace / 'node_modules').is_dir() or
            not stamp.exists() or stamp.read_text() != digest):
        subprocess.run([npm, 'ci', '--ignore-scripts', '--no-audit', '--no-fund'],
                       cwd=workspace, check=True)
        stamp.write_text(digest)
    subprocess.run([npm, 'run', 'check'], cwd=workspace, check=True)
    subprocess.run([npm, 'run', 'build'], cwd=workspace, check=True)

    build = workspace / 'build'
    html_file = build / 'index.html'
    document = html_file.read_text()
    parser = BootstrapParser()
    parser.feed(document)
    if len(parser.scripts) != 1:
        raise RuntimeError('Expected one static SvelteKit bootstrap script.')
    if parser.script_types != [None]:
        raise RuntimeError('Review the new SvelteKit bootstrap execution mode.')
    bootstrap = parser.scripts[0]
    # Keep Chromium's script-src policy: serve the generated bootstrap externally.
    # Use script start/end positions rather than rewriting any frontend code.
    document, count = re.subn(r'<script\b(?![^>]*\bsrc=)[^>]*>.*?</script>',
                             '<script src="/bootstrap.js"></script>',
                             document, flags=re.DOTALL)
    if count != 1:
        raise RuntimeError('Could not externalize the static bootstrap.')
    html_file.write_text(document)
    (build / 'bootstrap.js').write_text(bootstrap)

    parts = ['// Generated Settings WebUI resources.\n'
             '#ifndef YEE_SETTINGS_RESOURCES_H_\n#define YEE_SETTINGS_RESOURCES_H_\n'
             '#include <string_view>\nnamespace yee::settings_resources {\n'
             'struct Resource {std::string_view path; std::string_view response;};\n'
             'inline constexpr Resource kResources[] = {\n']
    for path in sorted(build.rglob('*')):
        if not path.is_file():
            continue
        if path.suffix not in ('.html', '.js', '.css', '.json'):
            raise RuntimeError(f'Unexpected binary frontend resource: {path}')
        content = path.read_text()
        if ')yee_settings"' in content:
            raise RuntimeError(f'Raw string delimiter in {path}')
        relative = path.relative_to(build).as_posix()
        parts.append(f'{{"{relative}", R"yee_settings({content})yee_settings"}},\n')
    parts.append('};\n}  // namespace yee::settings_resources\n#endif\n')
    output.write_text(''.join(parts))

    # Derive loadTimeData keys from the GRIT IDs so the C++ map cannot drift.
    grd = ET.parse(source.parent / 'strings/settings_strings.grd')
    mapping = ['// Generated from settings_strings.grd.\n',
               'inline constexpr webui::LocalizedString kSettingsStrings[] = {\n']
    for message in grd.iter('message'):
        name = message.attrib['name']
        words = name.removeprefix('IDS_YEE_SETTINGS_').lower().split('_')
        key = words[0] + ''.join(word.capitalize() for word in words[1:])
        mapping.append(f'  {{"{key}", {name}}},\n')
    mapping.append('};\n')
    (output.parent / 'settings_string_map.inc').write_text(''.join(mapping))


if __name__ == '__main__':
    main()
