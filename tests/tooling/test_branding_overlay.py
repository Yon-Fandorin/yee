#!/usr/bin/env python3
"""Exercise real branding patches and version templates in disposable checkouts.

Never compiles, builds, launches, or modifies the user's Chromium checkout.
"""

import ast
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / ".local-build/chromium/src"
PATCH_NAME = "0002-brand-yee-application.patch"
INPUT_PATHS = (
    "chrome/app/theme/chromium/BRANDING",
    "chrome/app/chromium_strings.grd",
    "chrome/installer/mac/signing/build_props_config.py.in",
    "base/win/embedded_i18n/create_string_rc.py",
    "base/win/embedded_i18n/generate_embedded_i18n.gni",
    "chrome/installer/util/BUILD.gn",
    "build/util/version.py",
    "chrome/process_version_rc_template.gni",
    "chrome/installer/setup/installer_crash_reporter_client.cc",
    "chrome/notification_helper/notification_helper_crash_reporter_client.cc",
    "chrome/windows_services/service_program/crash_reporting.cc",
    "chrome/installer/linux/common/installer.py",
    "chrome/browser/ui/startup/default_browser_prompt/BUILD.gn",
    "chrome/browser/ui/startup/default_browser_prompt/default_browser_infobar_delegate.cc",
    "chrome/browser/ui/startup/default_browser_prompt/default_browser_infobar_delegate.h",
    "chrome/browser/ui/pdf/infobar/BUILD.gn",
    "chrome/browser/ui/pdf/infobar/pdf_infobar_delegate.cc",
    "chrome/browser/ui/pdf/infobar/pdf_infobar_delegate.h",
    "chrome/browser/ui/pdf/infobar/pdf_infobar_controller.cc",
    "chrome/browser/infobars/infobar_spec.h",
    "chrome/browser/infobars/infobar_spec.cc",
    "chrome/browser/infobars/browser_infobar_manager.cc",
    "chrome/browser/ui/startup/default_browser_prompt/pin_infobar/BUILD.gn",
    "chrome/browser/ui/startup/default_browser_prompt/pin_infobar/pin_infobar_controller.cc",
    "chrome/browser/ui/startup/default_browser_prompt/pin_infobar/pin_infobar_delegate.cc",
    "chrome/browser/ui/startup/default_browser_prompt/pin_infobar/pin_infobar_delegate.h",
    "chrome/browser/ui/infobars/BUILD.gn",
    "chrome/browser/ui/infobars/browser_infobar_registry.cc",
    "chrome/browser/ui/views/session_restore_infobar/BUILD.gn",
    "chrome/browser/ui/views/session_restore_infobar/session_restore_infobar_delegate.cc",
    "chrome/browser/ui/views/session_restore_infobar/session_restore_infobar_delegate.h",
    "chrome/browser/ui/startup/BUILD.gn",
    "chrome/browser/ui/startup/startup_launch_infobar_delegate.cc",
    "chrome/browser/ui/startup/startup_launch_infobar_delegate.h",
    "chrome/browser/ui/cocoa/BUILD.gn",
    "chrome/browser/ui/cocoa/keystone_infobar_delegate.cc",
    "chrome/browser/ui/cocoa/keystone_infobar_delegate.h",
    "chrome/browser/profiles/profile_shortcut_manager_win.cc",
    "components/vector_icons/aggregate_vector_icons.py",
    "chrome/browser/ui/webui/version/version_ui.cc",
    "chrome/browser/ui/webui/intro/intro_ui.cc",
    "chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc",
    "chrome/browser/ui/webui/search_engine_choice/search_engine_choice_ui.cc",
    "chrome/browser/ui/webui/search_engine_choice/BUILD.gn",
    "chrome/browser/resources/intro/default_browser/app.css",
    "chrome/browser/pdf/pdf_extension_util.cc",
    "chrome/browser/pdf/BUILD.gn",
    "chrome/tools/build/mac/infoplist_strings_util.cc",
    "chrome/VERSION",
    "chrome/install_static/chromium_install_modes.h",
    "chrome/install_static/BUILD.gn",
    "chrome/install_static/install_util_unittest.cc",
    "chrome/install_static/product_install_details_unittest.cc",
    "chrome/install_static/user_data_dir_win_unittest.cc",
    "components/policy/BUILD.gn",
    "components/policy/tools/generate_policy_source.gni",
    "components/policy/tools/generate_policy_source.py",
    "chrome/installer/mini_installer/BUILD.gn",
    "chrome/installer/mini_installer/chrome.release",
    "chrome/installer/mini_installer/mini_installer_constants.cc",
    "chrome/installer/util/shell_util.cc",
    "chrome/installer/setup/uninstall.cc",
    "chrome/installer/setup/setup_util.cc",
    "chrome/installer/setup/setup_util_unittest.cc",
    "chrome/installer/setup/BUILD.gn",
    "chrome/installer/setup/install_worker_unittest.cc",
)
ENV = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}


