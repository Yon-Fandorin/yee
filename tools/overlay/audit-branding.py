#!/usr/bin/env python3
"""Inventory branding inputs and source candidates without building or writing."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

from brand_config import REPO_ROOT, load_brand, product_parts


MANIFEST_PATH = REPO_ROOT / "branding/surfaces.json"
BRAND_TOKEN = re.compile(r"\b(?:Chromium(?:OS)?|Chrome(?:OS)?|Google Chrome)\b|\bchrome://")
YEE_TOKEN = re.compile(r"\bYee\b", re.IGNORECASE)
CPP_TOKEN = re.compile(
    r'(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)'
    r'|(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>\w*)\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*")'
)
PRESERVED_LITERAL_IDENTIFIERS = {
    '"disable-yee-shell-scaffold"',
    '"yee-content-blocking-disabled-sites"',
}


def message_text(node):
    """Keep placeholder tokens and tails, excluding examples and descriptions."""
    if node.tag == "ex":
        return ""
    return (node.text or "") + "".join(
        message_text(child) + (child.tail or "") for child in node)


def scan_strings(src, roots):
    candidates, errors, files, visited, active = [], [], [], set(), set()

    def visit(path, conditions=()):
        resolved = path.resolve()
        if not resolved.is_relative_to(src.resolve()):
            errors.append({"source": str(path), "error": "part escapes Chromium source"})
            return
        relative = resolved.relative_to(src.resolve()).as_posix()
        if resolved in active:
            errors.append({"source": relative, "error": "cyclic part reference"})
            return
        context_key = (resolved, conditions)
        if context_key in visited:
            return
        visited.add(context_key)
        if relative not in files:
            files.append(relative)
        try:
            tree = ET.parse(resolved)
        except (OSError, ET.ParseError) as error:
            errors.append({"source": relative, "error": str(error)})
            return

        def walk(node, context):
            if node.tag == "part":
                filename = node.get("file")
                if filename:
                    visit(resolved.parent / filename, context)
                else:
                    errors.append({"source": relative, "error": "part has no file"})
                return
            if node.tag == "message":
                text = re.sub(r"\s+", " ", message_text(node)).strip()
                if BRAND_TOKEN.search(text):
                    candidates.append({"source": relative,
                                       "message_id": node.get("name", ""),
                                       "text": text, "conditions": list(context),
                                       "review": "unreviewed"})
                return
            if node.tag == "if":
                context += ("if " + node.get("expr", "<missing>"),)
            elif node.tag in ("then", "else"):
                context += (node.tag,)
            for child in node:
                walk(child, context)

        active.add(resolved)
        try:
            walk(tree.getroot(), conditions)
        finally:
            active.remove(resolved)

    for root in roots:
        visit(src / root)
    return {"files": files, "candidates": candidates, "errors": errors,
            "scope": "Declared GRD roots and recursive parts; all conditional branches; no XTB/runtime proof."}


def review_strings(candidates, surfaces):
    for candidate in candidates:
        matches = [surface for surface in surfaces
                   if candidate["message_id"] in surface.get("message_ids", [])
                   and any(source["scope"] == "chromium"
                           and source["path"] == candidate["source"]
                           for source in surface["sources"])]
        if matches:
            candidate["surfaces"] = [surface["id"] for surface in matches]
            candidate["review"] = matches[0]["status"]
            candidate["policy"] = matches[0].get("candidate_policy", "Explicit message-level review")
            if matches[0]["id"] == "core-product-name-inputs" and "Chrome for Testing" in candidate["text"]:
                candidate["review"] = "preserved"
                candidate["policy"] = "Separate Chrome for Testing conditional product branding."


def scan_cpp_text(text, source, line_map=None):
    candidates, ignored = [], Counter()
    for match in CPP_TOKEN.finditer(text):
        token = match.group()
        if match.lastgroup == "comment":
            if YEE_TOKEN.search(token):
                ignored["comments"] += 1
            continue
        if not YEE_TOKEN.search(token):
            continue
        line = text.count("\n", 0, match.start()) + 1
        mapped_line = line
        if line_map is not None:
            end_line = line + token.count("\n")
            added_lines = [number for number in line_map[line - 1:end_line] if number is not None]
            if not added_lines:
                continue
            mapped_line = added_lines[0]
        statement_start = max(text.rfind(";", 0, match.start()),
                              text.rfind("{", 0, match.start()),
                              text.rfind("}", 0, match.start())) + 1
        context = text[statement_start:match.end()]
        if (token in PRESERVED_LITERAL_IDENTIFIERS or
                re.search(r"(?:#include|--yee-|yee::|chrome/browser/|\.app/|/MacOS/|yee-agent-bridge|yee-agent-status|yee-signal)", token)):
            category = "internal_identifier"
        elif re.search(r"(?:D?V?LOG\(|TRACE_EVENT|<<)", context):
            category = "developer_message"
        else:
            category = "display_candidate"
        if category == "internal_identifier":
            ignored[category] += 1
            continue
        candidates.append({"source": source, "line": mapped_line,
                           "literal": token, "category": category,
                           "review": "unreviewed"})
    return candidates, ignored


def scan_owned_ui(repo_root):
    candidates, ignored, errors = [], Counter(), []
    for family in ('browser', 'renderer', 'components'):
        source_root = repo_root / family
        for path in sorted(source_root.rglob('*')):
            if path.suffix not in ('.cc', '.h', '.mm') or 'test' in path.name:
                continue
            hits, counts = scan_cpp_text(path.read_text(encoding='utf-8'),
                                        path.relative_to(repo_root).as_posix())
            candidates.extend(hits)
            ignored.update(counts)
        if not source_root.is_dir():
            errors.append({'source': str(source_root), 'error': 'owned source directory missing'})
    patch = repo_root / "patches/0001-integrate-yee-shell.patch"
    if patch.is_file():
        chunks, source, lines, numbers = [], None, [], []
        for number, line in enumerate(patch.read_text(encoding="utf-8").splitlines(), 1):
            if line.startswith("diff --git "):
                if source:
                    chunks.append((source, lines, numbers))
                source = line.split(" b/", 1)[-1]
                lines, numbers = [], []
            elif source and line.startswith("+") and not line.startswith("+++"):
                lines.append(line[1:])
                numbers.append(number)
            elif source and line.startswith(" "):
                # Keep context so comments and multiline literals are lexed correctly.
                lines.append(line[1:])
                numbers.append(None)
            elif source and line.startswith("@@"):
                lines.append("")
                numbers.append(None)
        if source:
            chunks.append((source, lines, numbers))
        for source, lines, numbers in chunks:
            if Path(source).suffix not in (".cc", ".h", ".mm") or "test" in Path(source).name:
                continue
            hits, counts = scan_cpp_text("\n".join(lines),
                                        patch.relative_to(repo_root).as_posix(), numbers)
            for hit in hits:
                hit["chromium_source"] = source
            candidates.extend(hits)
            ignored.update(counts)
    else:
        errors.append({"source": str(patch), "error": "shell patch missing"})
    return {"candidates": candidates, "ignored": dict(ignored), "errors": errors,
            "scope": "C++ string-literal heuristic in owned native UI and added shell-patch lines; excludes tests/prototypes; not a semantic UI analysis."}


def check_product_inputs(src, brand):
    expected = brand.product_values
    path = src / "chrome/app/theme/chromium/BRANDING"
    try:
        text = path.read_text(encoding="utf-8")
        observed = {key: re.findall(rf"(?m)^{key}=([^\r\n]*)", text) for key in expected}
        metadata_matches = all(observed[key] == [value] for key, value in expected.items())
    except OSError:
        observed, metadata_matches = {}, False
    parts = {}
    for filename, expected_text in product_parts(brand).items():
        path = src / "chrome/app" / filename
        parts[filename] = path.is_file() and path.read_bytes() == expected_text.encode("utf-8")
    try:
        grd = ET.parse(src / "chrome/app/chromium_strings.grd")
        connected = {node.get("file") for node in grd.iter("part")}
    except (OSError, ET.ParseError):
        connected = set()
    connections = {filename: filename in connected for filename in parts}
    return {"branding_metadata_matches": metadata_matches,
            "observed_product_fields": observed, "generated_parts_match": parts,
            "grd_connections_present": connections,
            "applied": metadata_matches and all(parts.values()) and all(connections.values()),
            "runtime_verification": "not_verified"}


def audit(chromium_src, *, repo_root=REPO_ROOT, manifest_path=MANIFEST_PATH, config_path=None):
    src, repo_root = Path(chromium_src).resolve(), Path(repo_root).resolve()
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    brand = load_brand(config_path or repo_root / "branding/brand.json",
                       repo_root, validate_assets=False)
    surfaces = []
    for entry in manifest["surfaces"]:
        if entry["status"] not in ("managed", "pending", "preserved", "not_verified"):
            raise ValueError("Unknown surface status: " + entry["id"])
        surface = dict(entry)
        surface["source_presence"] = [{**source, "present":
                                       ((src if source["scope"] == "chromium" else repo_root)
                                        / source["path"]).is_file()}
                                      for source in entry["sources"]]
        surfaces.append(surface)
    strings = scan_strings(src, manifest["string_roots"])
    review_strings(strings["candidates"], surfaces)
    owned_ui = scan_owned_ui(repo_root)
    inputs = check_product_inputs(src, brand)
    for surface in surfaces:
        if surface.get("input_check") == "product_inputs":
            surface["input_application"] = "applied" if inputs["applied"] else "not_applied"
        else:
            surface["input_application"] = "not_checked"
        # Reading source cannot attest that a built application uses it.
        surface["runtime_verification"] = "not_verified"
    missing = [{"surface": surface["id"], **source}
               for surface in surfaces for source in surface["source_presence"]
               if not source["present"]]
    incomplete = [surface["id"] for surface in surfaces
                  if surface["status"] in ("pending", "not_verified")
                  or (surface["status"] == "managed"
                      and surface["runtime_verification"] != "verified")]
    unreviewed = sum(candidate["review"] == "unreviewed" for candidate in strings["candidates"])
    fixed_display = sum(candidate["category"] == "display_candidate"
                        for candidate in owned_ui["candidates"])
    blockers = {"incomplete_surfaces": incomplete, "missing_sources": missing,
                "unreviewed_string_candidates": unreviewed,
                "fixed_working_name_display_candidates": fixed_display,
                "scan_errors": strings["errors"] + owned_ui["errors"],
                "product_inputs_not_applied": not inputs["applied"],
                "provisional_brand": brand.provisional,
                "runtime_not_verified": True}
    return {"schema_version": 1, "chromium_src": str(src), "brand": brand.metadata(),
            "provisional_brand": brand.provisional,
            "complete": not any(blockers.values()), "runtime_verification": "not_verified",
            "notice": "Read-only source inventory. It neither proves all branding surfaces are inventoried nor validates a built application.",
            "product_inputs": inputs, "surfaces": surfaces, "strings": strings,
            "owned_ui": owned_ui, "blockers": blockers}


def render_text(report):
    print(f"Configured name: {report['brand']['name']} (provisional={report['provisional_brand']})")
    print(f"Product inputs applied: {report['product_inputs']['applied']}; application runtime: not_verified")
    print(report["notice"])
    print("Surface status: " + ", ".join(f"{key}={value}" for key, value in
                                        sorted(Counter(s["status"] for s in report["surfaces"]).items())))
    print(f"String candidates: {len(report['strings']['candidates'])}; unreviewed: {report['blockers']['unreviewed_string_candidates']}")
    unreviewed = [candidate for candidate in report["strings"]["candidates"]
                  if candidate["review"] == "unreviewed"]
    for candidate in unreviewed[:12]:
        print(f"  unreviewed: {candidate['source']} {candidate['message_id']}")
    if len(unreviewed) > 12:
        print(f"  {len(unreviewed) - 12} more unreviewed occurrences; --json includes every candidate and conditional context.")
    for surface in report["surfaces"]:
        print(f"  {surface['status']}: {surface['id']} [inputs={surface['input_application']}; runtime=not_verified]")
    for hit in report["owned_ui"]["candidates"]:
        print(f"  {hit['category']}: {hit['source']}:{hit['line']} {hit['literal']}")
    for source in report["blockers"]["missing_sources"]:
        print(f"  missing: {source['scope']}/{source['path']}")
    for error in report["blockers"]["scan_errors"]:
        print(f"  scan error: {error['source']}: {error['error']}")
    print("Complete: false (source inventory cannot attest runtime completion)" if not report["complete"] else "Complete: true")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("chromium_src", type=Path)
    parser.add_argument("--json", action="store_true", help="print the complete source inventory as JSON")
    parser.add_argument("--require-complete", action="store_true", help="fail on pending, missing, unreviewed or runtime-unverified surfaces")
    args = parser.parse_args(argv)
    if not args.chromium_src.is_absolute() or not args.chromium_src.is_dir():
        parser.error("Chromium source must be an existing absolute directory")
    try:
        report = audit(args.chromium_src)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        render_text(report)
    return 1 if args.require_complete and not report["complete"] else 0


if __name__ == "__main__":
    sys.exit(main())
