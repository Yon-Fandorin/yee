#!/usr/bin/env python3
"""빌드한 앱을 독립 Yee 설치판으로 서명하고 드래그 설치용 DMG를 만든다."""

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/overlay"))
from brand_config import load_brand
from product_identity import load_identity
from product_version import version_inputs, version_string


PAYLOAD_NAMES = (
    "YeeCommunityFilterManifest.json", "YeeCommunityFilterNotices.txt",
    "YeeCommunityFilters.txt", "YeeCommunityFilterSources.tar.xz",
    "YeeCommunityResources.json", "YeeCompiledFilterManifest.json",
    "YeeCompiledFilters.dat", "YeeContentBlockingNotices.txt",
    "YeeContentBlockingSources.tar.xz",
)
MACHO_MAGIC = {bytes.fromhex(value) for value in
               ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}


def sha256(path):
    """대용량 Framework와 DMG를 메모리에 올리지 않고 해시한다."""
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_plist(path):
    with Path(path).open("rb") as stream:
        return plistlib.load(stream)


def validate_bundle(app, brand, version, identity):
    """다른 앱·오래된 버전·번들 밖 의존성을 배포본에 넣지 않는다."""
    name = brand.name
    app = Path(app).resolve(strict=True)
    info = read_plist(app / "Contents/Info.plist")
    if (app.name != name + ".app" or info.get("CFBundleName") != brand.short_name
            or info.get("CFBundleExecutable") != name
            or info.get("CFBundleShortVersionString") != version
            or info.get("CFBundleIdentifier") not in
               {"org.chromium.Chromium", identity.macos.bundle_id}):
        raise ValueError("App name, version, or source bundle identity does not match Yee")
    executable = app / "Contents/MacOS" / name
    if not executable.is_file():
        raise ValueError("The app executable is missing")
    for path in app.rglob("*"):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(app):
            raise ValueError(f"App dependency escapes the bundle: {path}")
    resources = (app / "Contents/Frameworks" / f"{name} Framework.framework"
                 / "Versions" / version / "Resources")
    if read_plist(resources / "Info.plist").get("CFBundleShortVersionString") != version:
        raise ValueError("The bundled Framework has a stale version")
    if any(not (resources / filename).is_file() for filename in PAYLOAD_NAMES):
        raise ValueError("Content-blocking data or source notices are missing from the app")
    return info, resources


def replace_bundle_id(value, previous, current):
    """동일 제품의 helper·Framework ID만 바꾸고 공용 문서 형식은 유지한다."""
    if isinstance(value, str) and (value == previous or value.startswith(previous + ".")):
        return current + value[len(previous):]
    if isinstance(value, list):
        return [replace_bundle_id(item, previous, current) for item in value]
    if isinstance(value, dict):
        return {replace_bundle_id(key, previous, current): replace_bundle_id(item, previous, current)
                for key, item in value.items()}
    return value


def prepare_installed_bundle(app, identity):
    """복사한 설치판에만 고정 ID와 기본 프로필 경로를 적용한다."""
    main_info = app / "Contents/Info.plist"
    previous = read_plist(main_info)["CFBundleIdentifier"]
    for path in app.rglob("Info.plist"):
        if path.is_symlink():
            continue
        data = replace_bundle_id(read_plist(path), previous, identity.macos.bundle_id)
        if path == main_info:
            data["CrProductDirName"] = identity.profile_directory
        with path.open("wb") as stream:
            plistlib.dump(data, stream, sort_keys=False)


def sign_bundle(app):
    """중첩 실행 파일부터 서명해 설치판의 리소스와 Info.plist를 봉인한다."""
    binaries = []
    bundles = [app]
    for path in app.rglob("*"):
        if path.is_symlink():
            continue
        if path.is_dir() and path.suffix in {".app", ".framework", ".xpc"}:
            bundles.append(path)
        elif path.is_file():
            with path.open("rb") as stream:
                if stream.read(4) in MACHO_MAGIC:
                    binaries.append(path)
    for path in (sorted(binaries, key=lambda p: len(p.parts), reverse=True)
                 + sorted(bundles, key=lambda p: len(p.parts), reverse=True)):
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none",
                        "--preserve-metadata=entitlements", str(path)], check=True)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)], check=True)


def dmg_settings(app, background, icon):
    """Finder의 두 아이콘을 이용하는 표준 드래그 설치 화면이다."""
    return {
        "files": [str(app)], "symlinks": {"Applications": "/Applications"},
        "format": "ULMO", "filesystem": "HFS+", "icon": str(icon),
        "background": str(background), "window_rect": ((200, 200), (640, 400)),
        "icon_locations": {app.name: (150, 180), "Applications": (490, 180)},
        "icon_size": 112, "text_size": 14, "label_pos": "bottom",
        "default_view": "icon-view", "show_status_bar": False,
        "show_toolbar": False, "show_sidebar": False,
        "show_tab_view": False, "show_pathbar": False,
    }


