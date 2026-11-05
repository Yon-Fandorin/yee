// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/mac/install_from_dmg.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class YeeDiskImageInstallTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temporary_.CreateUniqueTempDir());
    source_ = temporary_.GetPath().AppendASCII("source");
    bundle_ = source_.AppendASCII("YeeDiskImageTest.app");
    ASSERT_TRUE(base::CreateDirectory(bundle_.AppendASCII("Contents")));
    ASSERT_TRUE(base::WriteFile(
        bundle_.AppendASCII("Contents/Info.plist"),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
        "<plist version=\"1.0\"><dict>"
        "<key>CFBundleIdentifier</key><string>org.yee.disk-image-test</string>"
        "<key>CFBundlePackageType</key><string>APPL</string>"
        "</dict></plist>"));
  }

  void TearDown() override {
    base::apple::SetOverrideOuterBundlePath(base::FilePath());
    if (!image_.empty()) {
      EXPECT_TRUE(DetachOwnedImage());
    }
  }

  bool MountImage(bool read_only) {
    image_ = base::MakeAbsoluteFilePath(temporary_.GetPath())
                 .AppendASCII("fixture with spaces.dmg");
    if (!RunHdiutil({"create", "-fs", "HFS+", "-format", "UDRW", "-volname",
                     "YeeDiskImageTest", "-srcfolder", source_.value(),
                     image_.value()})) {
      return false;
    }
    mount_ = temporary_.GetPath().AppendASCII("volume");
    if (!base::CreateDirectory(mount_)) {
      return false;
    }
    std::vector<std::string> arguments = {"attach", "-nobrowse", "-noautoopen"};
    if (read_only) {
      arguments.push_back("-readonly");
    }
    arguments.insert(arguments.end(),
                     {"-mountpoint", mount_.value(), image_.value()});
    const bool mounted = RunHdiutil(arguments);
    if (mounted) {
      bundle_ = mount_.AppendASCII("YeeDiskImageTest.app");
    }
    return mounted;
  }

  bool DetachOwnedImage() {
    // 추출 실패로 마운트 지점만 사라져도, 현재 검사 파일의 연결을 찾아
    // 정리한다.
    std::string output;
    if (!RunHdiutil({"info", "-plist"}, &output)) {
      return false;
    }
    base::apple::ScopedCFTypeRef<CFDataRef> data(CFDataCreate(
        nullptr, reinterpret_cast<const UInt8*>(output.data()), output.size()));
    base::apple::ScopedCFTypeRef<CFPropertyListRef> plist(
        CFPropertyListCreateWithData(
            nullptr, data.get(), kCFPropertyListImmutable, nullptr, nullptr));
    auto root = base::apple::CFCast<CFDictionaryRef>(plist.get());
    auto images = root ? base::apple::CFCast<CFArrayRef>(
                             CFDictionaryGetValue(root, CFSTR("images")))
                       : nullptr;
    if (!images) {
      return false;
    }
    for (CFIndex i = 0; i < CFArrayGetCount(images); ++i) {
      auto image = base::apple::CFCast<CFDictionaryRef>(
          CFArrayGetValueAtIndex(images, i));
      if (!image) {
        continue;
      }
      auto path = base::apple::CFCast<CFStringRef>(
          CFDictionaryGetValue(image, CFSTR("image-path")));
      if (!path || base::SysCFStringRefToUTF8(path) != image_.value()) {
        continue;
      }
      auto entities = base::apple::CFCast<CFArrayRef>(
          CFDictionaryGetValue(image, CFSTR("system-entities")));
      if (!entities) {
        return false;
      }
      for (CFIndex j = 0; j < CFArrayGetCount(entities); ++j) {
        auto entity = base::apple::CFCast<CFDictionaryRef>(
            CFArrayGetValueAtIndex(entities, j));
        auto device =
            entity ? base::apple::CFCast<CFStringRef>(
                         CFDictionaryGetValue(entity, CFSTR("dev-entry")))
                   : nullptr;
        if (device) {
          return RunHdiutil({"detach", base::SysCFStringRefToUTF8(device)});
        }
      }
      return false;
    }
    return true;
  }

  bool RunHdiutil(const std::vector<std::string>& arguments,
                  std::string* captured_output = nullptr) {
    base::CommandLine command(base::FilePath("/usr/bin/hdiutil"));
    for (const auto& argument : arguments) {
      command.AppendArg(argument);
    }
    std::string output;
    int exit_code = -1;
    const bool launched =
        base::GetAppOutputWithExitCode(command, &output, &exit_code);
    if (!launched || exit_code != 0) {
      ADD_FAILURE() << "hdiutil failed (" << exit_code << "): " << output;
      return false;
    }
    if (captured_output) {
      *captured_output = std::move(output);
    }
    return true;
  }

  base::ScopedTempDir temporary_;
  base::FilePath source_;
  base::FilePath bundle_;
  base::FilePath mount_;
  base::FilePath image_;
};

