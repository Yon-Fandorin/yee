import importlib.util
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("yee_preprocess", ROOT / "components/content_blocking/preprocess_filters.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
sys.path.insert(0, str(ROOT / "components/content_blocking"))
PACK_SPEC = importlib.util.spec_from_file_location("yee_filter_pack", ROOT / "components/content_blocking/build_filter_pack.py")
PACK = importlib.util.module_from_spec(PACK_SPEC)
PACK_SPEC.loader.exec_module(PACK)
COMMUNITY = ROOT / "components/content_blocking/data/community"
VENDOR = ROOT / "third_party/yee_adblock"


class FilterBranches(unittest.TestCase):
    def test_abp_exception_is_excluded_and_chromium_rule_retained(self):
        text = "!#if ext_abp\n@@||tracker.test^\n!#else\n||tracker.test^\n!#endif\n"
        self.assertEqual(MODULE.preprocess(text), "||tracker.test^\n")

    def test_nested_else_cannot_escape_disabled_parent(self):
        text = "!#if env_firefox\n!#if ext_abp\nwrong-one\n!#else\nwrong-two\n!#endif\n!#else\nright\n!#endif"
        self.assertEqual(MODULE.preprocess(text), "right\n")

    def test_unknown_retains_both_branches_and_negation_selects(self):
        self.assertEqual(MODULE.preprocess("!#if future\none\n!#else\ntwo\n!#endif"), "one\ntwo\n")
        self.assertEqual(MODULE.preprocess("!#if !env_firefox\nright\n!#else\nwrong\n!#endif"), "right\n")

    def test_malformed_branches_fail_build(self):
        for text in ["!#else", "!#endif", "!#if ext_abp", "!#if ",
                     "!#if env_chromium\n!#else\n!#else\n!#endif"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                MODULE.preprocess(text)

    def test_production_tracker_exception_changes_only_abp_branch(self):
        raw = (ROOT / "components/content_blocking/data/easyprivacy.txt").read_text()
        compiled = MODULE.preprocess(raw)
        self.assertIn("@@||bam.nr-data.net^$xmlhttprequest,domain=abema.tv", raw)
        self.assertNotIn("@@||bam.nr-data.net^$xmlhttprequest,domain=abema.tv", compiled)
        self.assertIn("||bam.nr-data.net^", compiled)


class CommunityPack(unittest.TestCase):
    def test_scriptlet_shared_state_contract(self):
        subprocess.run(["node", str(ROOT / "tools/dev/test-scriptlet-runtime.mjs")], check=True)

    def test_original_checksums_and_scriptlet_selection(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "pack"
            report = PACK.build(COMMUNITY, output)
            text = (output / PACK.RULES).read_text()
            self.assertEqual(len(report["sources"]), 15)
            self.assertGreater(report["selected_rules"], 10000)
            self.assertEqual(report["excluded_rules"]["unsupported_scriptlet"], 0)
            self.assertEqual(report["scriptlet_count"], 152)
            self.assertGreater(report["excluded_rules"]["unsupported_redirect"], 0)
            self.assertTrue(any("##+js(" in line for line in text.splitlines()
                                 if not line.lstrip().startswith("!")),
                             "Original scriptlet calls are missing")
            self.assertFalse(any(line.lstrip().startswith("!#include") for line in text.splitlines()))
            self.assertFalse(any(line.lstrip().startswith("!#if") for line in text.splitlines()))
            self.assertTrue("funnyand.com##.ad-unit-desktop" in text)
            self.assertEqual(report["rules_sha256"], hashlib.sha256((output / PACK.RULES).read_bytes()).hexdigest())
            self.assertEqual(report["source_archive_sha256"], hashlib.sha256((output / PACK.ARCHIVE).read_bytes()).hexdigest())

    def test_source_archive_is_whitelisted_and_rebuilds_without_product_code(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data = root / "data"
            shutil.copytree(COMMUNITY, data)
            (data / "private-integration.cc").write_text("PRIVATE_SENTINEL_DO_NOT_PUBLISH")
            first = root / "first"
            PACK.build(data, first)
            extracted = root / "extracted"
            with tarfile.open(first / PACK.ARCHIVE) as archive:
                names = archive.getnames()
                self.assertEqual(len(names), 129)
                self.assertFalse(any(name.endswith((".cc", ".h", ".rs")) for name in names))
                self.assertTrue(all("PRIVATE_SENTINEL_DO_NOT_PUBLISH" not in archive.extractfile(name).read().decode(errors="replace") for name in names))
                archive.extractall(extracted, filter="data")
            second = root / "second"
            subprocess.run([sys.executable, str(extracted / "build_filter_pack.py"),
                            str(extracted / "data"), str(second)], check=True)
            for name in [PACK.RULES, PACK.RESOURCES, PACK.MANIFEST, PACK.NOTICES, PACK.ARCHIVE]:
                self.assertEqual((first / name).read_bytes(), (second / name).read_bytes(), name)

    def test_modified_original_is_rejected_until_manifest_is_updated(self):
        with tempfile.TemporaryDirectory() as temp:
            data = Path(temp) / "data"
            shutil.copytree(COMMUNITY, data)
            manifest = json.loads((data / "sources.json").read_text())
            path = data / manifest["sources"][0]["file"]
            path.write_text(path.read_text() + "\n||user-added.test^\n")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                PACK.build(data, Path(temp) / "pack")
            manifest["sources"][0]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            (data / "sources.json").write_text(json.dumps(manifest))
            PACK.build(data, Path(temp) / "pack")
            self.assertTrue("||user-added.test^" in (Path(temp) / "pack" / PACK.RULES).read_text())

    def test_unpinned_includes_and_path_traversal_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            data = Path(temp) / "data"
            shutil.copytree(COMMUNITY, data)
            manifest = json.loads((data / "sources.json").read_text())
            entry = manifest["sources"][0]
            path = data / entry["file"]
            path.write_text(path.read_text() + "\n!#include unknown.txt\n")
            entry["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            (data / "sources.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "Unpinned include"):
                PACK.build(data, Path(temp) / "pack")
            with self.assertRaisesRegex(ValueError, "Invalid source path"):
                PACK.original(data, "../private.cc", "")

    def test_unsupported_code_and_resources_are_excluded(self):
        redirects = {"noopjs"}
        self.assertIsNone(PACK.supported("||ads.test^$redirect=noopjs:5", redirects))
        self.assertEqual(PACK.supported("||ads.test^$redirect=google-ima.js", redirects), "unsupported_redirect")
        self.assertEqual(PACK.supported("site.test##+js(rpnt, script, arbitrary-code)", redirects), "unsupported_scriptlet")
        self.assertIsNone(PACK.supported("site.test##+js(rpnt, script, source)", redirects, {"rpnt.js"}))
        self.assertIsNone(PACK.supported("site.test#@#+js()", redirects))
        self.assertEqual(PACK.supported("site.test##div:remove()", redirects), "extended_cosmetic")
        self.assertIsNone(PACK.supported("site.test##div:has(.ad)", redirects))
        self.assertEqual(PACK.supported("||site.test^$replace=/one/two/", redirects), "unsupported_network")

    def test_gpl_data_is_not_a_cpp_embedding_input(self):
        gn = (ROOT / "components/content_blocking/BUILD.gn").read_text()
        bundled = gn.split('action("bundled_rules") {', 1)[1].split('\n}', 1)[0]
        self.assertNotIn("community", bundled)
        embedded = (ROOT / "components/content_blocking/embed_rules.py").read_text()
        self.assertNotIn("community", embedded)


class VendoredSourceInputs(unittest.TestCase):
    def test_manifest_sources_are_present_hashed_and_tracked(self):
        manifest = json.loads((VENDOR / "manifest.json").read_text())
        tracked = {
            Path(path.decode())
            for path in subprocess.check_output(
                ["git", "ls-files", "-z", "--", str(VENDOR.relative_to(ROOT))],
                cwd=ROOT,
            ).split(b"\0")
            if path
        }
        missing = []
        for crate in manifest:
            for relative, expected in crate["files"].items():
                source = VENDOR / crate["path"] / relative
                repository_path = source.relative_to(ROOT)
                if not source.is_file() or repository_path not in tracked:
                    missing.append(repository_path.as_posix())
                    continue
                self.assertEqual(
                    hashlib.sha256(source.read_bytes()).hexdigest(),
                    expected,
                    repository_path.as_posix(),
                )
        self.assertEqual([], missing, "Manifest sources must survive a fresh clone")


if __name__ == "__main__":
    unittest.main()