def verify_image(image, mount, brand, version, identity):
    """Finder 메타데이터 적용 후에도 배포 이미지의 앱 서명이 유효한지 확인한다."""
    subprocess.run(["/usr/bin/hdiutil", "verify", str(image)], check=True)
    mount.mkdir()
    try:
        subprocess.run(["/usr/bin/hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen",
                        "-mountpoint", str(mount), str(image)], check=True)
        app = mount / (brand.name + ".app")
        validate_bundle(app, brand, version, identity)
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    finally:
        if mount.is_mount():
            subprocess.run(["/usr/bin/hdiutil", "detach", str(mount)], check=True)


def package_app(app, chromium_src, release_root):
    """현재 소스와 일치하는 앱·메타데이터를 한 테스트 패키지로 발행한다."""
    import dmgbuild

    brand, identity = load_brand(), load_identity()
    version, engine = version_inputs(chromium_src)
    version = version_string(version)
    info, resources = validate_bundle(app, brand, version, identity)
    args = (app.parent / "args.gn").read_text()
    if not re.search(r"(?m)^\s*is_component_build\s*=\s*false\b", args):
        raise ValueError("The macOS test package requires a static build")
    architecture = subprocess.check_output(
        ["/usr/bin/lipo", "-archs", str(app / "Contents/MacOS" / brand.name)], text=True).split()
    architectures = {("arm64",): "arm64", ("x86_64",): "x64",
                     ("arm64", "x86_64"): "universal"}
    architecture = architectures.get(tuple(sorted(architecture)))
    if architecture is None:
        raise ValueError("Unsupported macOS app architecture")
    payloads = {filename: sha256(resources / filename) for filename in PAYLOAD_NAMES}
    source_executable = sha256(app / "Contents/MacOS" / brand.name)
    source_framework = sha256(resources.parent / f"{brand.name} Framework")
    source_commit = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"]).strip())
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    release_root.mkdir(parents=True, exist_ok=True)
    directory = release_root / f"macos-{version}-{stamp}"
    if directory.exists():
        raise ValueError(f"Package output already exists: {directory}")
    filename = f"{brand.name}-{version}-macos-{architecture}-test-adhoc.dmg"
    with tempfile.TemporaryDirectory(prefix=".macos-package-", dir=release_root) as temporary:
        staging = Path(temporary)
        copied_app = staging / app.name
        subprocess.run(["/usr/bin/ditto", str(app), str(copied_app)], check=True)
        validate_bundle(copied_app, brand, version, identity)
        prepare_installed_bundle(copied_app, identity)
        sign_bundle(copied_app)
        background = staging / "install-background.tiff"
        module_cache = release_root.parent / "cache/swift-packaging"
        module_cache.mkdir(parents=True, exist_ok=True)
        subprocess.run(["/usr/bin/xcrun", "swift", "-module-cache-path", str(module_cache),
                        str(Path(__file__).with_name("macos-dmg-background.swift")), str(background)], check=True)
        icon = app / "Contents/Resources" / info["CFBundleIconFile"]
        image = staging / filename
        dmgbuild.build_dmg(str(image), brand.name,
                           settings=dmg_settings(copied_app, background, icon))
        verify_image(image, staging / "verify-volume", brand, version, identity)
        metadata = {
            "schema_version": 1, "product_id": identity.product_id, "platform": "macos",
            "architecture": architecture, "channel": "test", "version": version,
            "chromium_version": version_string(engine), "source_commit": source_commit,
            "working_tree_dirty": dirty, "built_at_utc": stamp, "installer": filename,
            "sha256": sha256(image), "size_bytes": image.stat().st_size,
            "signature": "NotSigned", "app_signature": "AdHoc",
            "notarized": False, "component_build": False,
            "profile_directory": identity.profile_directory, "application_name": brand.name,
            "macos_identity": asdict(identity.macos), "source_bundle_id": info["CFBundleIdentifier"],
            "source_executable_sha256": source_executable, "payload_sha256": payloads,
            "source_framework_sha256": source_framework,
        }
        directory.mkdir()
        image.replace(directory / filename)
        shutil.copyfile(ROOT / "docs/macos-test-release.md", directory / "README.ko.md")
        (directory / "release.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"macOS test package: {directory}")
    print(f"Installer: {directory / filename}")
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--chromium-src", type=Path, required=True)
    parser.add_argument("--release-root", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("이 도구는 macOS에서 실행한다")
    try:
        package_app(args.app.resolve(), args.chromium_src.resolve(), args.release_root.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"macOS package failed: {error}\n")


if __name__ == "__main__":
    main()
