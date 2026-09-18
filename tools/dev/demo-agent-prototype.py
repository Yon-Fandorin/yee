#!/usr/bin/env python3
"""Run the deterministic local form flow against an already-open Yee tab."""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


CLI = Path(__file__).with_name("yee-browser.py")
REF_LINE = re.compile(r'^[+~]?@([^\s]+)\s+(field|button)\s+"((?:\\.|[^"\\])*)"', re.MULTILINE)


class DemoError(Exception):
    pass


def fail(message, detail=None):
    result = {"ok": False, "error": message}
    if detail:
        result["detail"] = detail
    raise DemoError(json.dumps(result, ensure_ascii=False, separators=(",", ":")))


def run_cli(args, command, *values):
    command_line = [sys.executable, str(CLI), "--bridge", args.bridge, "--timeout", str(args.timeout)]
    command_line.extend([command, *values])
    completed = subprocess.run(command_line, text=True, capture_output=True)
    if completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip()
        fail("CLI command failed: %s" % command, detail)
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        fail("CLI returned malformed JSON: %s" % command, str(exc))


def snapshot_text(response):
    snapshot = response.get("snapshot")
    if not isinstance(snapshot, str):
        fail("response did not contain a snapshot")
    return snapshot


def find_ref(snapshot, role, name):
    matches = []
    for ref, found_role, found_name in REF_LINE.findall(snapshot):
        if found_role == role and found_name == name:
            matches.append(ref)
    if len(matches) != 1:
        fail("expected exactly one %s named %s, found %d" % (role, name, len(matches)))
    return matches[0]


def observation_bytes(response, snapshot):
    value = response.get("observation_bytes")
    if isinstance(value, int) and value >= 0:
        return value
    return len(snapshot.encode("utf-8"))


def progress(message):
    print("[demo] " + message, file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Run the Yee local agent form prototype.")
    parser.add_argument("--bridge", required=True, help="absolute private bridge directory")
    parser.add_argument("--timeout", type=float, default=180.0, help="CLI transport timeout")
    parser.add_argument("--ask", nargs="?", const="Which name should I use?", metavar="QUESTION",
                        help="ask the native agent question before interacting")
    args = parser.parse_args(argv)
    try:
        progress("attaching to the currently active tab (approve in Yee if prompted)")
        run_cli(args, "attach")
        if args.ask is not None:
            progress("asking the native agent (no automatic approval)")
            run_cli(args, "ask", args.ask)
        progress("requesting full observation")
        full_response = run_cli(args, "observe", "--full")
        full_snapshot = snapshot_text(full_response)
        name_ref = find_ref(full_snapshot, "field", "Name")
        save_ref = find_ref(full_snapshot, "button", "Save locally")
        progress("filling discovered Name reference")
        run_cli(args, "fill", name_ref, "Yee prototype")
        progress("clicking discovered Save locally reference")
        run_cli(args, "click", save_ref)
        progress("requesting full observation after save")
        saved_response = run_cli(args, "observe", "--full")
        saved_snapshot = snapshot_text(saved_response)
        if "Saved: Yee prototype" not in saved_snapshot:
            fail("saved result was not observed")
        progress("requesting unchanged observation delta")
        delta_response = run_cli(args, "observe")
        delta_snapshot = snapshot_text(delta_response)
        full_bytes = observation_bytes(saved_response, saved_snapshot)
        delta_bytes = observation_bytes(delta_response, delta_snapshot)
        reduction = full_bytes - delta_bytes
        print(json.dumps({
            "success": True,
            "full_observation_bytes": full_bytes,
            "unchanged_delta_bytes": delta_bytes,
            "byte_reduction": reduction,
            "measurement": "observation_text_bytes_not_tokens",
        }, separators=(",", ":")))
        return 0
    except DemoError as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
