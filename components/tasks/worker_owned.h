// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_TASKS_WORKER_OWNED_H_
#define COMPONENTS_YEE_TASKS_WORKER_OWNED_H_

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/callback.h"
#include "base/sequence_checker.h"

namespace yee::tasks {

// A factory adapter for base::SequenceBound. Construction, operations and
// destruction stay on its bound sequence, including objects that cannot be
// constructed on the caller and then transferred to a worker.
//
// Use a dedicated SingleThreadTaskRunner when T needs a physical thread.
// Task priority, shutdown behavior, reply cancellation and queue limits belong
// to the consumer. Operations must not access UI/frame objects; returned data
// must be safe to use on the reply sequence.
template <typename T>
class WorkerOwned {
 public:
  explicit WorkerOwned(base::OnceCallback<std::unique_ptr<T>()> create)
      : object_(std::move(create).Run()) {
    CHECK(object_);
  }
  ~WorkerOwned() { DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_); }
  WorkerOwned(const WorkerOwned&) = delete;
  WorkerOwned& operator=(const WorkerOwned&) = delete;

  // SequenceBound::AsyncCall(&WorkerOwned<T>::Run<Result>) accepts an owned
  // operation; Then() delivers its result on the caller sequence. Bind a weak
  // receiver to Then() when the consumer may disappear before the reply.
  template <typename Result>
  Result Run(base::OnceCallback<Result(T&)> operation) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return std::move(operation).Run(*object_);
  }

 private:
  SEQUENCE_CHECKER(sequence_checker_);
  std::unique_ptr<T> object_;
};

}  // namespace yee::tasks
#endif
