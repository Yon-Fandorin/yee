#!/usr/bin/env python3
"""Measure browser-agent JSONL transcripts without making provider claims.

The local ledger counts UTF-8 bytes and a pinned tiktoken encoding.  Provider
usage is a separate ledger: absent usage is ``null`` (never zero), and cached
input/reasoning fields are reported without being added a second time.
"""

import argparse
import contextlib
import importlib.metadata
import json
import math
import pathlib
import sys


REQUIRED_RUN = {"run_id", "harness", "version", "task_id", "model", "viewport", "success", "events"}
OPTIONAL_RUN = {"provider_usage", "comparison"}
COMPARISON_FIELDS = ("cohort", "provider", "reasoning", "initial_state", "approval_policy", "repetition")
COMPARISON_STRING_FIELDS = ("cohort", "provider", "reasoning", "initial_state", "approval_policy")
EXPECTED_BASELINES = ("yee", "aside")
VIEWPORT_FIELDS = {"width", "height", "device_scale_factor"}
EVENT_KINDS = ("instruction", "tool_call", "tool_result", "assistant")
USAGE_FIELDS = ("input_tokens", "output_tokens", "cached_input_tokens", "reasoning_tokens")


class TranscriptError(ValueError):
    """Raised when a transcript does not conform to the measurement schema."""


def _integer(value, name, *, minimum=0):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise TranscriptError(f"{name} must be an integer >= {minimum}")
    return value


