#!/usr/bin/env python3
from pathlib import Path
import sys
generic, youtube, validation, output = map(Path, sys.argv[1:])
lines = ['// Generated.', '#pragma once', 'namespace yee::content_blocking {']
for name, path in [('kGenericCosmeticScript', generic), ('kYouTubeScript', youtube),
                   ('kSelectorValidationScript', validation)]:
    text = path.read_text()
    assert ')yee_script"' not in text
    lines.append(f'inline constexpr char {name}[] = R"yee_script({text})yee_script";')
lines.append('}')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('\n'.join(lines) + '\n')
