#!/usr/bin/env python3
# Copyright 2026 The Yee Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Build a standalone, redistributable ABP filter-data package.

This independently authored tool is distributed with its inputs. It does not
import browser implementation code or include it in the source archive.
"""
import hashlib
import io
import json
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tarfile

from preprocess_filters import preprocess

RULES = "YeeCommunityFilters.txt"
MANIFEST = "YeeCommunityFilterManifest.json"
NOTICES = "YeeCommunityFilterNotices.txt"
ARCHIVE = "YeeCommunityFilterSources.tar.xz"
RESOURCES = "YeeCommunityResources.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def original(root, name, expected):
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Invalid source path: {name}")
    path = root / relative
    if any((root / Path(*relative.parts[:n])).is_symlink()
           for n in range(len(relative.parts) + 1)):
        raise ValueError(f"Symlink in source path: {name}")
    data = path.read_bytes()
    if digest(data) != expected:
        raise ValueError(f"Pinned source checksum mismatch: {name}")
    return data


def supported(line, redirects, scriptlets=()):
    if line.lstrip().startswith("!#include"):
        raise ValueError("Unresolved include directive")
    if line.lstrip().startswith(("!", "[")) or not line.strip():
        return None
    call = re.search(r"#(?:@)?#\s*\+js\(\s*([^,)]*)", line)
    if call:
        name = call[1].strip().strip("\"'")
        if not name and "#@#" in line:
            return None  # Entire-site scriptlet exception.
        name = name if name.endswith(".js") else name + ".js"
        return None if name in scriptlets else "unsupported_scriptlet"
    if any(marker in line for marker in ["#?#", "#@?#", "#$#", "#@$#",
                                         "#%#", "#@%#", "##^", "#@#^"]):
        return "extended_cosmetic"
    if "##" in line or "#@#" in line:
        if re.search(r":(?:has-text|contains|matches-[\w-]+|remove(?:-attr|-class)?|style|xpath|upward|watch-attr)\(", line):
            return "extended_cosmetic"
        return None
    if re.search(r"(?:\$|,)(?:replace|urltransform|uritransform)=", line):
        return "unsupported_network"
    for match in re.finditer(r"(?:\$|,)(?:redirect(?:-rule)?|rewrite)=([^,]+)", line):
        name = re.sub(r":-?\d+$", "", match[1])
        if name not in redirects:
            return "unsupported_redirect"
    return None


def build(root, output, node_executable="node", bundled_filters=None,
          bundled_generation=None):
    root, output = Path(root), Path(output)
    source_manifest = root / "sources.json"
    metadata = json.loads(source_manifest.read_text())
    if metadata["schema_version"] != 1:
        raise ValueError("Unsupported source manifest")
    license_data = original(root, metadata["license_file"], metadata["license_sha256"])
    assets = {"data/sources.json": source_manifest.read_bytes(),
              "data/" + metadata["license_file"]: license_data}
    scriptlet_manifest = root / "scriptlet-sources.json"
    scriptlet_metadata = json.loads(scriptlet_manifest.read_text())
    if scriptlet_metadata["schema_version"] != 1:
        raise ValueError("Unsupported scriptlet source manifest")
    assets["data/scriptlet-sources.json"] = scriptlet_manifest.read_bytes()
    for entry in scriptlet_metadata["sources"]:
        name = "data/" + entry["file"]
        if name in assets:
            raise ValueError("Duplicate resource source file")
        assets[name] = original(root, entry["file"], entry["sha256"])
    assets["data/package.json"] = (root / "package.json").read_bytes()
    # Only ABP data and an opaque generation identifier accompany the optional
    # cache compiler. No native adapter or private JavaScript is included.
    for name, source in [("bundled-filters.txt", bundled_filters),
                         ("bundled-generation.txt", bundled_generation)]:
        path = Path(source) if source else root / name
        if source or path.is_file():
            assets["data/" + name] = path.read_bytes()
    tool_root = Path(__file__).resolve().parent
    # Chromium's node.py is a locator/runner whose CLI discards stdout. Locate
    # its platform-specific binary, then capture the compiler's JSON directly.
    if str(node_executable).endswith(".py"):
        node_executable = runpy.run_path(str(node_executable))["GetBinaryPath"]()
    node_command = [str(node_executable)]
    resource_data = subprocess.check_output(
        node_command + [str(tool_root / "build_scriptlet_resources.mjs"), str(root)])
    resources = json.loads(resource_data)
    scriptlets = {name for entry in resources
                  if entry["kind"] == {"mime": "application/javascript"}
                  for name in [entry["name"], *entry["aliases"]]}
    redirects = set(metadata["supported_redirects"])
    redirects.update(name for entry in resources if entry["permission"] == 0
                     for name in [entry["name"], *entry["aliases"]])
    excluded = {"unsupported_scriptlet": 0, "extended_cosmetic": 0,
                "unsupported_redirect": 0, "unsupported_network": 0}
    lines = ["! Yee community filter data — modified selection of uAssets",
             "! SPDX-License-Identifier: GPL-3.0",
             "! Revision: " + metadata["revision"],
             "! Modified: " + metadata["modification_date"],
             "! Modification: Chromium branches; supported rules and original scriptlets.",
             "! Originals, exclusion report and builder accompany this file."]
    count = 0
    seen = set()
    source_names = {entry["file"] for entry in metadata["sources"]}
    included = set()
    for entry in metadata["sources"]:
        if entry["file"] in seen:
            raise ValueError("Duplicate source file")
        seen.add(entry["file"])
        raw = original(root, entry["file"], entry["sha256"])
        assets["data/" + entry["file"]] = raw
        lines.append("! Source: " + entry["source"])
        for line in preprocess(raw.decode("utf-8"), entry["file"]).splitlines():
            if line.lstrip().startswith("!#include "):
                target = str(Path(entry["file"]).parent / line.strip()[10:].strip())
                if target not in source_names:
                    raise ValueError(f"Unpinned include: {target}")
                included.add(target)
                continue  # Each catalog source is appended exactly once below.
            reason = supported(line, redirects, scriptlets)
            if reason:
                excluded[reason] += 1
            else:
                lines.append(line)
                if line.strip() and not line.lstrip().startswith(("!", "[")):
                    count += 1
    rules = ("\n".join(lines) + "\n").encode()
    notice = ("Yee community filter data\n\n"
              "Derived filter text is licensed under GPL-3.0.\n"
              "Original uBlock JavaScript is GPL-3.0-or-later, subject to\n"
              "individual file notices. Compiled resources retain those terms.\n"
              "Brave-specific original resources retain MPL-2.0 and individual\n"
              "file notices; their original license and source are included.\n"
              "The uBlock Origin contributors retain their copyright notices.\n"
              "Original inputs, exact revision, selection report and the public\n"
              "builder are in " + ARCHIVE + " beside this file.\n"
              "Build: python3 build_filter_pack.py data OUTPUT_DIRECTORY\n"
              "You may modify and redistribute this filter data under its license.\n"
              "This package contains filters and a separate JavaScript program.\n"
              "Its public compiler and all original modules accompany it.\n"
              "It does not contain Yee browser implementation.\n\n"
              + json.dumps(metadata, indent=2) + "\n\n"
              + json.dumps(scriptlet_metadata, indent=2) + "\n\n"
              + license_data.decode() + "\n\n"
              + assets["data/uBlock/LICENSE.txt"].decode() + "\n\n"
              + assets["data/brave-resources/LICENSE"].decode()).encode()
    report = {"schema_version": 2, "revision": metadata["revision"],
              "modification_date": metadata["modification_date"],
              "rules_file": RULES, "rules_sha256": digest(rules),
              "rules_bytes": len(rules), "selected_rules": count,
              "resources_file": RESOURCES, "resources_sha256": digest(resource_data),
              "resources_bytes": len(resource_data), "resource_count": len(resources),
              "scriptlet_count": sum(bool(entry.get("scriptlet")) for entry in resources),
              "brave_resource_count": sum(bool(entry.get("brave_resource")) for entry in resources),
              "scriptlet_revision": scriptlet_metadata["revision"],
              "excluded_rules": excluded, "license": "GPL-3.0",
              "resolved_includes": sorted(included),
              "source_archive": ARCHIVE, "notices_file": NOTICES,
              "notices_sha256": digest(notice), "sources": metadata["sources"]}
    assets["selection-report.json"] = (json.dumps(report, indent=2) + "\n").encode()
    for name in ["build_filter_pack.py", "preprocess_filters.py", "build_scriptlet_resources.mjs", "scriptlet_runtime.js",
                 "compile_filters.rs", "compile_filter_snapshot.py"]:
        assets[name] = (tool_root / name).read_bytes()
    # These two files are intentionally public. Never scan product directories.
    assets["LICENSE-builder"] = (root / "LICENSE-builder").read_bytes()
    assets["README.md"] = (root / "README.md").read_bytes()
    assets["data/LICENSE-builder"] = assets["LICENSE-builder"]
    assets["data/README.md"] = assets["README.md"]
    output.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output / ARCHIVE, "w:xz") as archive:
        for name, data in sorted(assets.items()):
            info = tarfile.TarInfo(name)
            info.size, info.mtime, info.mode = len(data), 0, 0o644
            archive.addfile(info, io.BytesIO(data))
    report["source_archive_sha256"] = digest((output / ARCHIVE).read_bytes())
    for name, data in [(RULES, rules), (RESOURCES, resource_data), (NOTICES, notice),
                       (MANIFEST, (json.dumps(report, indent=2) + "\n").encode())]:
        (output / name).write_bytes(data)
    return report


if __name__ == "__main__":
    build(*sys.argv[1:])
