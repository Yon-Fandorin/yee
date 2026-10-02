#!/usr/bin/env python3
"""Pin adblock-rust's private GN dependency graph without updating Chromium crates.

Run cargo metadata first with the Chromium Rust toolchain and a workspace-local
CARGO_HOME. This command copies those verified registry sources and generates
GN targets. Build-time downloads are never needed. Upstream files are unchanged.
"""

import argparse
import json
import hashlib
from pathlib import Path
import shutil
import tomllib
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "third_party/yee_adblock"
GN_ROOT = "//third_party/rust/yee_adblock"


def verify_registry_source(package, upstream, expected):
    archive = (upstream.parent.parent.parent / "cache" / upstream.parent.name /
               f"{package['name']}-{package['version']}.crate")
    if not expected or hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise ValueError(f"Cargo.lock registry archive checksum mismatch: {archive}")
    prefix = f"{package['name']}-{package['version']}"
    verified = set()
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            if not member.isfile():
                continue
            relative = Path(member.name).relative_to(prefix)
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError(f"Invalid registry source path: {member.name}")
            verified.add(relative.as_posix())
            if (upstream / relative).read_bytes() != source.extractfile(member).read():
                raise ValueError(f"Registry source differs from archive: {upstream / relative}")

    actual = {p.relative_to(upstream).as_posix() for p in upstream.rglob('*') if p.is_file()}
    if actual - verified - {'.cargo-ok', '.cargo-checksum.json'}:
        raise ValueError(f"Unverified files in registry source: {sorted(actual - verified)}")
    if any(p.is_symlink() for p in upstream.rglob('*')):
        raise ValueError(f"Registry source contains links: {upstream}")


def quoted(value):
    # GN strings use $ interpolation, unlike JSON strings.
    return json.dumps(str(value), ensure_ascii=False).replace("$", r"\$")


