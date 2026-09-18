#!/usr/bin/env python3
"""Read-only source inventory checks; never builds or launches a browser."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "tools/overlay"
sys.path.insert(0, str(OVERLAY))
SPEC = importlib.util.spec_from_file_location("branding_audit", OVERLAY / "audit-branding.py")
auditor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(auditor)


class BrandingAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="branding-audit-test-")
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name).resolve()
        self.overlay = self.repo / "tools/overlay"
        self.overlay.mkdir(parents=True)
        self.src = self.repo / "chromium/src"
        self.app = self.src / "chrome/app"
        self.app.mkdir(parents=True)
        (self.repo / "branding").mkdir()
        self.config = self.repo / "branding/brand.json"
        self.config.write_text(json.dumps({"name": "Temporary Name", "short_name": "Temp",
                                           "logo_source": "assets/not-created.png",
                                           "logo_crop_size": 820, "provisional": True}))
        self.brand = auditor.load_brand(self.config, self.repo, validate_assets=False)
        for family in ("browser", "renderer", "components"):
            (self.repo / family).mkdir()
        (self.repo / "patches").mkdir()
        (self.repo / "patches/0001-integrate-yee-shell.patch").write_text("")
        self.manifest = self.repo / "branding/surfaces.json"
        self.surface = {"id": "fixture-product-inputs", "status": "managed",
                        "sources": [{"scope": "chromium", "path": "chrome/app/chromium_strings.grd"}],
                        "symbols": ["PRODUCT_FULLNAME"], "configuration_roles": ["name"],
                        "input_check": "product_inputs"}
        self.write_manifest([self.surface])
        for filename, text in auditor.product_parts(self.brand).items():
            (self.app / filename).write_text(text)
        branding = self.app / "theme/chromium/BRANDING"
        branding.parent.mkdir(parents=True)
        branding.write_text("\n".join(f"{key}={value}" for key, value in self.brand.product_values.items()) + "\n")
        self.grd = self.app / "chromium_strings.grd"
        self.grd.write_text('<grit><release><part file="yee_product_names.grdp"/>'
                            '<part file="yee_app_menu_name.grdp"/></release></grit>')

    def write_manifest(self, surfaces):
        self.manifest.write_text(json.dumps({"schema_version": 1,
                                             "string_roots": ["chrome/app/chromium_strings.grd"],
                                             "surfaces": surfaces}))

    def report(self):
        return auditor.audit(self.src, repo_root=self.repo, manifest_path=self.manifest,
                             config_path=self.config)

    def snapshot(self):
        return {path.relative_to(self.repo).as_posix(): (path.read_bytes(), path.stat().st_mtime_ns)
                for path in self.repo.rglob("*") if path.is_file()}

    def test_recursive_parts_all_conditions_exclude_descriptions_and_examples(self):
        self.grd.write_text('<grit><release><if expr="is_macosx"><then>'
                            '<message name="IDS_MAC" desc="Chromium description">Browser only '
                            '<ph name="NAME">$1<ex>Chrome example</ex></ph></message>'
                            '</then><else><message name="IDS_OTHER">Chromium branch</message>'
                            '</else></if><part file="nested.grdp"/></release></grit>')
        (self.app / "nested.grdp").write_text('<grit-part><message name="IDS_PART">Chrome body</message>'
                                               '<part file="deeper.grdp"/></grit-part>')
        (self.app / "deeper.grdp").write_text('<grit-part><message name="IDS_DEEP">chrome://net-export</message></grit-part>')
        report = self.report()["strings"]
        self.assertEqual([c["message_id"] for c in report["candidates"]],
                         ["IDS_OTHER", "IDS_PART", "IDS_DEEP"])
        self.assertEqual(report["candidates"][0]["conditions"], ["if is_macosx", "else"])
        self.assertEqual(report["candidates"][-1]["source"], "chrome/app/deeper.grdp")
        self.assertEqual(report["errors"], [])

    def test_unreviewed_candidates_are_not_covered_by_general_manifest_entry(self):
        self.grd.write_text('<grit><message name="IDS_REVIEWED">Chromium known</message>'
                            '<message name="IDS_NEW">Chromium unknown</message></grit>')
        pending = {**self.surface, "id": "fixture-pending", "status": "pending",
                   "message_ids": ["IDS_REVIEWED"]}
        self.write_manifest([pending])
        report = self.report()
        self.assertEqual([c["review"] for c in report["strings"]["candidates"]],
                         ["pending", "unreviewed"])
        self.assertEqual(report["blockers"]["unreviewed_string_candidates"], 1)
        self.assertFalse(report["complete"])

    def test_shared_conditional_parts_keep_each_context_and_cycles_are_incomplete(self):
        self.grd.write_text('<grit><if expr="is_macosx"><then><part file="shared.grdp"/></then>'
                            '<else><part file="shared.grdp"/></else></if></grit>')
        shared = self.app / "shared.grdp"
        shared.write_text('<grit-part><message name="IDS_SHARED">Chromium shared</message></grit-part>')
        report = self.report()
        self.assertEqual([hit["conditions"] for hit in report["strings"]["candidates"]],
                         [["if is_macosx", "then"], ["if is_macosx", "else"]])
        shared.write_text('<grit-part><part file="chromium_strings.grd"/></grit-part>')
        self.assertTrue(self.report()["strings"]["errors"])

    def test_applied_provisional_product_inputs_are_separate_from_runtime_validation(self):
        report = self.report()
        self.assertEqual(report["brand"]["name"], "Temporary Name")
        self.assertEqual(report["brand"]["short_name"], "Temp")
        self.assertTrue(report["provisional_brand"])
        self.assertTrue(report["product_inputs"]["applied"])
        self.assertEqual(report["surfaces"][0]["input_application"], "applied")
        self.assertEqual(report["surfaces"][0]["runtime_verification"], "not_verified")
        self.assertFalse(report["complete"])

    def test_stale_parts_or_disconnected_grd_do_not_count_as_applied(self):
        for mutate in (lambda: (self.app / "yee_product_names.grdp").write_text("stale"),
                       lambda: self.grd.write_text('<grit/>')):
            for filename, text in auditor.product_parts(self.brand).items():
                (self.app / filename).write_text(text)
            mutate()
            self.assertFalse(self.report()["product_inputs"]["applied"])

    def test_cpp_literals_distinguish_comments_internal_ids_logs_and_display(self):
        text = '// Yee comment\nconst char* path = "chrome/browser/Yee/header.h";\n'
        text += 'LOG(INFO) << "Yee internal diagnostic";\n'
        text += 'SetText(u"YEE Agent needs input");\nSetText("yee:// displayed literal");\n'
        hits, ignored = auditor.scan_cpp_text(text, "fixture.cc")
        self.assertEqual([hit["category"] for hit in hits],
                         ["developer_message", "display_candidate", "display_candidate"])
        self.assertEqual(hits[-1]["line"], 5)
        self.assertEqual(ignored, {"comments": 1, "internal_identifier": 1})

    def test_patch_context_does_not_count_existing_literal_as_new(self):
        patch = self.repo / "patches/0001-integrate-yee-shell.patch"
        patch.write_text('diff --git a/view.cc b/view.cc\n--- a/view.cc\n+++ b/view.cc\n'
                         '@@ -1,2 +1,3 @@\n SetText(u"Yee existing");\n'
                         ' // Yee context comment\n+SetText(u"Yee new");\n')
        hits = self.report()["owned_ui"]["candidates"]
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0]["line"], 7)
        self.assertEqual(hits[0]["chromium_source"], "view.cc")

    def test_missing_malformed_and_escaping_parts_are_reported(self):
        cases = [('missing.grdp', None), ('broken.grdp', '<grit-part>'),
                 ('../../../outside.grdp', '<grit-part/>')]
        for filename, contents in cases:
            with self.subTest(filename=filename):
                self.grd.write_text(f'<grit><part file="{filename}"/></grit>')
                if contents is not None:
                    path = (self.app / filename).resolve()
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(contents)
                report = self.report()
                self.assertEqual(len(report["strings"]["errors"]), 1)
                self.assertFalse(report["complete"])

    def test_cli_default_and_strict_are_read_only_and_never_claim_completion(self):
        for filename in ("audit-branding.py", "brand_config.py"):
            shutil.copy(OVERLAY / filename, self.overlay / filename)
        before = self.snapshot()
        command = [sys.executable, "-B", str(self.overlay / "audit-branding.py"), str(self.src), "--json"]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(json.loads(result.stdout)["complete"])
        result = subprocess.run(command + ["--require-complete"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_preservation_only_manifest_does_not_attest_application_runtime(self):
        self.write_manifest([{**self.surface, "status": "preserved"}])
        report = self.report()
        self.assertFalse(report["complete"])
        self.assertTrue(report["blockers"]["runtime_not_verified"])


if __name__ == "__main__":
    unittest.main()
