#!/usr/bin/env python3
"""Exercise real branding patches and version templates in disposable checkouts.

Never compiles, builds, launches, or modifies the user's Chromium checkout.
"""

import ast
import contextlib
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
    "chrome/VERSION",
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
        for name in ("brand_config.py", "install-branding.sh"):
            shutil.copy(ROOT / "tools/overlay" / name, self.overlay)
        shutil.copytree(ROOT / "tools/overlay/lib", self.overlay / "lib")
        (self.repo / "build").mkdir()
        shutil.copy(ROOT / "build/overlay.json", self.repo / "build/overlay.json")
        (self.repo / "branding").mkdir()
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
