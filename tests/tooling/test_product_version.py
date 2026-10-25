#!/usr/bin/env python3
"""제품 버전의 엔진 호환 범위·입력 검증·반복 적용을 확인한다."""

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("product_version", ROOT / "tools/overlay/product_version.py")
VERSION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERSION)


class ProductVersionTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="yee-version-")
        self.addCleanup(temporary.cleanup)
        self.src = Path(temporary.name) / "src"
        (self.src / "chrome").mkdir(parents=True)
        self.output = self.src / "chrome/VERSION"
        self.original = "MAJOR=153\nMINOR=0\nBUILD=8005\nPATCH=0\n"
        self.output.write_text(self.original, encoding="utf-8")
        self.config = self.src.parent / "VERSION"
        self.config.write_text(self.original.replace("PATCH=0", "PATCH=1"), encoding="utf-8")
        for args in (("init", "-q"), ("add", "."),
                     ("-c", "user.name=Version fixture", "-c", "user.email=fixture@example.invalid",
                      "commit", "-qm", "Engine version")):
            subprocess.run(["git", "-C", str(self.src), *args], check=True, capture_output=True)

    def test_preview_and_repeated_install_preserve_upstream_version(self):
        version, engine = VERSION.version_inputs(self.src, self.config)
        self.assertEqual(VERSION.version_string(version), "153.0.8005.1")
        self.assertEqual(VERSION.version_string(engine), "153.0.8005.0")
        with contextlib.redirect_stdout(io.StringIO()):
            VERSION.install_version(self.src, version, check_only=True)
        self.assertEqual(self.output.read_text(), self.original)
        with contextlib.redirect_stdout(io.StringIO()):
            VERSION.install_version(self.src, version)
        timestamp = self.output.stat().st_mtime_ns
        VERSION.install_version(self.src, version)
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)
        self.assertEqual(VERSION.version_inputs(self.src, self.config), (version, engine))

    def test_engine_rebase_and_older_patch_require_version_update(self):
        for text in (self.original.replace("BUILD=8005", "BUILD=8004"),
                     self.original.replace("MAJOR=153", "MAJOR=154")):
            self.config.write_text(text, encoding="utf-8")
            with self.assertRaises(ValueError):
                VERSION.version_inputs(self.src, self.config)
        self.output.write_text(self.original.replace("PATCH=0", "PATCH=2"), encoding="utf-8")
        subprocess.run(["git", "-C", str(self.src), "add", "."], check=True, capture_output=True)
        subprocess.run(["git", "-C", str(self.src), "-c", "user.name=Version fixture", "-c",
                        "user.email=fixture@example.invalid", "commit", "-qm", "Engine patch"],
                       check=True, capture_output=True)
        self.config.write_text(self.original, encoding="utf-8")
        with self.assertRaises(ValueError):
            VERSION.version_inputs(self.src, self.config)

    def test_malformed_and_non_pe_versions_are_rejected(self):
        for text in (self.original + "PATCH=1\n", self.original.replace("PATCH=0", "PATCH=-1"),
                     self.original.replace("BUILD=8005", "BUILD=65536"),
                     self.original.replace("MINOR=0", "MINOR=00")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                VERSION.parse_version(text)

    def test_link_output_does_not_modify_its_target(self):
        target = self.src.parent / "other-version"
        target.write_text(self.original, encoding="utf-8")
        self.output.unlink()
        try:
            self.output.symlink_to(target)
        except OSError as error:
            self.skipTest(f"심볼릭 링크 권한 없음: {error}")
        with self.assertRaises(ValueError):
            VERSION.install_version(self.src, (153, 0, 8005, 1))
        self.assertEqual(target.read_text(), self.original)


if __name__ == "__main__":
    unittest.main()