def command(*args, cwd=None, check=True):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True, env=ENV)
    if check and result.returncode:
        raise RuntimeError(f"{args}: {result.stderr.strip()}")
    return result


@unittest.skipUnless((SRC / "build/util/version.py").is_file() and shutil.which("zsh"),
                     "requires the local Chromium version utility and zsh")
class BrandingOverlayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = {
            path: command("git", "-C", str(SRC), "show", "HEAD:" + path).stdout
            for path in INPUT_PATHS
        }

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="branding-overlay-test-")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.repo = self.base / "product"
        self.overlay = self.repo / "tools/overlay"
        self.overlay.mkdir(parents=True)
        for name in ("brand_config.py", "brand_strings.py", "product_identity.py", "product_version.py", "install-branding.sh"):
            shutil.copy(ROOT / "tools/overlay" / name, self.overlay)
        shutil.copytree(ROOT / "tools/overlay/lib", self.overlay / "lib")
        (self.repo / "build").mkdir()
        shutil.copy(ROOT / "build/overlay.json", self.repo / "build/overlay.json")
        (self.repo / "branding").mkdir()
        shutil.copy(ROOT / "branding/identity.json", self.repo / "branding/identity.json")
        shutil.copy(ROOT / "branding/VERSION", self.repo / "branding/VERSION")
        patches = self.repo / "patches"
        patches.mkdir()
        self.patch = patches / PATCH_NAME
        shutil.copy(ROOT / "patches" / PATCH_NAME, self.patch)
        shutil.copy(ROOT / "patches/0003-fix-windows-protoc-python-aliases.patch", patches)
        (self.repo / "logo.png").write_bytes(b"asset existence fixture; never rendered")
        self.rename("Orbit")
        self.src = self.prepare("chromium")

    def rename(self, name, short_name=None):
        config = {"name": name, "provisional": True, "logo_source": "logo.png",
                  "logo_crop_size": 820}
        if short_name is not None:
            config["short_name"] = short_name
        if not re.fullmatch(r"[a-z][a-z0-9+.-]*", (short_name or name).lower()):
            config["internal_url_scheme"] = "orbit"
        (self.repo / "branding/brand.json").write_text(json.dumps(config))

    def prepare(self, name):
        src = self.base / name
        src.mkdir()
        command("git", "init", "-q", str(src))
        for path, text in self.upstream.items():
            target = src / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        (src / "structural.cc").write_text("// Upstream structural fixture\n")
        for name in ('android_chrome_version.py', 'LASTCHANGE.dummy'):
            shutil.copy(SRC / 'build/util' / name, src / 'build/util' / name)
        command("git", "add", ".", cwd=src)
        command("git", "-c", "user.name=Branding fixture", "-c",
                "user.email=fixture@example.invalid", "commit", "-qm", "Upstream fixture",
                cwd=src)
        return src

    def install(self, src=None, check_only=False, check=True):
        args = ["zsh", str(self.overlay / "install-branding.sh"), str(src or self.src)]
        if check_only:
            args.append("--check")
        return command(*args, check=check)

    def prepare_public_apply(self):
        for name in ("apply.sh", "install-yee-ui-sources.sh", "install-brand-assets.sh",
                     "brand_assets.py", "audit-branding.py"):
            shutil.copy(ROOT / "tools/overlay" / name, self.overlay)
        catalog_path = self.repo / "build/overlay.json"
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        catalog["source_roots"] = [{"source": "browser/ui",
                                    "destination": "chrome/browser/ui/views/yee"}]
        catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
        owned = self.repo / "browser/ui"
        owned.mkdir(parents=True)
        (owned / "fixture.cc").write_text("int yee_fixture;\n", encoding="utf-8")
        for path in ("chrome/browser/ui/BUILD.gn",
                     "chrome/browser/ui/tabs/tab_strip_prefs.cc"):
            target = self.src / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("", encoding="utf-8")
        (self.src / "windows.cc").write_text("upstream\n", encoding="utf-8")
        for filename, path, before, after in (
                ("0001-integrate-yee-shell.patch", "structural.cc",
                 "// Upstream structural fixture", "// 변경된 구조 픽스처"),
                ("0003-fix-windows-protoc-python-aliases.patch", "windows.cc",
                 "upstream", "overlay")):
            (self.repo / "patches" / filename).write_text(
                f"diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n"
                f"@@ -1 +1 @@\n-{before}\n+{after}\n", encoding="utf-8")
        policy = json.loads((ROOT / "branding/strings.json").read_text(encoding="utf-8"))
        policy["roots"] = ["components/components_strings.grd"]
        (self.repo / "branding/strings.json").write_text(
            json.dumps(policy, ensure_ascii=False, indent=2), encoding="utf-8")
        (self.repo / "branding/surfaces.json").write_text(
            json.dumps({"schema_version": 1, "surfaces": [], "string_roots": policy["roots"]}),
            encoding="utf-8")
        config_path = self.repo / "branding/brand.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["strings_policy"] = "branding/strings.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        components = self.src / "components"
        (components / "parts").mkdir(parents=True, exist_ok=True)
        (components / "components_strings.grd").write_text(
            '<grit><translations><file path="strings_ko.xtb" lang="ko"/></translations>'
            '<messages><part file="parts/messages.grdp"/></messages></grit>', encoding="utf-8")
        (components / "parts/messages.grdp").write_text(
            '<grit-part><message name="IDS_PUBLIC_APPLY">Welcome to Chromium</message>'
            '</grit-part>', encoding="utf-8")
        (components / "strings_ko.xtb").write_text(
            '<translationbundle lang="ko"><translation id="5862307444128926510">'
            'Chromium에 오신 것을 환영합니다.</translation></translationbundle>', encoding="utf-8")
        (components / "components_strings.grd.gritdeps").write_text(
            "components/parts/messages.grdp\ncomponents/strings_ko.xtb\n", encoding="utf-8")
        command("git", "add", ".", cwd=self.src)
        command("git", "-c", "user.name=Branding fixture", "-c",
                "user.email=fixture@example.invalid", "commit", "-qm", "Public apply fixture",
                cwd=self.src)

    def apply_overlay(self, *options, check=True):
        return command("zsh", str(self.overlay / "apply.sh"), str(self.src),
                       *options, check=check)

    def prepare_public_assets(self):
        sys.path.insert(0, str(ROOT / "tools/overlay"))
        try:
            from brand_assets import UPSTREAM_INPUTS, upstream_input_blobs
        finally:
            sys.path.pop(0)
        for relative in UPSTREAM_INPUTS:
            path = self.src / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("upstream asset fixture\n", encoding="utf-8")
        command("git", "add", *UPSTREAM_INPUTS, cwd=self.src)
        command("git", "-c", "user.name=Branding fixture", "-c",
                "user.email=fixture@example.invalid", "commit", "-qm", "Asset inputs", cwd=self.src)
        config_path = self.repo / "branding/brand.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["logo_assets_manifest"] = "assets/brand/generated/manifest.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        generator = self.overlay / "generate-brand-assets.py"
        generator.write_text('''from pathlib import Path
import hashlib
import json
import sys
from brand_assets import upstream_input_blobs

repo = Path(__file__).resolve().parents[2]
if (repo / 'fail-brand-generation').exists():
    raise SystemExit('fixture brand generation failed')
config = json.loads((repo / 'branding/brand.json').read_text(encoding='utf-8'))
manifest = repo / config['logo_assets_manifest']
manifest.parent.mkdir(parents=True, exist_ok=True)
output = manifest.parent / 'mark.png'
output.write_bytes(b'prepared brand asset')
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
data = {'schema_version': 1, 'product_name': config['name'],
        'short_name': config.get('short_name', config['name']),
        'source_sha256': digest(repo / config['logo_source']),
        'generator_sha256': digest(Path(__file__)),
        'palette': config.get('logo_palette', {}),
        'upstream_inputs': upstream_input_blobs(Path(sys.argv[1])),
        'files': [{'source': 'mark.png',
                   'destination': 'chrome/app/theme/chromium/product_logo_16.png',
                   'sha256': digest(output)}]}
manifest.write_text(json.dumps(data), encoding='utf-8')
(repo / 'generation-completed').write_text('generated', encoding='utf-8')
''', encoding="utf-8")
        package = self.repo / "assets/brand/generated"
        package.mkdir(parents=True)
        output = package / "mark.png"
        output.write_bytes(b'stale package asset')
        manifest = {"schema_version": 1, "product_name": "Old Orbit", "short_name": "Orbit",
                    "source_sha256": hashlib.sha256((self.repo / "logo.png").read_bytes()).hexdigest(),
                    "generator_sha256": hashlib.sha256(generator.read_bytes()).hexdigest(),
                    "palette": {},
                    "upstream_inputs": upstream_input_blobs(self.src),
                    "files": [{"source": "mark.png",
                               "destination": "chrome/app/theme/chromium/product_logo_16.png",
                               "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}]}
        (package / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    def audit_overlay(self):
        result = command(sys.executable, str(self.overlay / "audit-branding.py"),
                         str(self.src), "--json")
        return json.loads(result.stdout)

    def snapshot(self):
        return {str(path.relative_to(self.src)): (path.read_bytes(), path.stat().st_mtime_ns)
                for path in self.src.rglob("*")
                if path.is_file() and ".git" not in path.relative_to(self.src).parts}

    def version_and_signing_name(self, name):
        branding = self.src / INPUT_PATHS[0]
        version_script = str(self.src / "build/util/version.py")
        result = command(sys.executable, version_script, "-f", str(branding),
                         "-t", "@PRODUCT_FULLNAME@")
        self.assertEqual(result.stdout.rstrip("\n"), name)
        values = self.base / "signing-values"
        values.write_text("IS_CHROME_BRANDED=0\nENABLE_UPDATER=0\nUSE_STATIC_ANGLE=0\n")
        rendered = command(sys.executable, version_script, "-f", str(branding),
                           "-f", str(self.src / "chrome/VERSION"), "-f", str(values),
                           "-i", str(self.src / INPUT_PATHS[2])).stdout
        tree = ast.parse(rendered)
        properties = {node.name: node for node in ast.walk(tree)
                      if isinstance(node, ast.FunctionDef)}
        for key in ("app_product", "product"):
            self.assertEqual(properties[key].body[0].value.value, name)

    def test_fresh_install_preview_rename_and_reapply(self):
        before = self.snapshot()
        self.install(check_only=True)
        self.assertEqual(before, self.snapshot())
        self.install()
        self.version_and_signing_name("Orbit")
        self.rename("O'Reilly & 새 이름", short_name="새 이름")
        self.install()
        self.version_and_signing_name("O'Reilly & 새 이름")
        installed = self.snapshot()
        self.install()
        self.install(check_only=True)
        self.assertEqual(installed, self.snapshot())
        command("git", "apply", "--reverse", "--check", str(self.patch), cwd=self.src)

    def test_fixed_name_and_comment_marker_migrations(self):
        for legacy in ("fixed-name", "comment-marker"):
            with self.subTest(legacy=legacy):
                self.src = self.prepare("legacy-" + legacy)
                branding = self.src / INPUT_PATHS[0]
                branding.write_text(branding.read_text().replace("=Chromium", "=Yee"))
                if legacy == "comment-marker":
                    legacy_patch = self.base / "legacy-comment.patch"
                    text = self.patch.read_text().split("diff --git a/" + INPUT_PATHS[2])[0]
                    first_part = text.index("diff --git a/" + INPUT_PATHS[1])
                    text = text[first_part:]
                    legacy_patch.write_text(text)
                    command("git", "apply", str(legacy_patch), cwd=self.src)
                    branding.write_text(
                        "# Product names are managed by the Yee overlay brand installer.\n"
                        + branding.read_text())
                self.install()
                self.version_and_signing_name("Orbit")
                command("git", "apply", "--reverse", "--check", str(self.patch), cwd=self.src)

    def test_invalid_version_input_is_rejected_before_patch_mutation(self):
        branding = self.src / INPUT_PATHS[0]
        branding.write_text(branding.read_text() + "# Not a version input\n")
        before = self.snapshot()
        result = self.install(check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(before, self.snapshot())

    def test_public_apply_previews_and_installs_enabled_brand_strings(self):
        self.prepare_public_apply()
        before = self.snapshot()
        self.apply_overlay("--check", "--skip-brand-assets")
        self.assertEqual(before, self.snapshot())
        self.apply_overlay("--skip-brand-assets")
        self.version_and_signing_name("Orbit")
        self.assertIn("yee_brand_strings_enabled = true",
                      (self.src / "chrome/app/yee_branding_strings.gni").read_text(encoding="utf-8"))
        generated = self.src / "chrome/app/yee_branding_strings"
        manifest = json.loads((generated / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["name"], "Orbit")
        self.assertIn("Welcome to Orbit",
                      (generated / "inputs/components/parts/messages.grdp").read_text(encoding="utf-8"))
        self.assertIn("Orbit에 오신 것을 환영합니다.",
                      (generated / "inputs/components/strings_ko.xtb").read_text(encoding="utf-8"))
        self.assertIn("yee_branding_strings/inputs/components/parts/messages.grdp",
                      (self.src / "components/components_strings.grd").read_text(encoding="utf-8"))
        self.assertEqual((self.src / "chrome/browser/ui/views/yee/fixture.cc").read_text(encoding="utf-8"),
                         "int yee_fixture;\n")
        self.assertEqual((self.src / "windows.cc").read_text(encoding="utf-8"), "overlay\n")
        installed = self.snapshot()
        self.apply_overlay("--skip-brand-assets")
        self.apply_overlay("--check", "--skip-brand-assets")
        self.assertEqual(installed, self.snapshot())

    def test_public_apply_rejects_all_invalid_inputs_before_any_checkout_write(self):
        self.prepare_public_apply()
        policy = self.repo / "branding/strings.json"
        catalog = self.repo / "build/overlay.json"
        last_patch = self.repo / "patches/0003-fix-windows-protoc-python-aliases.patch"
        cases = (
            (policy, policy.read_bytes().replace(b'"schema_version": 1', b'"schema_version": 2'),
             "Unsupported brand string policy"),
            (catalog, catalog.read_bytes().replace(b'"source": "browser/ui"', b'"source": "browser/missing"'),
             "Missing owned source root"),
            (last_patch, last_patch.read_bytes().replace(b"-upstream", b"-different"), "Cannot apply"),
            (self.src / "chrome/app/yee_branding_strings", b"blocked output directory",
             "parent is not a directory"),
            (None, None, "logo_assets_manifest"),
        )
        for path, invalid, error in cases:
            with self.subTest(error=error):
                original = path.read_bytes() if path and path.is_file() else None
                try:
                    if path:
                        path.write_bytes(invalid)
                    before = self.snapshot()
                    result = self.apply_overlay(*(["--skip-brand-assets"] if path else []), check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(error, result.stderr)
                    self.assertEqual(before, self.snapshot())
                finally:
                    if path:
                        if original is None:
                            path.unlink()
                        else:
                            path.write_bytes(original)

    def test_public_apply_rejects_invalid_name_outputs_in_preview_and_application(self):
        self.prepare_public_apply()
        external = self.base / "external-branding-output"
        external.write_bytes(b'external output fixture')
        for filename in ("yee_branding_strings.gni", "yee_product_names.grdp", "yee_app_menu_name.grdp"):
            path = self.src / "chrome/app" / filename
            for obstruction in ("directory", "symlink"):
                with self.subTest(filename=filename, obstruction=obstruction):
                    if obstruction == "directory":
                        path.mkdir()
                        error = "Branding output is not a file"
                    else:
                        path.symlink_to(external)
                        error = "Branding output traverses a symlink"
                    try:
                        before = self.snapshot()
                        for options in (("--check", "--skip-brand-assets"), ("--skip-brand-assets",)):
                            result = self.apply_overlay(*options, check=False)
                            self.assertNotEqual(result.returncode, 0)
                            self.assertIn(error, result.stderr)
                            self.assertNotIn("overlay is ready", result.stdout)
                            self.assertEqual(before, self.snapshot())
                        self.assertEqual(external.read_bytes(), b'external output fixture')
                    finally:
                        if obstruction == "directory":
                            path.rmdir()
                        else:
                            path.unlink()

    def test_public_apply_rejects_disabling_installed_strings_before_rename(self):
        self.prepare_public_apply()
        self.apply_overlay("--skip-brand-assets")
        config_path = self.repo / "branding/brand.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config.pop("strings_policy")
        config["name"] = "Nova"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        generated = self.src / "chrome/app/yee_branding_strings"
        for state in ("present", "empty", "removed"):
            with self.subTest(generated_directory=state):
                if state != "present":
                    shutil.rmtree(generated)
                    if state == "empty":
                        generated.mkdir()
                before = self.snapshot()
                for options in (("--check", "--skip-brand-assets"), ("--skip-brand-assets",)):
                    result = self.apply_overlay(*options, check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Cannot disable strings_policy", result.stderr)
                    self.assertNotIn("overlay is ready", result.stdout)
                    self.assertEqual(before, self.snapshot())
                audit = self.audit_overlay()
                self.assertFalse(audit["brand_strings"]["enabled"])
                self.assertFalse(audit["brand_strings"]["applied"])
                self.assertIn("Cannot disable strings_policy", audit["brand_strings"]["error"])
                self.assertIn("components/components_strings.grd", audit["brand_strings"]["mismatches"])
                self.assertIn("components/components_strings.grd.gritdeps", audit["brand_strings"]["mismatches"])
                self.assertTrue(audit["blockers"]["brand_strings_not_applied"])
                self.assertEqual(generated.exists(), state != "removed")
                if state == "empty":
                    self.assertFalse(any(generated.iterdir()))
                self.assertEqual(before, self.snapshot())

    def test_public_apply_rejects_missing_tracked_gritdeps_and_audit_reports_it(self):
        self.prepare_public_apply()
        self.apply_overlay("--skip-brand-assets")
        dependency = self.src / "components/components_strings.grd.gritdeps"
        dependency.unlink()
        before = self.snapshot()
        for options in (("--check", "--skip-brand-assets"), ("--skip-brand-assets",)):
            result = self.apply_overlay(*options, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unmanaged change to branding input: components/components_strings.grd.gritdeps",
                          result.stderr)
            self.assertNotIn("overlay is ready", result.stdout)
            self.assertEqual(before, self.snapshot())
        audit = self.audit_overlay()
        self.assertFalse(audit["brand_strings"]["applied"])
        self.assertIn("components/components_strings.grd.gritdeps", audit["brand_strings"]["mismatches"])
        self.assertTrue(audit["blockers"]["brand_strings_not_applied"])
        self.assertEqual(before, self.snapshot())

    def test_public_apply_prepares_stale_assets_before_any_checkout_write(self):
        self.prepare_public_apply()
        self.prepare_public_assets()
        before = self.snapshot()
        package = self.repo / "assets/brand/generated"
        package_before = {path.name: path.read_bytes() for path in package.iterdir()}
        preview = self.apply_overlay("--check", check=False)
        self.assertNotEqual(preview.returncode, 0)
        self.assertIn("stale", preview.stderr)
        self.assertEqual(before, self.snapshot())
        self.assertEqual(package_before, {path.name: path.read_bytes() for path in package.iterdir()})
        failure = self.repo / "fail-brand-generation"
        failure.touch()
        result = self.apply_overlay(check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fixture brand generation failed", result.stderr)
        self.assertNotIn("overlay is ready", result.stdout)
        self.assertEqual(before, self.snapshot())
        failure.unlink()
        self.apply_overlay()
        self.assertEqual((self.src / "chrome/app/theme/chromium/product_logo_16.png").read_bytes(),
                         b'prepared brand asset')
        self.assertTrue((self.repo / "generation-completed").is_file())
        self.assertEqual(json.loads((package / "manifest.json").read_text(encoding="utf-8"))["product_name"],
                         "Orbit")

    def test_installer_rc_names_follow_generated_parts_and_rename(self):
        self.install()
        grd = self.base / "chromium_strings.grd"
        resources = self.base / "resources"
        resources.mkdir()
        xtb = resources / "chromium_strings_ko.xtb"
        xtb.write_text('<translationbundle lang="ko"/>')
        grd.write_text('<grit><translations><file path="resources/chromium_strings_ko.xtb"/></translations>'
                       '<messages><message name="IDS_PRODUCT_NAME">Chrome for Testing</message>'
                       '<part file="chromium/chrome/app/yee_product_names.grdp"/>'
                       '</messages></grit>')
        part = self.src / "chrome/app/yee_product_names.grdp"
        header, rc = self.base / "strings.h", self.base / "strings.rc"
        args = [sys.executable, str(self.src / INPUT_PATHS[3]), '-i', str(grd),
                '-r', 'resources', '-x', str(xtb), '--header-file', str(header),
                '--rc-file', str(rc), '--first-resource-id', '1600']
        env = {**ENV, 'PYTHONPATH': os.pathsep.join([
            str(SRC / 'tools/grit'), str(SRC / 'tools/python')])}
        # Existing callers retain the previous flat extraction behavior.
        subprocess.run(args, env=env, capture_output=True, check=True)
        self.assertIn('"Chrome for Testing"', rc.read_text(encoding='utf-16'))
        for name in ('Orbit', "O'Reilly & 새 이름"):
            self.rename(name)
            self.install()
            subprocess.run(args + ['--source-part', str(part)], env=env,
                           capture_output=True, check=True)
            generated = rc.read_text(encoding='utf-16')
            self.assertIn('IDS_PRODUCT_NAME_EN_US "' + name + '"', generated)
            self.assertIn('IDS_PRODUCT_NAME_KO "' + name + '"', generated)
            self.assertNotIn('Chrome for Testing', generated)
            self.assertIn('IDS_PRODUCT_NAME_BASE', header.read_text())

    def test_unicode_version_rc_is_accepted_by_actual_resource_driver(self):
        self.rename("O'Reilly & 새 이름")
        self.install()
        output = self.base / 'version.rc'
        args = [sys.executable, str(self.src / 'build/util/version.py'),
                '-f', str(self.src / INPUT_PATHS[0]),
                '-t', 'VALUE "ProductName", "@PRODUCT_FULLNAME@"',
                '-o', str(output)]
        command(*args)
        self.assertIn('새 이름', output.read_text(encoding='utf-8'))
        command(*args, '--output-encoding', 'utf-16')
        tree = ast.parse((SRC / 'build/toolchain/win/rc/rc.py').read_text())
        reader = next(node for node in tree.body
                      if isinstance(node, ast.FunctionDef) and node.name == 'ReadInput')
        import codecs
        namespace = {'codecs': codecs, 'sys': sys}
        exec(compile(ast.Module(body=[reader], type_ignores=[]), 'rc.ReadInput', 'exec'), namespace)
        data, unicode_input = namespace['ReadInput'](str(output))
        self.assertTrue(unicode_input)
        self.assertIn('새 이름', data.decode('utf-8'))
        installed = output.stat().st_mtime_ns
        command(*args, '--output-encoding', 'utf-16')
        self.assertEqual(output.stat().st_mtime_ns, installed)

    def test_linux_branding_keeps_equals_inside_name(self):
        self.rename('Orbit = Browser')
        self.install()
        tree = ast.parse((self.src / 'chrome/installer/linux/common/installer.py').read_text())
        parser = next(node for node in ast.walk(tree)
                      if isinstance(node, ast.FunctionDef) and node.name == 'parse_simple')
        import pathlib
        namespace = {'pathlib': pathlib}
        exec(compile(ast.Module(body=[parser], type_ignores=[]), 'installer.parse_simple', 'exec'), namespace)
        values = namespace['parse_simple'](self.src / INPUT_PATHS[0])
        self.assertEqual(values['PRODUCT_FULLNAME'], 'Orbit = Browser')

    def test_upstream_version_utility_regressions(self):
        self.install()
        shutil.copy(SRC / 'build/util/version_test.py', self.src / 'build/util/version_test.py')
        # Chromium's tests use the mock package API, also provided by stdlib.
        result = command(sys.executable, '-c',
                         'import sys, unittest, unittest.mock; '
                         'sys.modules["mock"] = unittest.mock; '
                         'unittest.main(module="version_test")',
                         cwd=self.src / 'build/util')
        self.assertIn('OK', result.stderr)

    def test_shell_patch_regeneration_keeps_branding_owned_paths_separate(self):
        self.install()
        (self.src / "structural.cc").write_text("// Updated structural fixture\n")
        sys.path.insert(0, str(ROOT / "tools/overlay"))
        self.addCleanup(sys.path.remove, str(ROOT / "tools/overlay"))
        spec = importlib.util.spec_from_file_location(
            "regenerate_brand_fixture", ROOT / "tools/overlay/regenerate-shell-patch.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT, module.SRC = self.repo, self.src
        module.PATCH, module.NEW_GLUE = self.repo / "0001.patch", []
        with contextlib.redirect_stdout(io.StringIO()):
            module.main()
        generated = module.PATCH.read_text()
        self.assertIn("structural.cc", generated)
        for path in INPUT_PATHS[:-1]:
            self.assertNotIn(path, generated)
        fresh = self.prepare("fresh-after-regeneration")
        command("git", "apply", str(module.PATCH), cwd=fresh)
        self.install(src=fresh)
        command("git", "apply", "--reverse", "--check", str(self.patch), cwd=fresh)


if __name__ == "__main__":
    unittest.main()
