// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_projects/project_service.h"

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::projects {
namespace {

class ProjectServiceTest : public testing::Test {
 protected:
  ProjectServiceTest() {
    ProjectService::RegisterProfilePrefs(prefs_.registry());
  }
  TestingPrefServiceSimple prefs_;
};

// 같은 URL인 두 탭은 서로 다른 항목이며, 같은 탭의 재연결은 멱등적이다.
TEST_F(ProjectServiceTest, IdentityAndOrderSurviveReload) {
  ProjectService service(&prefs_);
  const auto project =
      service.CreateProject(u"  자료 조사  ", u"자료와 근거를 모읍니다.");
  ASSERT_TRUE(project);
  const auto first =
      service.AttachPage(*project, GURL("https://example.com"), u"첫 탭");
  const auto second =
      service.AttachPage(*project, GURL("https://example.com"), u"둘째 탭");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(*first, *second);
  EXPECT_EQ(first, service.AttachPage(*project, GURL("https://example.com"),
                                      u"첫 탭", *first));
  ProjectService reloaded(&prefs_);
  ASSERT_TRUE(reloaded.writable());
  ASSERT_TRUE(reloaded.FindProject(*project));
  EXPECT_EQ(u"자료 조사", reloaded.FindProject(*project)->name);
  EXPECT_EQ(u"자료와 근거를 모읍니다.",
            reloaded.FindProject(*project)->description);
  EXPECT_EQ((std::vector<std::string>{*first, *second}),
            reloaded.FindProject(*project)->page_ids);
}

// 이동을 취소하거나 확인 당시 소유자가 달라지면 저장 내용이 바뀌지 않는다.
TEST_F(ProjectServiceTest, MoveRequiresConfirmedCurrentOwner) {
  ProjectService service(&prefs_);
  auto a = service.CreateProject(u"A");
  auto b = service.CreateProject(u"B");
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  auto page = service.AttachPage(*a, GURL("https://example.com"), u"페이지");
  ASSERT_TRUE(page);
  auto before = prefs_.GetDict(ProjectService::kPrefName).Clone();
  EXPECT_FALSE(service.AttachPage(*b, GURL("https://example.com"), u"페이지",
                                  *page, *a, false));
  EXPECT_FALSE(service.AttachPage(*b, GURL("https://example.com"), u"페이지",
                                  *page, *b, true));
  EXPECT_EQ(before, prefs_.GetDict(ProjectService::kPrefName));
  EXPECT_EQ(page, service.AttachPage(*b, GURL("https://example.com"), u"페이지",
                                     *page, *a, true));
  EXPECT_TRUE(service.FindProject(*a)->page_ids.empty());
  EXPECT_EQ((std::vector<std::string>{*page}),
            service.FindProject(*b)->page_ids);
  ProjectService reloaded(&prefs_);
  EXPECT_EQ(*b, reloaded.FindPage(*page)->project_id);
}

// 내부 페이지나 자격 증명이 포함된 URL은 이전의 유효한 웹 참조를 덮지 않는다.
TEST_F(ProjectServiceTest, KeepsLastValidCommittedPage) {
  ProjectService service(&prefs_);
  auto project = service.CreateProject(u"A");
  ASSERT_TRUE(project);
  EXPECT_FALSE(
      service.AttachPage(*project, GURL("chrome://settings"), u"설정"));
  EXPECT_FALSE(service.AttachPage(
      *project, GURL("https://user:pass@example.com"), u"비공개"));
  auto page =
      service.AttachPage(*project, GURL("https://example.com/start"), u"처음");
  ASSERT_TRUE(page);
  EXPECT_FALSE(service.UpdatePage(*page, GURL("file:///tmp/test"), u"파일"));
  EXPECT_EQ(GURL("https://example.com/start"), service.FindPage(*page)->url);
  EXPECT_TRUE(
      service.UpdatePage(*page, GURL("https://example.com/next"), u"다음"));
  EXPECT_EQ(u"다음", service.FindPage(*page)->title);
}

// 참조 제거와 프로젝트 삭제는 다른 프로젝트의 항목을 변경하지 않는다.
TEST_F(ProjectServiceTest, DeleteOnlyOwnedReferences) {
  ProjectService service(&prefs_);
  auto a = service.CreateProject(u"A");
  auto b = service.CreateProject(u"B");
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  auto first = service.AttachPage(*a, GURL("https://example.com/a"), u"A");
  auto second = service.AttachPage(*b, GURL("https://example.com/b"), u"B");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_TRUE(service.RemovePage(*first));
  EXPECT_TRUE(service.FindProject(*a)->page_ids.empty());
  EXPECT_TRUE(service.DeleteProject(*a));
  EXPECT_TRUE(service.FindPage(*second));
  EXPECT_FALSE(service.FindPage(*first));
  EXPECT_FALSE(service.RenameProject(*b, u"  "));
}

// 미래 버전과 잘못된 참조는 읽기 전용으로 남겨 저장 원본을 보존한다.
TEST_F(ProjectServiceTest, PreservesUnsupportedOrInvalidState) {
  base::DictValue future;
  future.Set("schema_version", 100);
  future.Set("future_data", "keep");
  prefs_.SetDict(ProjectService::kPrefName, future.Clone());
  ProjectService unknown(&prefs_);
  EXPECT_FALSE(unknown.writable());
  EXPECT_FALSE(unknown.CreateProject(u"덮어쓰기"));
  EXPECT_EQ(future, prefs_.GetDict(ProjectService::kPrefName));
  future.Set("schema_version", 1);
  future.Set("projects", base::ListValue().Append("invalid"));
  future.Set("pages", base::DictValue());
  prefs_.SetDict(ProjectService::kPrefName, future.Clone());
  ProjectService invalid(&prefs_);
  EXPECT_FALSE(invalid.writable());
  EXPECT_EQ(future, prefs_.GetDict(ProjectService::kPrefName));
}

// 등록 타입과 다른 사용자 값이 기본 빈 사전으로 읽혀도 원본을 덮지 않는다.
TEST_F(ProjectServiceTest, PreservesWrongRootTypeAndExplicitEmptyDictionary) {
  for (auto value : {base::Value::Type::STRING, base::Value::Type::LIST,
                     base::Value::Type::DICT}) {
    base::Value original(value);
    prefs_.SetUserPref(ProjectService::kPrefName, original.Clone());
    ProjectService service(&prefs_);
    EXPECT_FALSE(service.writable());
    EXPECT_FALSE(service.CreateProject(u"덮어쓰기"));
    ASSERT_TRUE(prefs_.GetUserPref(ProjectService::kPrefName));
    EXPECT_EQ(original, *prefs_.GetUserPref(ProjectService::kPrefName));
  }
}

// 선택 설명 필드도 키가 존재하면 문자열이어야 하며 다른 타입은 보존한다.
TEST_F(ProjectServiceTest, PreservesMalformedOptionalDescription) {
  ProjectService initial(&prefs_);
  ASSERT_TRUE(initial.CreateProject(u"자료"));
  auto state = prefs_.GetDict(ProjectService::kPrefName).Clone();
  state.FindList("projects")->front().GetDict().Set("description", 123);
  prefs_.SetDict(ProjectService::kPrefName, state.Clone());
  ProjectService malformed(&prefs_);
  EXPECT_FALSE(malformed.writable());
  EXPECT_FALSE(malformed.CreateProject(u"덮어쓰기"));
  EXPECT_EQ(state, prefs_.GetDict(ProjectService::kPrefName));
}

}  // namespace
}  // namespace yee::projects
