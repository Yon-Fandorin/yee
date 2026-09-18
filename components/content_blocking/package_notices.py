#!/usr/bin/env python3
"""Emit attribution, license texts and matching component source for distribution."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile

vendor, component, notices, archive = map(Path, sys.argv[1:])
manifest = json.loads((vendor / "manifest.json").read_text())
source_license = component.parents[1] / "LICENSE"
for entry in json.loads((component / "data/sources.json").read_text()):
    path = component / "data" / entry["file"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
        raise ValueError(f"Pinned filter checksum mismatch: {path}")
supplemental_root = component / "data/third-party-licenses"
supplemental = json.loads((supplemental_root / "sources.json").read_text())
for entry in supplemental:
    path = supplemental_root / entry["file"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
        raise ValueError(f"Supplemental license checksum mismatch: {path}")
lines = ["Yee Content Blocking — third-party notices", "",
         "adblock-rust is licensed under MPL-2.0. Complete original sources for",
         "the engine and dependencies are in YeeContentBlockingSources.tar.xz",
         "beside this file. No changes were made to the upstream engine sources.",
         "The archive also contains the licensed third-party filter data.",
         "Additional GPL community filter data, its notices and matching source",
         "are separately provided in YeeCommunityFilterNotices.txt and",
         "YeeCommunityFilterSources.tar.xz beside this file.",
         "Independently authored Yee integration sources are not part of it.",
         "Upstream: https://github.com/brave/adblock-rust", "",
         (component / "data/LICENSE-EasyList.txt").read_text(),
         (component / "data/sources.json").read_text()]
for crate in manifest:
    directory = vendor / crate["path"]
    for filename, expected in crate["files"].items():
        if hashlib.sha256((directory / filename).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Original crate source changed: {crate['name']}/{filename}")
    lines += ["", f"{crate['name']} {crate['version']} — {crate['license']}",
              f"Source: https://crates.io/crates/{crate['name']}/{crate['version']}"]
    license_files = sorted(p for p in directory.rglob("*") if p.is_file() and
                           p.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE")))
    additions = [e for e in supplemental if (e["name"], e["version"]) == (crate["name"], crate["version"])]
    if not license_files and not additions:
        raise ValueError(f"No license text available for {crate['name']} {crate['version']}")
    for path in license_files:
        lines += ["", path.relative_to(directory).as_posix(), path.read_text(errors="replace")]
    for entry in additions:
        lines += ["", entry["note"], f"License source: {entry['source']}",
                  (supplemental_root / entry["file"]).read_text()]
notices.parent.mkdir(parents=True, exist_ok=True)
notices.write_text("\n".join(lines) + "\n")
archive.parent.mkdir(parents=True, exist_ok=True)
temporary = archive.with_name(archive.name + ".tmp")
with tarfile.open(temporary, "w:xz") as output:
    info = output.gettarinfo(str(source_license), "LICENSE")
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    with source_license.open("rb") as stream:
        output.addfile(info, stream)
    # Complete original source for every crate, including its own license.
    # Archive timestamps/ownership are fixed for reproducible output.
    for root, prefix in [(vendor, "third_party/rust/yee_adblock"),
                         (component / "data", "third_party/filter_data")]:
        if root == vendor:
            paths = [vendor / "manifest.json", vendor / "sources.gni"]
            paths += [vendor / c["path"] / p for c in manifest for p in [*c["files"], "BUILD.gn"]]
        else:
            # Package only third-party data and its original attribution.
            # A whitelist prevents private Yee code, test rules or local caches
            # from entering a redistributable third-party source archive.
            paths = [root / p for p in ["easylist.txt", "easyprivacy.txt", "sources.json", "LICENSE-EasyList.txt"]]
            paths += [supplemental_root / "sources.json"]
            paths += [supplemental_root / e["file"] for e in supplemental]
        for path in sorted(paths):
            if path.is_symlink():
                raise ValueError(f"Unexpected symlink in source package: {path}")
            info = output.gettarinfo(str(path), str(Path(prefix) / path.relative_to(root)))
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with path.open("rb") as stream: output.addfile(info, stream)
temporary.replace(archive)
