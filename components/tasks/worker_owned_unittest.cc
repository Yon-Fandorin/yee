// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_tasks/worker_owned.h"

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequence_bound.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::tasks {
namespace {
class Counter {
 public:
  explicit Counter(base::OnceClosure destroyed = {})
      : destroyed_(std::move(destroyed)) {}
  ~Counter() {
    if (destroyed_)
      std::move(destroyed_).Run();
  }
  int value = 0;

 private:
  base::OnceClosure destroyed_;
};
using WorkerCounter = WorkerOwned<Counter>;

auto Runner() {
  return base::ThreadPool::CreateSingleThreadTaskRunner(
      {base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::SingleThreadTaskRunnerThreadMode::DEDICATED);
}
}  // namespace

TEST(WorkerOwned, ConstructsUsesAndDestroysOnWorkerInOrder) {
  base::test::TaskEnvironment tasks;
  const auto caller = base::PlatformThread::CurrentRef();
  base::PlatformThreadRef created_on, destroyed_on;
  base::SequenceBound<WorkerCounter> worker(
      Runner(),
      base::BindOnce(
          [](base::PlatformThreadRef* created_on,
             base::PlatformThreadRef* destroyed_on) {
            *created_on = base::PlatformThread::CurrentRef();
            return std::make_unique<Counter>(base::BindOnce(
                [](base::PlatformThreadRef* destroyed_on) {
                  *destroyed_on = base::PlatformThread::CurrentRef();
                },
                base::Unretained(destroyed_on)));
          },
          base::Unretained(&created_on), base::Unretained(&destroyed_on)));
  // The first operation also waits for queued construction. The second
  // observes the same object and the effects of the first operation.
  worker.AsyncCall(&WorkerCounter::Run<void>)
      .WithArgs(base::BindOnce([](Counter& counter) { counter.value += 7; }));
  base::test::TestFuture<int> result;
  worker.AsyncCall(&WorkerCounter::Run<int>)
      .WithArgs(base::BindOnce([](Counter& counter) {
        counter.value += 3;
        return counter.value;
      }))
      .Then(result.GetCallback());
  EXPECT_EQ(result.Get(), 10);
  EXPECT_NE(created_on, caller);
  worker.SynchronouslyResetForTest();
  EXPECT_EQ(destroyed_on, created_on);
}

TEST(WorkerOwned, MovesInputsAndResultsAcrossSequences) {
  base::test::TaskEnvironment tasks;
  base::SequenceBound<WorkerCounter> worker(
      Runner(), base::BindOnce([] { return std::make_unique<Counter>(); }));
  base::test::TestFuture<std::unique_ptr<int>> result;
  worker.AsyncCall(&WorkerCounter::Run<std::unique_ptr<int>>)
      .WithArgs(base::BindOnce(
          [](std::unique_ptr<int> input, Counter& counter) {
            counter.value += *input;
            return std::make_unique<int>(counter.value);
          },
          std::make_unique<int>(29)))
      .Then(result.GetCallback());
  EXPECT_EQ(*result.Get(), 29);
}

}  // namespace yee::tasks
