#!/usr/bin/env python3
"""Verify the synthetic input ablation uses equivalent native requests."""
import importlib.util
import json
from pathlib import Path
import shlex
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bench = load("bench", "benchmark-yee-command-inputs.py")
cli = load("cli", "yee-browser.py")


class CharacterCounter:
    def encode(self, text, **kwargs):
        return list(text)


class InputAblationTests(unittest.TestCase):
    @patch('time.time', return_value=1700000000.0)
    def test_all_variants_expand_to_same_native_requests(self, _clock):
        commands = bench.workload(2)
        variants = bench.variants(commands)
        parser = cli.build_parser()
        for index, command in enumerate(commands):
            requests = []
            for name in ("one_shot_native_refs", "one_shot_compact", "session_compact"):
                if name == "session_compact":
                    config = parser.parse_args(bench.BASE[2:] + ["--compact", "session"])
                    args = cli.session_command(json.loads(variants[name][index + 1]), config)
                else:
                    argv = shlex.split(variants[name][index])[2:]
                    args = parser.parse_args(argv)
                request = cli.request_for(args,
                                          {"document": bench.DOCUMENT, "revision": 1})
                request.pop("id")
                requests.append(request)
            self.assertEqual(requests[0], requests[1])
            self.assertEqual(requests[0], requests[2])

    def test_startup_is_counted_and_scope_is_not_provider_usage(self):
        report = bench.measure(CharacterCounter())
        self.assertTrue(report["synthetic"])
        self.assertFalse(report["browser_executed"])
        self.assertIsNone(report["provider_usage"])
        self.assertEqual(report["competitive_verdict"], "not_evaluated")
        first = report["cases"][0]["variants"]["session_compact"]
        self.assertEqual(len(first["inputs"]), 2)
        self.assertEqual(first["text_tokens"], sum(map(len, first["inputs"])))


if __name__ == "__main__":
    unittest.main()