TEST_F(YeeDiskImageInstallTest, ReadOnlyImageIsAnInstallSource) {
  // hdiutil의 실제 IOMedia 조상을 통해 설치 디스크를 인식해야 한다.
  ASSERT_TRUE(MountImage(/*read_only=*/true));
  base::apple::SetOverrideOuterBundlePath(bundle_);
  auto result = IsAppRunningFromReadOnlyDiskImage();
  EXPECT_EQ(result.status, DiskImageStatus::kRunningFromDiskImage);
  EXPECT_FALSE(result.dmg_bsd_device_name.empty());
}

TEST_F(YeeDiskImageInstallTest, WritableImageDoesNotTriggerInstallation) {
  // 편집 중인 쓰기 가능한 이미지에서는 설치를 제안하지 않는다.
  ASSERT_TRUE(MountImage(/*read_only=*/false));
  base::apple::SetOverrideOuterBundlePath(bundle_);
  EXPECT_EQ(IsAppRunningFromReadOnlyDiskImage().status,
            DiskImageStatus::kNotRunningFromDiskImage);
}

TEST_F(YeeDiskImageInstallTest, OrdinaryBundleDoesNotTriggerInstallation) {
  base::apple::SetOverrideOuterBundlePath(bundle_);
  EXPECT_EQ(IsAppRunningFromReadOnlyDiskImage().status,
            DiskImageStatus::kNotRunningFromDiskImage);
}

TEST_F(YeeDiskImageInstallTest, EjectsAndTrashesOwnedImage) {
  // 실제 image-path를 읽어 검사에서 만든 이미지 하나만 추출하고 휴지통으로
  // 옮긴다.
  ASSERT_TRUE(MountImage(/*read_only=*/true));
  base::apple::SetOverrideOuterBundlePath(bundle_);
  auto result = IsAppRunningFromReadOnlyDiskImage();
  ASSERT_EQ(result.status, DiskImageStatus::kRunningFromDiskImage);
  ASSERT_FALSE(result.dmg_bsd_device_name.empty());
  EjectAndTrashDiskImage(result.dmg_bsd_device_name);
  EXPECT_FALSE(base::PathExists(bundle_));
  EXPECT_FALSE(base::PathExists(image_));
}

