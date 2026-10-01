// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/document_engine.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/sequence_bound.h"
#include "base/trace_event/trace_event.h"
#include "components/yee_tasks/worker_owned.h"

namespace yee::content_blocking {
namespace {
using WorkerEngine = yee::tasks::WorkerOwned<Engine>;
}  // namespace

struct DocumentEngine::Impl {
  explicit Impl(base::OnceCallback<std::unique_ptr<Engine>()> create_engine)
      : worker(base::ThreadPool::CreateSingleThreadTaskRunner(
                   {base::TaskPriority::USER_BLOCKING,
                    base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
                   base::SingleThreadTaskRunnerThreadMode::DEDICATED),
               std::move(create_engine)) {}
  base::SequenceBound<WorkerEngine> worker;
};

DocumentEngine::DocumentEngine()
    : DocumentEngine(base::BindOnce(&CreateBundledEngine)) {}
DocumentEngine::DocumentEngine(
    base::OnceCallback<std::unique_ptr<Engine>()> create_engine)
    : impl_(std::make_unique<Impl>(std::move(create_engine))) {}
DocumentEngine::~DocumentEngine() = default;

PageRules DocumentEngine::RulesForPage(std::string url) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.WaitForDocumentRules");
  PageRules result;
  base::WaitableEvent ready;
  // FIFO execution waits for construction too. Signal/Wait synchronize the
  // result and stack lifetime. BLOCK_SHUTDOWN keeps accepted work from being
  // skipped while the caller is waiting. The worker has no caller dependency.
  impl_->worker.AsyncCall(&WorkerEngine::Run<void>)
      .WithArgs(base::BindOnce(
          [](std::string url, PageRules* result, base::WaitableEvent* ready,
             Engine& engine) {
            *result = engine.RulesForPage(url);
            ready->Signal();
          },
          std::move(url), base::Unretained(&result), base::Unretained(&ready)));
  ready.Wait();
  return result;
}

void DocumentEngine::GenericSelectors(
    std::vector<std::string> classes,
    std::vector<std::string> ids,
    std::vector<std::string> exceptions,
    base::OnceCallback<void(std::vector<std::string>)> reply) {
  impl_->worker.AsyncCall(&WorkerEngine::Run<std::vector<std::string>>)
      .WithArgs(base::BindOnce(
          [](std::vector<std::string> classes, std::vector<std::string> ids,
             std::vector<std::string> exceptions, Engine& engine) {
            return engine.GenericSelectors(classes, ids, exceptions);
          },
          std::move(classes), std::move(ids), std::move(exceptions)))
      .Then(std::move(reply));
}

DocumentEngine& RendererDocumentEngine() {
  static base::NoDestructor<DocumentEngine> engine;
  return *engine;
}
}  // namespace yee::content_blocking