def _finite(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise TranscriptError(f"{name} must be finite")
    return value


def validate_run(run, line_number=None):
    where = f"line {line_number}: " if line_number else ""
    if not isinstance(run, dict):
        raise TranscriptError(where + "run must be an object")
    unknown = set(run) - REQUIRED_RUN - OPTIONAL_RUN
    if unknown:
        raise TranscriptError(where + "unknown field(s): " + ", ".join(sorted(unknown)))
    missing = REQUIRED_RUN - set(run)
    if missing:
        raise TranscriptError(where + "missing required field(s): " + ", ".join(sorted(missing)))
    for field in ("run_id", "harness", "version", "task_id", "model"):
        if not isinstance(run[field], str) or not run[field]:
            raise TranscriptError(where + f"{field} must be a non-empty string")
    viewport = run["viewport"]
    if not isinstance(viewport, dict):
        raise TranscriptError(where + "viewport must be an object")
    unknown_viewport = set(viewport) - VIEWPORT_FIELDS
    if unknown_viewport:
        raise TranscriptError(where + "unknown viewport field(s): " + ", ".join(sorted(unknown_viewport)))
    for field in ("width", "height"):
        _integer(viewport.get(field), f"viewport.{field}", minimum=1)
    if "device_scale_factor" in viewport:
        _finite(viewport["device_scale_factor"], "viewport.device_scale_factor")
        if viewport["device_scale_factor"] <= 0:
            raise TranscriptError(where + "viewport.device_scale_factor must be > 0")
    if not isinstance(run["success"], bool):
        raise TranscriptError(where + "success must be a boolean")
    if "comparison" in run:
        comparison = run["comparison"]
        if not isinstance(comparison, dict) or set(comparison) != set(COMPARISON_FIELDS):
            raise TranscriptError(where + "comparison must contain exactly: " + ", ".join(COMPARISON_FIELDS))
        for field in COMPARISON_STRING_FIELDS:
            if not isinstance(comparison[field], str) or not comparison[field]:
                raise TranscriptError(where + f"comparison.{field} must be a non-empty string")
        _integer(comparison["repetition"], "comparison.repetition", minimum=1)
    events = run["events"]
    if not isinstance(events, list):
        raise TranscriptError(where + "events must be an array")
    for index, event in enumerate(events):
        if not isinstance(event, dict) or set(event) != {"kind", "text"}:
            raise TranscriptError(where + f"events[{index}] must contain only kind and text")
        if not isinstance(event["kind"], str) or event["kind"] not in EVENT_KINDS:
            raise TranscriptError(where + f"events[{index}].kind is invalid")
        if not isinstance(event["text"], str):
            raise TranscriptError(where + f"events[{index}].text must be a string")
    if "provider_usage" in run and run["provider_usage"] is not None:
        usage = run["provider_usage"]
        if not isinstance(usage, list) or not usage:
            raise TranscriptError(where + "provider_usage must be null or a non-empty array")
        for index, item in enumerate(usage):
            if not isinstance(item, dict) or not set(item).issubset(USAGE_FIELDS):
                raise TranscriptError(where + f"provider_usage[{index}] has invalid fields")
            for field, value in item.items():
                _integer(value, f"provider_usage[{index}].{field}")
            if "cached_input_tokens" in item and "input_tokens" in item:
                if item["cached_input_tokens"] > item["input_tokens"]:
                    raise TranscriptError(where + f"provider_usage[{index}].cached_input_tokens exceeds input_tokens")
    return run


def _token_count(tokenizer, text):
    # tiktoken's default rejects strings containing special-token spellings;
    # transcript text is untrusted text, not a request to interpret tokens.
    try:
        encoded = tokenizer.encode(text, disallowed_special=())
    except TypeError:
        # Keep the injected test tokenizer (and compatible implementations)
        # simple while retaining the tiktoken safety behavior above.
        encoded = tokenizer.encode(text)
    if not isinstance(encoded, (list, tuple)):
        raise TranscriptError("tokenizer.encode must return a sequence")
    return len(encoded)


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _comparison_audit(runs):
    buckets = {}
    cohort_harness_versions = {}
    for run in runs:
        comparison = run.get("comparison")
        if comparison is None:
            continue
        key = (comparison["cohort"], run["task_id"], comparison["repetition"])
        buckets.setdefault(key, []).append(run)
        cohort_harness_versions.setdefault((comparison["cohort"], run["harness"]), set()).add(run["version"])
    audits = []
    fields = ("provider", "reasoning", "initial_state", "approval_policy", "model", "viewport")
    for (cohort, task_id, repetition), members in sorted(buckets.items(), key=lambda item: item[0]):
        mismatches = {}
        for field in fields:
            values = []
            for run in members:
                value = run["viewport"] if field == "viewport" else (
                    run["comparison"][field] if field in COMPARISON_STRING_FIELDS else run[field])
                encoded = _canonical(value)
                if encoded not in {seen for seen, _ in values}:
                    values.append((encoded, value))
            if len(values) > 1:
                mismatches[field] = [value for _, value in values]
        harness_counts = {}
        versions = {}
        for run in members:
            harness = run["harness"]
            harness_counts[harness] = harness_counts.get(harness, 0) + 1
            versions.setdefault(harness, set()).update(
                cohort_harness_versions[(cohort, harness)])
        duplicate_harnesses = sorted(harness for harness, count in harness_counts.items() if count > 1)
        version_mismatches = {harness: sorted(values) for harness, values in versions.items() if len(values) > 1}
        missing = sorted(set(EXPECTED_BASELINES) - set(harness_counts))
        unexpected = sorted(set(harness_counts) - set(EXPECTED_BASELINES))
        audits.append({"cohort": cohort, "task_id": task_id, "repetition": repetition,
                       "mismatches": mismatches, "missing_baselines": missing,
                       "unexpected_harnesses": unexpected,
                       "duplicate_harnesses": duplicate_harnesses,
                       "version_mismatches": version_mismatches,
                       "matched": not mismatches and not missing and not unexpected
                       and not duplicate_harnesses and not version_mismatches})
    return audits


def measure_runs(runs, tokenizer):
    """Return a JSON-serializable report. ``tokenizer`` is injected for tests."""
    if not isinstance(runs, list):
        raise TranscriptError("input must contain a JSON object per line")
    totals = {"bytes": 0, "tokens": 0}
    event_totals = {kind: {"bytes": 0, "tokens": 0} for kind in EVENT_KINDS}
    provider = {field: 0 for field in USAGE_FIELDS}
    provider_known = {field: bool(runs) for field in USAGE_FIELDS}
    measured = []
    seen_ids = set()
    groups = {}
    for index, run in enumerate(runs, 1):
        validate_run(run, index)
        if run["run_id"] in seen_ids:
            raise TranscriptError(f"duplicate run_id: {run['run_id']}")
        seen_ids.add(run["run_id"])
        local = {"bytes": 0, "tokens": 0}
        kinds = {kind: {"bytes": 0, "tokens": 0} for kind in EVENT_KINDS}
        for event in run["events"]:
            text = event["text"]
            byte_count = len(text.encode("utf-8"))
            token_count = _token_count(tokenizer, text)
            local["bytes"] += byte_count
            local["tokens"] += token_count
            kinds[event["kind"]]["bytes"] += byte_count
            kinds[event["kind"]]["tokens"] += token_count
            event_totals[event["kind"]]["bytes"] += byte_count
            event_totals[event["kind"]]["tokens"] += token_count
        totals["bytes"] += local["bytes"]
        totals["tokens"] += local["tokens"]
        usage = run.get("provider_usage")
        if usage is None:
            for field in USAGE_FIELDS:
                provider_known[field] = False
            run_provider = None
        else:
            run_provider = {}
            for field in USAGE_FIELDS:
                if all(field in item for item in usage):
                    run_provider[field] = sum(item[field] for item in usage)
                    provider[field] += run_provider[field]
                else:
                    run_provider[field] = None
                    provider_known[field] = False
        identity = {field: run[field] for field in ("harness", "version", "task_id", "model", "viewport")}
        identity["comparison"] = run.get("comparison")
        measured.append({"run_id": run["run_id"], **identity, "has_comparison_metadata": "comparison" in run,
                         "success": run["success"], "text": local,
                         "event_kinds": kinds,
                         "provider_usage": run_provider})
        key = json.dumps(identity, sort_keys=True)
        group = groups.setdefault(key, {
            **identity, "attempts": 0, "successes": 0, "text_tokens": 0,
            "provider_total_tokens": 0,
        })
        group["attempts"] += 1
        group["successes"] += int(run["success"])
        group["text_tokens"] += local["tokens"]
        # Input includes cached tokens; output includes reasoning. Do not add
        # either detail field again. Unknown totals propagate, even on failures.
        if (run_provider is None or run_provider["input_tokens"] is None
                or run_provider["output_tokens"] is None):
            group["provider_total_tokens"] = None
        elif group["provider_total_tokens"] is not None:
            group["provider_total_tokens"] += run_provider["input_tokens"] + run_provider["output_tokens"]
    for group in groups.values():
        group["success_rate"] = group["successes"] / group["attempts"]
        for ledger in ("text_tokens", "provider_total_tokens"):
            total = group[ledger]
            group[ledger + "_per_attempt"] = None if total is None else total / group["attempts"]
            group[ledger + "_per_success"] = (
                None if total is None or not group["successes"] else total / group["successes"])
    provider_complete = bool(runs) and all(provider_known.values())
    provider = {field: provider[field] if provider_known[field] else None for field in USAGE_FIELDS}
    return {
        "schema": "yee.browser-transcript-meter.v1",
        "runs": len(runs),
        "successful_runs": sum(bool(run["success"]) for run in runs),
        "failed_runs": sum(not run["success"] for run in runs),
        "text": {"bytes": totals["bytes"], "tokens": totals["tokens"], "event_kinds": event_totals},
        "provider_usage_complete": provider_complete,
        # Keep known fields useful while representing every missing field as
        # null; completeness is reported separately for consumers that need a
        # fully billed comparison.
        "provider_usage": provider if runs else None,
        "provider_usage_scope": "reported entries only; all-call coverage unverified",
        "per_run": measured,
        "groups": list(groups.values()),
        "comparison_audit": _comparison_audit(runs),
        "legacy_unmatched_runs": [run["run_id"] for run in runs if "comparison" not in run],
        "comparison_verdict": "not_evaluated",
    }


def _load_tiktoken(encoding):
    try:
        import tiktoken
    except ImportError as exc:
        raise TranscriptError("tiktoken is required; use .local-build/agent-bench-venv") from exc
    try:
        installed_version = importlib.metadata.version("tiktoken")
    except importlib.metadata.PackageNotFoundError:
        installed_version = getattr(tiktoken, "__version__", "unknown")
    if installed_version != "0.12.0":
        raise TranscriptError(f"tiktoken 0.12.0 required, found {installed_version}")
    try:
        return tiktoken.get_encoding(encoding)
    except Exception as exc:
        raise TranscriptError(f"unable to load tiktoken encoding {encoding}: {exc}") from exc


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="JSONL path, or - for stdin")
    parser.add_argument("--encoding", default="o200k_base", help="tiktoken encoding (default: o200k_base)")
    args = parser.parse_args(argv)
    try:
        runs = []
        source_context = (contextlib.nullcontext(sys.stdin) if args.input == "-"
                          else pathlib.Path(args.input).open(encoding="utf-8"))
        with source_context as source:
            for number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                try:
                    runs.append(json.loads(line, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value))))
                except (json.JSONDecodeError, ValueError) as exc:
                    raise TranscriptError(f"line {number}: invalid JSON ({exc})") from exc
        tokenizer = _load_tiktoken(args.encoding)
        report = measure_runs(runs, tokenizer)
        report["text_accounting"] = {
            "library": "tiktoken", "version": "0.12.0", "encoding": args.encoding,
            "scope": "sum of event text encoded independently; excludes chat framing and context replay",
            "provider_usage_equivalent": False,
        }
        print(json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2))
        return 0
    except (OSError, TranscriptError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
