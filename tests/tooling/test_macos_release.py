"""Mac 패키지의 앱 경계·버전·배포 식별자 적용을 검사한다. 앱을 실행하지 않는다."""

import importlib.util
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("macos_release", ROOT / "tools/dev/lib/macos_release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class MacReleaseTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="yee-macos-package-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.app = self.root / "Yee.app"
        self.identity = release.load_identity()
        self.version = "153.0.8005.4"
        self.info = self.app / "Contents/Info.plist"
        self.info.parent.mkdir(parents=True)
        self.write_plist(self.info, {
            "CFBundleName": "Yee", "CFBundleExecutable": "Yee",
            "CFBundleShortVersionString": self.version,
            "CFBundleIdentifier": "org.chromium.Chromium",
            "UTTypeIdentifier": "org.chromium.extension",
        })
        executable = self.app / "Contents/MacOS/Yee"
        executable.parent.mkdir()
        executable.write_bytes(b"test-executable")
        self.resources = (self.app / "Contents/Frameworks/Yee Framework.framework/Versions"
                          / self.version / "Resources")
        self.resources.mkdir(parents=True)
        self.write_plist(self.resources / "Info.plist", {
            "CFBundleIdentifier": "org.chromium.Chromium.framework",
            "CFBundleShortVersionString": self.version,
        })
        for filename in release.PAYLOAD_NAMES:
            (self.resources / filename).write_text(filename)

    @staticmethod
    def write_plist(path, value):
        path.write_bytes(plistlib.dumps(value))

    def test_rebranding_a_copy_keeps_development_bundle_and_document_types(self):
        """설치판과 개발 번들의 프로필·키체인이 섞이지 않아야 한다."""
        installed = self.root / "Applications/Yee.app"
        shutil.copytree(self.app, installed, symlinks=True)
        original = self.info.read_bytes()
        release.prepare_installed_bundle(installed, self.identity)
        info, resources = release.validate_bundle(installed, "Yee", self.version, self.identity)
        self.assertEqual(info["CFBundleIdentifier"], self.identity.macos.bundle_id)
        self.assertEqual(info["CrProductDirName"], "Yee")
        self.assertEqual(info["UTTypeIdentifier"], "org.chromium.extension")
        self.assertEqual(release.read_plist(resources / "Info.plist")["CFBundleIdentifier"],
                         self.identity.macos.bundle_id + ".framework")
        self.assertEqual(self.info.read_bytes(), original)

    def test_stale_framework_and_missing_license_sources_are_rejected(self):
        """최신 실행 파일에 오래된 Framework나 누락된 고지 자료를 묶지 않는다."""
        framework_info = self.resources / "Info.plist"
        original = framework_info.read_bytes()
        self.write_plist(framework_info, {"CFBundleShortVersionString": "153.0.8005.3"})
        with self.assertRaisesRegex(ValueError, "Framework"):
            release.validate_bundle(self.app, "Yee", self.version, self.identity)
        framework_info.write_bytes(original)
        (self.resources / "YeeContentBlockingSources.tar.xz").unlink()
        with self.assertRaisesRegex(ValueError, "source notices"):
            release.validate_bundle(self.app, "Yee", self.version, self.identity)

    def test_wrong_app_and_version_are_rejected(self):
        """이름이 비슷한 타 브라우저나 구버전은 배포본으로 만들지 않는다."""
        for key, value in (("CFBundleIdentifier", "com.google.Chrome"),
                           ("CFBundleName", "Other browser"),
                           ("CFBundleShortVersionString", "153.0.8005.3")):
            with self.subTest(key=key):
                info = release.read_plist(self.info)
                original = self.info.read_bytes()
                info[key] = value
                self.write_plist(self.info, info)
                with self.assertRaises(ValueError):
                    release.validate_bundle(self.app, "Yee", self.version, self.identity)
                self.info.write_bytes(original)

    def test_external_dependencies_cannot_escape_into_user_data(self):
        """번들 밖 파일을 서명하거나 배포 이미지에 넣지 않는다."""
        outside = self.root / "user-data"
        outside.write_text("preserve")
        (self.resources / "external").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            release.validate_bundle(self.app, "Yee", self.version, self.identity)
        self.assertEqual(outside.read_text(), "preserve")

    def test_absolute_internal_link_is_rechecked_after_copying(self):
        """복사 후 원본을 가리키는 절대 링크는 원본 서명 전에 차단한다."""
        target = self.resources / "YeeContentBlockingNotices.txt"
        (self.resources / "absolute-internal").symlink_to(target.resolve())
        release.validate_bundle(self.app, "Yee", self.version, self.identity)
        copied = self.root / "staged/Yee.app"
        shutil.copytree(self.app, copied, symlinks=True)
        original = target.read_bytes()
        with self.assertRaisesRegex(ValueError, "escapes"):
            release.validate_bundle(copied, "Yee", self.version, self.identity)
        self.assertEqual(target.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
