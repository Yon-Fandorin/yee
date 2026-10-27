"""제품 식별자 입력과 생성의 충돌·경로 경계를 검사한다. 앱을 실행하지 않는다."""

import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("product_identity", ROOT / "tools/overlay/product_identity.py")
identity = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = identity
SPEC.loader.exec_module(identity)


class ProductIdentityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="yee-identity-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = self.root / "identity.json"
        self.raw = json.loads(identity.CONFIG_PATH.read_text(encoding="utf-8"))
        (self.root / "chrome/install_static").mkdir(parents=True)
        (self.root / "chrome/app").mkdir()
        (self.root / "chrome/install_static/chromium_install_modes.h").write_text("fixture")

    def load(self, raw=None):
        self.config.write_text(json.dumps(self.raw if raw is None else raw), encoding="utf-8")
        return identity.load_identity(self.config)

    def install(self, value, check=False):
        with contextlib.redirect_stdout(io.StringIO()):
            identity.install_identity(self.root, value, check)

    def test_native_guid_and_policy_representation(self):
        value = self.load()
        self.install(value)
        header = (self.root / "chrome/app/yee_product_identity.h").read_text(encoding="utf-8")
        self.assertIn('kProfileDirectory[] = L"Yee"', header)
        self.assertIn('kDirectLaunchScheme[] = "yee-browser"', header)
        self.assertIn("0xC9EB99B2, 0x59A9, 0x45FB, {0xA3, 0x09", header)
        self.assertIn('yee_windows_policy_key = "SOFTWARE\\\\Policies\\\\Yee"',
                      (self.root / "chrome/app/yee_product_identity.gni").read_text(encoding="utf-8"))
        self.assertNotIn("3251537155-1984446955", value.sandbox_sid_prefix)
        registration = (self.root / "chrome/app/yee_windows_registration.h").read_text(encoding="utf-8")
        self.assertIn('YEE_WINDOWS_REGISTRY_APPLICATION_NAME L"Yee.exe"', registration)
        self.assertIn('YEE_WINDOWS_PRODUCT_REGISTRY_KEY L"Software\\\\Yee"', registration)

    def test_preview_and_repeated_generation_do_not_mutate_outputs(self):
        value = self.load()
        self.install(value, check=True)
        output = self.root / "chrome/app/yee_product_identity.h"
        self.assertFalse(output.exists())
        self.install(value)
        before = output.stat().st_mtime_ns
        self.install(value)
        self.assertEqual(output.stat().st_mtime_ns, before)

    def test_mac_identity_is_independent_and_generates_native_input(self):
        """설치판 식별자는 표시 이름과 Windows 등록 값을 바꾸지 않는다."""
        value = self.load()
        self.install(value)
        generated = (self.root / "chrome/app/yee_mac_identity.values").read_text()
        self.assertIn("YEE_MAC_BUNDLE_ID=io.github.yon-fandorin.yee\n", generated)
        self.assertIn("YEE_MAC_KEYCHAIN_SERVICE=Yee Safe Storage\n", generated)
        self.assertIn("YEE_MAC_KEYCHAIN_ACCOUNT=Yee\n", generated)
        self.assertEqual(value.windows.app_id, self.raw["windows"]["app_id"])

    def test_rejects_mac_identity_collisions_and_template_injection(self):
        """타 브라우저 ID와 native 입력을 깨뜨리는 값은 쓰기 전에 거부한다."""
        for key, value in (("bundle_id", "org.chromium.Chromium"),
                           ("bundle_id", "com.google.Chrome"),
                           ("bundle_id", "yee"),
                           ("bundle_id", "io.github.yee\nOTHER=value"),
                           ("keychain_service", "Chromium Safe Storage"),
                           ("keychain_service", 'Yee "Storage"'),
                           ("keychain_account", "Chrome"),
                           ("keychain_account", "Yee\nOTHER=value")):
            with self.subTest(key=key, value=value):
                raw = copy.deepcopy(self.raw)
                raw["macos"][key] = value
                with self.assertRaises(ValueError):
                    self.load(raw)

    def test_rejects_reserved_paths_and_identifiers(self):
        for field, values in (("profile_directory", ("../Chromium", "chromium", "CON", "COM1", "Yee\\Data")),
                              ("product_id", ("chrome", "yee/other", ""))):
            for value in values:
                with self.subTest(field=field, value=value):
                    raw = copy.deepcopy(self.raw)
                    raw[field] = value
                    with self.assertRaises(ValueError):
                        self.load(raw)

    def test_rejects_registration_collisions(self):
        for key, value in (("browser_prog_id_prefix", "ChromeHTML"),
                           ("browser_prog_id_prefix", "YeePDF"),
                           ("app_id", "Chromium"),
                           ("direct_launch_scheme", "https"),
                           ("toast_activator_clsid", "{635EFA6F-08D6-4EC9-BD14-8A0FDE975159}"),
                           ("toast_activator_clsid", self.raw["windows"]["active_setup_guid"]),
                           ("elevator_clsid", "{00000000-0000-0000-0000-000000000000}")):
            with self.subTest(key=key):
                raw = copy.deepcopy(self.raw)
                raw["windows"][key] = value
                with self.assertRaises(ValueError):
                    self.load(raw)

    def test_all_output_paths_are_checked_before_writing(self):
        value = self.load()
        (self.root / "chrome/app/yee_product_identity.gni").mkdir()
        with self.assertRaises(ValueError):
            self.install(value)
        self.assertFalse((self.root / "chrome/app/yee_product_identity.h").exists())


if __name__ == "__main__":
    unittest.main()
