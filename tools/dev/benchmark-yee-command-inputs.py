#!/usr/bin/env python3
"""Synthetic command-input ablation; no browser/model calls or winner claim.

Includes session startup and explicit document capabilities. Output, reasoning,
tool framing, retries and repeated context are NOT measured by this script.
The canonical path is identical across variants; it is not accessed.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("meter", ROOT / "measure-browser-transcript.py")
meter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(meter)

DOCUMENT = "01234567-89ab-4cde-8123-456789abcdef"
BASE = ["python3", "tools/dev/yee-browser.py", "--bridge", "/private/tmp/yee-agent.BENCH00"]


def workload(repetitions):
    """Equivalent public command sequences; placeholder refs are never executed."""
    result = [["observe", "--full"]]
    for index in range(repetitions):
        result.extend([["fill", DOCUMENT + "_1", f"Example {index}"],
                       ["click", DOCUMENT + "_2"], ["observe"],
                       ["read", DOCUMENT + "_3"]])
    return result


def compact_args(command):
    if command[0] not in ("read", "click", "fill"):
        return list(command)
    return ["--document", DOCUMENT, command[0], command[1].rsplit("_", 1)[1], *command[2:]]


def variants(commands):
    return {
        "one_shot_native_refs": [shlex.join(BASE + command) for command in commands],
        "one_shot_compact": [shlex.join(BASE + ["--compact"] + compact_args(command))
                             for command in commands],
        "session_compact": [shlex.join(BASE + ["--compact", "session"])] + [
            json.dumps(compact_args(command), ensure_ascii=False, separators=(",", ":"))
            for command in commands],
    }


def measure(tokenizer):
    cases = []
    for repetitions in (0, 1, 5, 20):
        commands = workload(repetitions)
        case = {"commands": len(commands), "variants": {}}
        for name, messages in variants(commands).items():
            case["variants"][name] = {
                "text_tokens": sum(meter._token_count(tokenizer, text) for text in messages),
                "utf8_bytes": sum(len(text.encode("utf-8")) for text in messages),
                "inputs": messages,
            }
        cases.append(case)
    return {
        "schema": "yee.command-input-ablation.v1", "synthetic": True,
        "tokenizer": {"library": "tiktoken", "version": "0.12.0", "encoding": "o200k_base"},
        "scope": "command input strings only; includes session startup and document capability",
        "excludes": ["results", "model reasoning", "tool framing", "context replay", "failures/retries"],
        "browser_executed": False, "provider_usage": None,
        "competitive_verdict": "not_evaluated", "cases": cases,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-inputs", action="store_true", help="include reproducible input corpus")
    args = parser.parse_args()
    report = measure(meter._load_tiktoken("o200k_base"))
    if not args.include_inputs:
        for case in report["cases"]:
            for variant in case["variants"].values():
                variant.pop("inputs")
    print(json.dumps(report, ensure_ascii=False, indent=2))
