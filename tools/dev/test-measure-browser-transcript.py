#!/usr/bin/env python3
"""Unit tests for measure-browser-transcript.py; no network or tiktoken needed."""

import importlib.util
import contextlib
import io
import json
import pathlib
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("measure", ROOT / "measure-browser-transcript.py")
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


class SimpleTokenizer:
    def encode(self, text):
        return list(text)


def run(**kwargs):
    value = {"run_id": "r1", "harness": "local", "version": "1", "task_id": "t1",
             "model": "test", "viewport": {"width": 800, "height": 600}, "success": True,
             "events": [{"kind": "instruction", "text": "hé"}, {"kind": "tool_call", "text": "go"}],
             "provider_usage": [{"input_tokens": 10, "output_tokens": 4, "cached_input_tokens": 3,
                                  "reasoning_tokens": 1}]}
    value.update(kwargs)
    return value


def comparison(**kwargs):
    value = {"cohort": "checkpoint-1", "provider": "provider-x", "reasoning": "medium",
             "initial_state": "fixture-v1", "approval_policy": "allowlisted", "repetition": 1}
    value.update(kwargs)
    return value


class MeterTests(unittest.TestCase):
    def test_groups_include_failed_cost_and_do_not_mix_models(self):
        attempts = [run(), run(run_id="r2", success=False),
                    run(run_id="r3", model="different", success=False)]
        report = measure.measure_runs(attempts, SimpleTokenizer())
        group, other = report["groups"]
        self.assertEqual(group["attempts"], 2)
        self.assertEqual(group["success_rate"], .5)
        self.assertEqual(group["provider_total_tokens"], 28)
        self.assertEqual(group["provider_total_tokens_per_attempt"], 14)
        self.assertEqual(group["provider_total_tokens_per_success"], 28)
        self.assertIsNone(other["provider_total_tokens_per_success"])
        self.assertEqual(report["per_run"][0]["model"], "test")
        self.assertEqual(report["comparison_verdict"], "not_evaluated")

    def test_unknown_failure_cost_propagates_and_duplicate_ids_reject(self):
        report = measure.measure_runs([run(), run(run_id="r2", success=False,
                                                provider_usage=None)], SimpleTokenizer())
        self.assertIsNone(report["groups"][0]["provider_total_tokens_per_success"])
        with self.assertRaises(measure.TranscriptError):
            measure.measure_runs([run(), run()], SimpleTokenizer())

    def test_bytes_tokens_event_kinds_and_failed_runs(self):
        failed = run(run_id="r2", success=False, provider_usage=None,
                     events=[{"kind": "tool_result", "text": "한"}])
        report = measure.measure_runs([run(), failed], SimpleTokenizer())
        self.assertEqual(report["runs"], 2)
        self.assertEqual(report["successful_runs"], 1)
        self.assertEqual(report["failed_runs"], 1)
        self.assertEqual(report["text"]["bytes"], len("hégo".encode()) + len("한".encode()))
        self.assertEqual(report["text"]["event_kinds"]["instruction"]["tokens"], 2)
        self.assertEqual(report["provider_usage"], {"input_tokens": None, "output_tokens": None,
                                                       "cached_input_tokens": None, "reasoning_tokens": None})
        self.assertEqual(report["per_run"][0]["provider_usage"]["input_tokens"], 10)

    def test_cached_and_reasoning_are_not_double_counted(self):
        report = measure.measure_runs([run()], SimpleTokenizer())
        self.assertEqual(report["provider_usage"], {"input_tokens": 10, "output_tokens": 4,
                                                       "cached_input_tokens": 3, "reasoning_tokens": 1})

    def test_partial_and_empty_provider_usage_stays_unknown_fieldwise(self):
        partial = run(provider_usage=[{"input_tokens": 7}])
        empty = run(run_id="r2", provider_usage=[{}])
        report = measure.measure_runs([partial, empty], SimpleTokenizer())
        self.assertFalse(report["provider_usage_complete"])
        self.assertEqual(report["per_run"][0]["provider_usage"],
                         {"input_tokens": 7, "output_tokens": None,
                          "cached_input_tokens": None, "reasoning_tokens": None})
        self.assertEqual(report["per_run"][1]["provider_usage"],
                         {"input_tokens": None, "output_tokens": None,
                          "cached_input_tokens": None, "reasoning_tokens": None})
        self.assertEqual(report["provider_usage"],
                         {"input_tokens": None, "output_tokens": None,
                          "cached_input_tokens": None, "reasoning_tokens": None})

    def test_rejects_bool_negative_cache_nonfinite_and_bad_event(self):
        for change in (
            {"success": 1},
            {"provider_usage": [{"input_tokens": -1}]},
            {"provider_usage": [{"input_tokens": 2, "cached_input_tokens": 3}]},
            {"viewport": {"width": 0, "height": 600}},
            {"events": [{"kind": "tool_call", "text": 1}]},
        ):
            with self.assertRaises(measure.TranscriptError):
                measure.measure_runs([run(**change)], SimpleTokenizer())

    def test_json_nan_is_rejected_by_parser(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", encoding="utf-8") as file:
            file.write('{"success": NaN}\n')
            file.flush()
            # Exercise the actual CLI parser, not a duplicate json.loads call.
            with mock.patch.object(measure, "_load_tiktoken") as loader:
                with contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(measure.main([file.name]), 2)
                loader.assert_not_called()
                self.assertIn("invalid JSON", errors.getvalue())

    def test_cli_reports_encoding_and_unknown_usage_without_claiming_win(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", encoding="utf-8") as file:
            file.write(json.dumps(run(provider_usage=None)) + "\n")
            file.flush()
            with mock.patch.object(measure, "_load_tiktoken", return_value=SimpleTokenizer()):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(measure.main([file.name]), 0)
            report = json.loads(output.getvalue())
            self.assertEqual(report["text_accounting"]["encoding"], "o200k_base")
            self.assertFalse(report["text_accounting"]["provider_usage_equivalent"])
            self.assertIsNone(report["groups"][0]["provider_total_tokens"])

    def test_matched_checkpoint_audits_baselines_duplicates_mismatches_and_versions(self):
        baseline_runs = [run(run_id=h, harness=h, comparison=comparison())
                         for h in ("yee", "aside")]
        report = measure.measure_runs(baseline_runs, SimpleTokenizer())
        audit = report["comparison_audit"][0]
        self.assertTrue(audit["matched"])
        self.assertEqual(audit["missing_baselines"], [])
        self.assertEqual(report["legacy_unmatched_runs"], [])
        self.assertTrue(all(group["comparison"] for group in report["groups"]))
        changed = run(run_id="r5", harness="yee", version="2",
                      comparison=comparison(provider="other", repetition=1))
        report = measure.measure_runs(baseline_runs + [changed], SimpleTokenizer())
        audit = report["comparison_audit"][0]
        self.assertIn("provider", audit["mismatches"])
        self.assertIn("yee", audit["duplicate_harnesses"])
        self.assertEqual(audit["version_mismatches"]["yee"], ["1", "2"])
        self.assertEqual(report["comparison_verdict"], "not_evaluated")

    def test_legacy_and_malformed_or_differently_configured_records(self):
        legacy = run(run_id="legacy")
        report = measure.measure_runs([legacy], SimpleTokenizer())
        self.assertEqual(report["legacy_unmatched_runs"], ["legacy"])
        self.assertFalse(report["per_run"][0]["has_comparison_metadata"])
        for bad in ({"cohort": "x"},
                    {**comparison(), "repetition": True},
                    {**comparison(), "provider": ""}):
            with self.assertRaises(measure.TranscriptError):
                measure.measure_runs([run(comparison=bad)], SimpleTokenizer())

    def test_version_pin_is_checked_across_repetitions(self):
        first = run(run_id="r1", harness="yee", version="1", comparison=comparison(repetition=1))
        second = run(run_id="r2", harness="yee", version="2", comparison=comparison(repetition=2))
        report = measure.measure_runs([first, second], SimpleTokenizer())
        self.assertEqual(len(report["comparison_audit"]), 2)
        for audit in report["comparison_audit"]:
            self.assertEqual(audit["version_mismatches"]["yee"], ["1", "2"])
            self.assertFalse(audit["matched"])


if __name__ == "__main__":
    unittest.main()