TEST(YeeInstallShTest, UpdaterBuildFlagControlsElevatedSteps) {
  // 실제 템플릿을 렌더하되 EUID 판정과 시스템 명령만 임시 fixture로 바꾼다.
  // 권한 승격이나 /Applications·/Library·LaunchServices 변경은 하지 않는다.
  base::FilePath source_root;
  ASSERT_TRUE(
      base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  for (bool updater_enabled : {false, true}) {
    SCOPED_TRACE(updater_enabled);
    base::ScopedTempDir temporary;
    ASSERT_TRUE(temporary.CreateUniqueTempDir());
    const auto root = temporary.GetPath();
    const auto commands = root.AppendASCII("commands");
    ASSERT_TRUE(base::CreateDirectory(commands));
    const auto spy = commands.AppendASCII("spy");
    ASSERT_TRUE(base::WriteFile(spy, R"script(#!/bin/bash
set -eu
name="$(basename "$0")"
printf '%s\n' "$name $*" >> "$YEE_INSTALL_TEST_LOG"
if [[ "$name" == "defaults" && "$1" == "read" ]]; then
  case "${@: -1}" in
    KSProductID) echo org.yee.install-fixture ;;
    KSVersion) echo 1 ;;
    KSChannelID) echo stable ;;
  esac
fi
)script"));
    ASSERT_TRUE(base::SetPosixFilePermissions(spy, 0755));
    for (const char* name :
         {"defaults", "chown", "xattr", "lsregister", "ksadmin"}) {
      ASSERT_TRUE(base::CreateSymbolicLink(spy, commands.AppendASCII(name)));
    }
    const auto source = root.AppendASCII("source with spaces.app");
    ASSERT_TRUE(base::CreateDirectory(source.AppendASCII("Contents")));
    ASSERT_TRUE(
        base::WriteFile(source.AppendASCII("Contents/Info.plist"), "fixture"));
    if (updater_enabled) {
      const auto updater = source.AppendASCII(
          "Contents/Frameworks/Yee Fixture Framework.framework/Helpers/"
          "FixtureUpdater.app/Contents/MacOS/FixtureUpdater");
      ASSERT_TRUE(base::CreateDirectory(updater.DirName()));
      ASSERT_TRUE(base::CopyFile(spy, updater));
      ASSERT_TRUE(base::SetPosixFilePermissions(updater, 0755));
    }
    const auto script = root.AppendASCII("install.sh");
    base::CommandLine render(base::FilePath("/usr/bin/python3"));
    // 중첩 인터프리터의 옵션이 script 경로 앞으로 재배치되지 않게 한다.
    for (const auto& argument : std::vector<std::string>{
             source_root.AppendASCII("build/util/version.py").value(), "-e",
             "SHEBANGGUARD=''", "-e",
             updater_enabled ? "ENABLE_UPDATER=1" : "ENABLE_UPDATER=0", "-e",
             "BROWSER_PRODUCT_NAME='Yee Fixture'", "-e",
             "UPDATER_PRODUCT_NAME='FixtureUpdater'", "-e",
             "UPDATER_COMPANY_SHORT_NAME='YeeFixture'", "-e",
             "KEYSTONE_APP_NAME='FixtureKeystone'", "-i",
             source_root.AppendASCII("chrome/browser/mac/install.sh.in")
                 .value(),
             "-o", script.value()}) {
      render.AppendArg(argument);
    }
    std::string output;
    int exit_code = -1;
    ASSERT_TRUE(base::GetAppOutputWithExitCode(render, &output, &exit_code));
    ASSERT_EQ(0, exit_code) << output;
    std::string contents;
    ASSERT_TRUE(base::ReadFileToString(script, &contents));
    ASSERT_EQ(std::string::npos, contents.find("@ENABLE_UPDATER@"));
    for (const char* token :
         {"${EUID}", "export PATH=\"/usr/bin:/usr/sbin:/bin:/sbin\"",
          "${LAUNCHSERVICES}/Support/lsregister",
          "/Library/YeeFixture/FixtureKeystone/FixtureKeystone.bundle/Contents/"
          "MacOS/ksadmin"}) {
      ASSERT_NE(std::string::npos, contents.find(token));
    }
    base::ReplaceSubstringsAfterOffset(&contents, 0, "${EUID}", "0");
    base::ReplaceSubstringsAfterOffset(
        &contents, 0, "export PATH=\"/usr/bin:/usr/sbin:/bin:/sbin\"",
        "export PATH=\"" + commands.value() +
            ":/usr/bin:/usr/sbin:/bin:/sbin\"");
    base::ReplaceSubstringsAfterOffset(
        &contents, 0, "${LAUNCHSERVICES}/Support/lsregister",
        commands.AppendASCII("lsregister").value());
    base::ReplaceSubstringsAfterOffset(
        &contents, 0,
        "/Library/YeeFixture/FixtureKeystone/FixtureKeystone.bundle/Contents/"
        "MacOS/ksadmin",
        commands.AppendASCII("ksadmin").value());
    ASSERT_TRUE(base::WriteFile(script, contents));
    const auto destination = root.AppendASCII("installed with spaces.app");
    const auto log = root.AppendASCII("commands.log");
    base::CommandLine install(base::FilePath("/usr/bin/env"));
    for (const auto& argument : std::vector<std::string>{
             "YEE_INSTALL_TEST_LOG=" + log.value(), "/bin/bash", "-p",
             script.value(), source.value(), destination.value()}) {
      install.AppendArg(argument);
    }
    ASSERT_TRUE(base::GetAppOutputWithExitCode(install, &output, &exit_code));
    EXPECT_EQ(0, exit_code) << output;
    EXPECT_TRUE(
        base::PathExists(destination.AppendASCII("Contents/Info.plist")));
    ASSERT_TRUE(base::ReadFileToString(log, &output));
    EXPECT_EQ(
        updater_enabled,
        output.find("FixtureUpdater --install --system") != std::string::npos);
    EXPECT_EQ(updater_enabled,
              output.find("ksadmin --register") != std::string::npos);
  }
}

}  // namespace
