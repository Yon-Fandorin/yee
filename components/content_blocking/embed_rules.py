#!/usr/bin/env python3
"""Embed the same immutable bundle into browser and renderer targets."""
import hashlib
import json
from pathlib import Path
import sys
from preprocess_filters import CONDITIONS, preprocess

easylist, easyprivacy, filters, resources, test_filters, test_resources, sources, runtime, output, *compiled_inputs = map(Path, sys.argv[1:])
for entry in json.loads(sources.read_text(encoding="utf-8")):
    path = sources.parent / entry["file"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
        raise ValueError(f"Pinned filter checksum mismatch: {path}")
texts = ["\n".join(preprocess(path.read_text(encoding="utf-8"), str(path)) for path in [easylist, easyprivacy, filters]),
         resources.read_text(encoding="utf-8"), test_filters.read_text(encoding="utf-8"),
         json.dumps(json.loads(resources.read_text(encoding="utf-8")) + json.loads(test_resources.read_text(encoding="utf-8"))),
         runtime.read_text(encoding="utf-8")]
generation = hashlib.sha256("\0".join(texts).encode()).hexdigest()
header = ["// Generated. Do not edit.", "#pragma once",
          "#include <array>", "#include <string_view>", "#include <utility>",
          "namespace yee::content_blocking {"]
for name, text in zip(["kBundledFilters", "kBundledResources", "kTestFilters", "kTestResources", "kScriptletRuntime"], texts):
    delimiter = "yee_" + hashlib.sha256(text.encode()).hexdigest()[:10]
    assert f'){delimiter}"' not in text
    header.append(f'inline constexpr std::string_view {name} = R"{delimiter}({text}){delimiter}";')
header.append('inline constexpr std::array<std::string_view, 2> kBundledBaselineOriginals = {')
for path in [easylist, easyprivacy]:
    text = path.read_text(encoding="utf-8")
    delimiter = "yee_" + hashlib.sha256(text.encode()).hexdigest()[:10]
    assert f'){delimiter}"' not in text
    header.append(f'R"{delimiter}({text}){delimiter}",')
header.append('};')
owned = preprocess(filters.read_text(encoding="utf-8"), str(filters))
delimiter = "yee_" + hashlib.sha256(owned.encode()).hexdigest()[:10]
header.append(f'inline constexpr std::string_view kOwnedFilters = R"{delimiter}({owned}){delimiter}";')
header.append(f'inline constexpr std::array<std::pair<std::string_view, bool>, {len(CONDITIONS)}> kFilterConditions = {{{{')
header.extend(f'  {{{json.dumps(name)}, {str(value).lower()}}},' for name, value in sorted(CONDITIONS.items()))
header.append('}};')
header += [f'inline constexpr char kBundleGeneration[] = "{generation}";', "}"]
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("\n".join(header) + "\n", encoding="utf-8")
if compiled_inputs:
    filters_output, generation_output = compiled_inputs
    filters_output.write_text(texts[0], encoding="utf-8")
    generation_output.write_text(generation, encoding="utf-8")