def generate(metadata, destination_root):
    packages = {p["id"]: p for p in metadata["packages"]}
    lock = tomllib.loads((Path(metadata["workspace_root"]) / "Cargo.lock").read_text())
    checksums = {(p["name"], p["version"]): p.get("checksum") for p in lock["package"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    seeds = [p["id"] for p in packages.values() if p["name"] in {"adblock", "serde_json"}]
    wanted = set()

    def visit(package_id):
        if package_id in wanted:
            return
        wanted.add(package_id)
        for dep in nodes[package_id]["deps"]:
            if any(kind["kind"] != "dev" for kind in dep["dep_kinds"]):
                visit(dep["pkg"])

    for seed in seeds:
        visit(seed)
    names = {pid: packages[pid]["name"].replace("-", "_") + "_" +
             packages[pid]["version"].replace(".", "_") for pid in wanted}
    manifest = []
    for pid in sorted(wanted, key=lambda pid: names[pid]):
        package = packages[pid]
        node = nodes[pid]
        upstream = Path(package["manifest_path"]).parent
        verify_registry_source(package, upstream, checksums[(package["name"], package["version"])])
        destination = destination_root / names[pid]
        shutil.copytree(upstream, destination)
        # Registry archive checksums and original files were verified above.
        target = next(t for t in package["targets"]
                      if any(kind in {"lib", "proc-macro"} for kind in t["kind"]))
        prefix = f"{GN_ROOT}/{names[pid]}"
        sources = sorted(f"{prefix}/{p.relative_to(destination).as_posix()}"
                         for p in destination.rglob("*.rs"))
        inputs = sorted(f"{prefix}/{p.relative_to(destination).as_posix()}"
                        for p in destination.rglob("*") if p.is_file() and p.suffix != ".rs")
        fields = {
            "crate_name": target["name"],
            "rustc_metadata": f"yee-{package['name']}-{package['version']}",
            "crate_type": "proc-macro" if "proc-macro" in target["kind"] else "rlib",
            "crate_root": f"{prefix}/{Path(target['src_path']).relative_to(upstream).as_posix()}",
            "edition": package["edition"],
            "cargo_pkg_name": package["name"],
            "cargo_pkg_version": package["version"],
            "cargo_pkg_authors": ", ".join(package["authors"]),
            "cargo_pkg_description": package.get("description") or "",
            "cargo_pkg_repository": package.get("repository") or "",
        }
        lists = {"sources": sources, "inputs": inputs, "features": node["features"]}
        aliases = {}
        for category, kind in [("deps", None), ("build_deps", "build")]:
            lists[category] = []
            for dep in node["deps"]:
                if dep["pkg"] not in wanted or not any(k["kind"] == kind for k in dep["dep_kinds"]):
                    continue
                label = f"{GN_ROOT}/{names[dep['pkg']]}:lib"
                lists[category].append(label)
                dep_target = next(t for t in packages[dep["pkg"]]["targets"]
                                  if any(k in {"lib", "proc-macro"} for k in t["kind"]))
                if dep["name"] != dep_target["name"]:
                    aliases[dep["name"]] = label
        build = next((t for t in package["targets"] if "custom-build" in t["kind"]), None)
        if build:
            fields["build_root"] = f"{prefix}/{Path(build['src_path']).relative_to(upstream).as_posix()}"
            lists["build_sources"] = sources
            if package["name"] in {"serde", "serde_core"}:
                lists["build_script_outputs"] = ["private.rs"]
            elif package["name"] == "selectors":
                lists["build_script_outputs"] = ["ascii_case_insensitive_html_attributes.rs"]
        lines = ["# Generated by tools/overlay/vendor-yee-adblock.py.",
                 "# Upstream sources and their licenses are retained unchanged.",
                 'import("//build/rust/cargo_crate.gni")', '', 'cargo_crate("lib") {',
                 "  allow_unsafe = true", "  build_native_rust_unit_tests = false"]
        lines += [f"  {key} = {quoted(value)}" for key, value in fields.items()]
        if package["name"] == "flatbuffers":
            # This upstream build script detects the Chromium nightly toolchain
            # and opts into TrustedLen. Grant only that feature to this crate.
            lines += ['  rustflags = [ "-Zallow-features=trusted_len" ]',
                      '  library_configs -= [ "//build/config/compiler:disallow_unstable_features" ]']
        for key, values in lists.items():
            if key == "features" and not values:
                continue
            lines += [f"  {key} = ["] + [f"    {quoted(v)}," for v in sorted(set(values))] + ["  ]"]
        if aliases:
            lines += ["  aliased_deps = {"] + [f"    {key} = {quoted(value)}" for key, value in aliases.items()] + ["  }"]
        lines += ['  library_configs -= [ "//build/config/compiler:chromium_code" ]',
                  '  executable_configs -= [ "//build/config/compiler:chromium_code" ]',
                  '  proc_macro_configs -= [ "//build/config/compiler:chromium_code" ]', '}']
        (destination / "BUILD.gn").write_text("\n".join(lines) + "\n")
        manifest.append({"name": package["name"], "version": package["version"],
                         "license": package["license"], "source": package["source"],
                         "registry_checksum": checksums[(package["name"], package["version"])],
                         "path": names[pid],
                         "files": {p.relative_to(upstream).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in sorted(upstream.rglob("*")) if p.is_file()}})
    destination_root.mkdir(parents=True, exist_ok=True)
    (destination_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    originals = [f"{GN_ROOT}/{c['path']}/{p}" for c in manifest for p in c["files"]]
    originals += [f"{GN_ROOT}/{c['path']}/BUILD.gn" for c in manifest]
    (destination_root / "sources.gni").write_text(
        "# Generated by vendor-yee-adblock.py. Track matching source-package inputs.\n" +
        "yee_adblock_original_sources = [\n" +
        "".join(f"  {quoted(p)},\n" for p in sorted(originals)) + "]\n")
    return len(manifest)


def vendor(metadata, destination=DEST):
    # Generate and verify the whole graph before replacing any published tree.
    local = ROOT / '.local-build'
    local.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='yee-adblock-stage-', dir=local) as temporary:
        stage = Path(temporary) / 'new'
        count = generate(metadata, stage)
        previous = Path(temporary) / 'previous'
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink():
            raise ValueError(f"Vendor destination must not be a link: {destination}")
        had_previous = destination.exists()
        if had_previous:
            destination.rename(previous)
        try:
            stage.rename(destination)
        except OSError:
            if had_previous:
                previous.rename(destination)
            raise
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('metadata', type=Path)
    args = parser.parse_args()
    count = vendor(json.loads(args.metadata.read_text()))
    print(f"Vendored {count} crates, with independent GN targets -> {DEST}")


if __name__ == '__main__':
    main()
