// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/document_engine.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {
std::unique_ptr<Engine> FixtureEngine() {
  return std::make_unique<Engine>(
      "page.test##.site-ad\n##.generic-ad\n"
      "page.test#@#.generic-ad\n@@||disabled.test^$generichide\n");
}

class SelectorReceiver {
 public:
  explicit SelectorReceiver(bool* called) : called_(called) {}
  void Receive(std::vector<std::string>) { *called_ = true; }
  base::WeakPtr<SelectorReceiver> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
 private:
  raw_ptr<bool> called_;
  base::WeakPtrFactory<SelectorReceiver> weak_factory_{this};
};
}  // namespace

TEST(DocumentEngine, FirstDocumentWaitsForWorkerConstruction) {
  base::test::TaskEnvironment tasks;
  base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
  const auto caller = base::PlatformThread::CurrentRef();
  base::PlatformThreadRef created_on;
  int constructions = 0;
  DocumentEngine engine(base::BindOnce([](base::PlatformThreadRef* created_on,
                                         int* constructions) {
    *created_on = base::PlatformThread::CurrentRef();
    ++*constructions;
    return FixtureEngine();
  }, base::Unretained(&created_on), base::Unretained(&constructions)));
  const auto rules = engine.RulesForPage("https://page.test/");
  EXPECT_NE(created_on, caller);
  EXPECT_EQ(constructions, 1);
  EXPECT_EQ(rules.selectors, std::vector<std::string>{".site-ad"});
  EXPECT_EQ(rules.exceptions, std::vector<std::string>{".generic-ad"});
  EXPECT_TRUE(rules.generic_hide);
  EXPECT_FALSE(engine.RulesForPage("https://disabled.test/").generic_hide);
  EXPECT_EQ(constructions, 1);
}

TEST(DocumentEngine, GenericRepliesRespectExceptionsAndCallerSequence) {
  base::test::TaskEnvironment tasks;
  DocumentEngine engine(base::BindOnce(&FixtureEngine));
  const auto caller = base::PlatformThread::CurrentRef();
  base::test::TestFuture<std::vector<std::string>> visible;
  engine.GenericSelectors({"generic-ad"}, {}, {},
      base::BindOnce([](base::PlatformThreadRef caller,
                       base::test::TestFuture<std::vector<std::string>>* future,
                       std::vector<std::string> selectors) {
        EXPECT_EQ(base::PlatformThread::CurrentRef(), caller);
        future->SetValue(std::move(selectors));
      }, caller, base::Unretained(&visible)));
  EXPECT_EQ(visible.Get(), std::vector<std::string>{".generic-ad"});
  base::test::TestFuture<std::vector<std::string>> excepted;
  engine.GenericSelectors({"generic-ad"}, {}, {".generic-ad"},
                          excepted.GetCallback());
  EXPECT_TRUE(excepted.Get().empty());
}

TEST(DocumentEngine, CancelledReceiverDoesNotApplyDelayedSelectors) {
  base::test::TaskEnvironment tasks;
  DocumentEngine engine(base::BindOnce(&FixtureEngine));
  bool called = false;
  auto receiver = std::make_unique<SelectorReceiver>(&called);
  engine.GenericSelectors({"generic-ad"}, {}, {},
      base::BindOnce(&SelectorReceiver::Receive, receiver->GetWeakPtr()));
  receiver.reset();
  tasks.RunUntilIdle();
  EXPECT_FALSE(called);
}
}  // namespace yee::content_blocking
