#!/usr/bin/env python3
"""Brand rename and input-generation checks. Never builds or launches Chromium."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("brand_config", ROOT / "tools/overlay/brand_config.py")
branding = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = branding
SPEC.loader.exec_module(branding)
UPSTREAM_BRANDING = """COMPANY_FULLNAME=The Chromium Authors
COMPANY_SHORTNAME=The Chromium Authors
PRODUCT_FULLNAME=Chromium
PRODUCT_SHORTNAME=Chromium
PRODUCT_INSTALLER_FULLNAME=Chromium Installer
PRODUCT_INSTALLER_SHORTNAME=Chromium Installer
COPYRIGHT=Copyright @LASTCHANGE_YEAR@ The Chromium Authors. All rights reserved.
MAC_BUNDLE_ID=org.chromium.Chromium
MAC_CREATOR_CODE=Cr24
MAC_TEAM_ID=existing-team
"""


class BrandConfigTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="yee-brand-test-")
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.logo = self.repo / "assets/brand/logo.png"
        self.logo.parent.mkdir(parents=True)
        self.logo.write_bytes(b"logo fixture; never rendered")
        self.config = self.repo / "branding/brand.json"
        self.config.parent.mkdir()
        self.src = self.repo / "chromium/src"
        self.branding_path = self.src / "chrome/app/theme/chromium/BRANDING"
        self.branding_path.parent.mkdir(parents=True)
        self.branding_path.write_text(branding.BRANDING_MARKER + "\n" + UPSTREAM_BRANDING)

    def brand(self, name="Orbit & Co", **extra):
        self.config.write_text(json.dumps({"name": name, "logo_source": "assets/brand/logo.png",
                                          "logo_crop_size": 820, "internal_url_scheme": "orbit", **extra}))
        return branding.load_brand(self.config, self.repo)

    def install(self, brand, check_only=False):
        with contextlib.redirect_stdout(io.StringIO()):
            branding.install_brand(self.src, brand, check_only)

    def test_rename_preserves_identity_and_extra_upstream_fields(self):
        self.install(self.brand())
        self.install(self.brand("새 이름", short_name="새이름"))
        text = self.branding_path.read_text()
        self.assertIn("PRODUCT_FULLNAME=새 이름\n", text)
        self.assertIn("PRODUCT_SHORTNAME=새이름\n", text)
        self.assertIn("PRODUCT_INSTALLER_FULLNAME=새 이름 Installer\n", text)
        for key in ("COMPANY_FULLNAME", "COMPANY_SHORTNAME", "COPYRIGHT", "MAC_BUNDLE_ID", "MAC_CREATOR_CODE", "MAC_TEAM_ID"):
            original = next(line for line in UPSTREAM_BRANDING.splitlines() if line.startswith(key + "="))
            self.assertIn(original + "\n", text)

    def test_url_scheme_follows_brand_rename_and_can_be_overridden(self):
        for name, expected in (("Orbit", "orbit"), ("Nova", "nova")):
            self.config.write_text(json.dumps({"name": name, "logo_source": "assets/brand/logo.png",
                                              "logo_crop_size": 820}))
            brand = branding.load_brand(self.config, self.repo)
            self.assertEqual(brand.internal_url_scheme, expected)
            self.install(brand)
            text = self.branding_path.read_text()
            self.assertIn(f"PRODUCT_INTERNAL_URL_SCHEME={expected}\n", text)
            self.assertEqual(text.count("PRODUCT_INTERNAL_URL_SCHEME="), 1)
        self.assertEqual(self.brand("새 이름", internal_url_scheme="newbrand").internal_url_scheme,
                         "newbrand")

    def test_url_scheme_rejects_invalid_and_reserved_protocols(self):
        for scheme in ("", "Orbit", "123brand", "two words", "새이름", "http", "https",
                       "chrome", "chrome-untrusted", "file", "javascript", "data",
                       "devtools", "view-source", "x\nINJECT=1", None):
            with self.subTest(scheme=scheme), self.assertRaises(ValueError):
                self.brand(internal_url_scheme=scheme)
        for scheme in ("orbit", "new-brand", "brand.v2", "brand+local"):
            self.assertEqual(self.brand(internal_url_scheme=scheme).internal_url_scheme, scheme)

    def test_scheme_with_duplicate_branding_input_fails_before_writes(self):
        self.branding_path.write_text(self.branding_path.read_text()
            + "PRODUCT_INTERNAL_URL_SCHEME=first\nPRODUCT_INTERNAL_URL_SCHEME=second\n")
        before = self.branding_path.read_bytes()
        with self.assertRaises(ValueError):
            self.install(self.brand())
        self.assertEqual(self.branding_path.read_bytes(), before)

    def test_xml_names_and_untranslated_product_ids(self):
        self.install(self.brand(short_name="Orbit"))
        names = {}
        for path in self.src.joinpath("chrome/app").glob("yee_*.grdp"):
            for node in ET.parse(path).getroot().iter("message"):
                self.assertEqual(node.attrib["translateable"], "false")
                names[node.attrib["name"]] = node.text
        self.assertEqual(names, {"IDS_PRODUCT_NAME": "Orbit & Co",
                                 "IDS_SHORT_PRODUCT_NAME": "Orbit",
                                 "IDS_APP_MENU_PRODUCT_NAME": "Orbit"})

    def test_reapplying_identical_configuration_preserves_timestamps(self):
        brand = self.brand()
        self.install(brand)
        files = [self.branding_path, *self.src.joinpath("chrome/app").glob("yee_*.grdp")]
        before = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in files}
        self.install(brand)
        self.assertEqual(before, {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in files})

    def test_preview_does_not_modify_files(self):
        self.branding_path.write_text(UPSTREAM_BRANDING)
        before = self.branding_path.read_bytes()
        self.install(self.brand(), check_only=True)
        self.assertEqual(self.branding_path.read_bytes(), before)
        self.assertEqual(list(self.src.joinpath("chrome/app").glob("yee_*.grdp")), [])

    def test_missing_marker_or_duplicate_key_refuses_before_writes(self):
        for text in (UPSTREAM_BRANDING, branding.BRANDING_MARKER + "\n" + UPSTREAM_BRANDING + "PRODUCT_SHORTNAME=duplicate\n"):
            self.branding_path.write_text(text)
            before = self.branding_path.read_bytes()
            with self.assertRaises(ValueError):
                self.install(self.brand())
            self.assertEqual(self.branding_path.read_bytes(), before)
            self.assertEqual(list(self.src.joinpath("chrome/app").glob("yee_*.grdp")), [])

    def test_invalid_names_and_logo_paths_are_rejected(self):
        for name in ("", "bad/name", "bad\nname", " Name", "Name.", "Name $VALUE", "Name @PATCH@"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.brand(name)
        with self.assertRaises(ValueError):
            self.brand(logo_source="../outside.png")

    @unittest.skipUnless(shutil.which("zsh"), "zsh is required for macOS tool-path checks")
    def test_macos_runtime_paths_follow_configured_name_without_shell_evaluation(self):
        name = "Orbit `literal`"
        self.brand(name)
        overlay_tools = self.repo / "tools/overlay"
        overlay_tools.mkdir(parents=True)
        shutil.copy(ROOT / "tools/overlay/brand_config.py", overlay_tools)
        common = self.repo / "tools/dev/common.zsh"
        common.parent.mkdir(parents=True)
        shutil.copy(ROOT / "tools/dev/common.zsh", common)
        shutil.copytree(ROOT / "tools/dev/lib", common.parent / "lib")
        output = subprocess.check_output(["zsh", "-c", 'source "$1"; print -rl -- "$YEE_PRODUCT_NAME" "$YEE_BROWSER_BIN"', "brand-test", str(common)], text=True)
        expected_src = self.repo.resolve() / ".local-build/chromium/src"
        self.assertEqual(output.splitlines(), [name, str(expected_src / f"out/YeePilot/{name}.app/Contents/MacOS/{name}")])

    def test_name_is_provisional_by_default_and_can_be_marked_final(self):
        self.assertTrue(self.brand().provisional)
        self.assertFalse(self.brand(provisional=False).provisional)
        with self.assertRaises(ValueError):
            self.brand(provisional="yes")

    def test_reading_product_name_does_not_require_rendering_assets(self):
        self.brand()
        self.logo.unlink()
        self.assertEqual(branding.load_brand(self.config, self.repo, validate_assets=False).name,
                         "Orbit & Co")
        with self.assertRaises(ValueError):
            branding.load_brand(self.config, self.repo)

    def test_legacy_comment_marker_is_removed_for_version_compatibility(self):
        self.branding_path.write_text(branding.LEGACY_BRANDING_MARKER + "\n"
                                     + branding.BRANDING_MARKER + "\n" + UPSTREAM_BRANDING)
        self.install(self.brand())
        self.assertNotIn(branding.LEGACY_BRANDING_MARKER, self.branding_path.read_text())
        self.assertTrue(all("=" in line for line in self.branding_path.read_text().splitlines()))

    def test_unmanaged_non_version_lines_fail_before_writes(self):
        self.branding_path.write_text(branding.BRANDING_MARKER + "\n" + UPSTREAM_BRANDING
                                     + "# Unsupported upstream comment\n")
        before = self.branding_path.read_bytes()
        with self.assertRaises(ValueError):
            self.install(self.brand())
        self.assertEqual(self.branding_path.read_bytes(), before)

    @unittest.skipUnless((ROOT / ".local-build/chromium/src/build/util/version.py").is_file(),
                         "local Chromium version utility is required")
    def test_installed_branding_is_readable_by_actual_chromium_version_generator(self):
        name = "O'Reilly & 새 이름"
        self.install(self.brand(name))
        env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
        output = subprocess.check_output([
            sys.executable, str(ROOT / ".local-build/chromium/src/build/util/version.py"),
            "-f", str(self.branding_path), "-t", "@PRODUCT_FULLNAME@"],
            text=True, env=env)
        self.assertEqual(output.rstrip("\n"), name)

    @unittest.skipUnless((ROOT / ".local-build/chromium/src/build/util/version.py").is_file(),
                         "local Chromium version utility is required")
    def test_actual_generator_updates_cpp_url_scheme_from_brand_input(self):
        for name in ("Orbit", "Nova"):
            self.config.write_text(json.dumps({"name": name, "logo_source": "assets/brand/logo.png",
                                              "logo_crop_size": 820}))
            self.install(branding.load_brand(self.config, self.repo))
            output = subprocess.check_output([
                sys.executable, str(ROOT / ".local-build/chromium/src/build/util/version.py"),
                "-f", str(self.branding_path),
                "-i", str(ROOT / "components/branding/internal_url_scheme.h.in")],
                text=True, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
            self.assertIn(f'kInternalURLScheme[] = "{name.lower()}";', output)
            self.assertNotIn("@PRODUCT_INTERNAL_URL_SCHEME@", output)


if __name__ == "__main__":
    unittest.main()
