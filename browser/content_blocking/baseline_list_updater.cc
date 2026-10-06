// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"
#include <algorithm>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "components/yee_content_blocking/baseline_list_store.h"
#include "components/yee_content_blocking/settings.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace yee::content_blocking {
namespace {
base::WeakPtr<BaselineListUpdater>& Coordinator() {
  static base::NoDestructor<base::WeakPtr<BaselineListUpdater>> value;
  return *value;
}
base::RepeatingClosureList& StatusCallbacks() {
  static base::NoDestructor<base::RepeatingClosureList> callbacks;
  return *callbacks;
}
base::RepeatingCallback<scoped_refptr<network::SharedURLLoaderFactory>()>&
NetworkFactoryProvider() {
  static base::NoDestructor<
      base::RepeatingCallback<scoped_refptr<network::SharedURLLoaderFactory>()>>
      provider;
  return *provider;
}
constexpr base::TimeDelta kUpdateInterval = base::Days(1);
scoped_refptr<base::SequencedTaskRunner> StoreWorker() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN}));
  return *runner;
}
}  // namespace

BaselineListUpdater::BaselineListUpdater(
    base::FilePath directory,
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    std::string running_generation,
    base::Time checked_at,
    scoped_refptr<base::SequencedTaskRunner> worker)
    : directory_(std::move(directory)),
      factory_(std::move(factory)),
      running_generation_(std::move(running_generation)),
      checked_at_(checked_at),
      worker_(worker
                  ? std::move(worker)
                  : base::ThreadPool::CreateSequencedTaskRunner(
                        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
                         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})) {}
BaselineListUpdater::~BaselineListUpdater() {
  weak_factory_.InvalidateWeakPtrs();
  StatusCallbacks().Notify();
  for (auto& callback : completion_callbacks_) {
    std::move(callback).Run(false);
  }
}

base::CallbackListSubscription BaselineListUpdater::AddChangedCallback(
    base::RepeatingClosure callback) {
  return StatusCallbacks().Add(std::move(callback));
}

void BaselineListUpdater::CheckNow(base::OnceCallback<void(bool)> callback) {
  completion_callbacks_.push_back(std::move(callback));
  if (in_flight_) {
    return;
  }
  timer_.Stop();
  checked_at_ = {};
  Start();
}

bool BaselineListUpdater::RequestUpdate(
    base::OnceCallback<void(bool)> callback) {
  if (!Coordinator()) {
    return false;
  }
  Coordinator()->CheckNow(std::move(callback));
  return true;
}

void BaselineListUpdater::GetStatus(
    base::OnceCallback<void(BaselineListUpdateStatus)> callback) {
  const auto directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          kBaselineListDirectorySwitch);
  BaselineListUpdateStatus status;
  status.running_downloaded = !BaselineLists().generation.empty();
  status.available = !!Coordinator();
  status.in_flight = Coordinator() && Coordinator()->update_in_flight();
  StoreWorker()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath directory, std::string running_generation,
             BaselineListUpdateStatus status) {
            const auto saved = ReadBaselineListStore(directory);
            status.downloaded = !saved.generation.empty();
            status.pending_restart =
                status.downloaded && saved.generation != running_generation;
            status.recovered = saved.recovered;
            status.checked_at = saved.checked_at;
            return status;
          },
          directory, BaselineLists().generation, status),
      std::move(callback));
}
void BaselineListUpdater::SetNetworkFactoryProvider(
    base::RepeatingCallback<scoped_refptr<network::SharedURLLoaderFactory>()>
        provider) {
  NetworkFactoryProvider() = std::move(provider);
}

std::unique_ptr<BaselineListUpdater> BaselineListUpdater::MaybeCreate() {
  if (Coordinator() || !NetworkFactoryProvider() ||
      !base::FeatureList::IsEnabled(kYeeContentBlocking) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          "disable-background-networking"))
    return nullptr;
  const auto directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          kBaselineListDirectorySwitch);
  if (directory.empty())
    return nullptr;
  auto updater = std::make_unique<BaselineListUpdater>(
      directory, NetworkFactoryProvider().Run(), BaselineLists().generation,
      BaselineLists().checked_at, StoreWorker());
  Coordinator() = updater->weak_factory_.GetWeakPtr();
  // Avoid competing with the first tab's critical loading work.
  updater->timer_.Start(FROM_HERE, base::Seconds(30),
                        base::BindOnce(&BaselineListUpdater::RefreshAndStart,
                                       updater->weak_factory_.GetWeakPtr()));
  return updater;
}
void BaselineListUpdater::RefreshAndStart() {
  // Another regular profile may have completed an update before its service
  // was destroyed. Read the latest persisted time on the same store sequence.
  worker_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath path) {
            return ReadBaselineListStore(path).checked_at;
          },
          directory_),
      base::BindOnce(&BaselineListUpdater::Refreshed,
                     weak_factory_.GetWeakPtr()));
}
void BaselineListUpdater::Refreshed(base::Time checked_at) {
  checked_at_ = checked_at;
  Start();
}

void BaselineListUpdater::Start() {
  if (in_flight_)
    return;
  const auto remaining = checked_at_ + kUpdateInterval - base::Time::Now();
  if (!checked_at_.is_null() && remaining > base::TimeDelta()) {
    // Bound a future wall clock timestamp; it must not disable updates forever.
    timer_.Start(FROM_HERE, std::min(remaining, kUpdateInterval),
                 base::BindOnce(&BaselineListUpdater::Start,
                                weak_factory_.GetWeakPtr()));
    if (remaining > kUpdateInterval)
      checked_at_ = base::Time::Now();
    return;
  }
  in_flight_ = true;
  Download(0);
  StatusCallbacks().Notify();
}
void BaselineListUpdater::Download(size_t index) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kBaselineListURLs[index]);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_BYPASS_CACHE;
  constexpr auto annotation =
      net::DefineNetworkTrafficAnnotation("yee_baseline_filter_update", R"(
        semantics {
          sender: "Yee content blocking"
          description: "Downloads official EasyList and EasyPrivacy rules. A validated pair is applied on the next browser start; previous working rules remain available."
          trigger: "Thirty seconds after a regular profile starts, then once daily, or when the user checks in Settings. Failed updates retry after six hours."
          data: "No user content, cookies or credentials."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Runs with Yee content blocking enabled. Chromium's disable-background-networking switch prevents updates."
          policy_exception_justification: "No enterprise-specific policy is implemented for Yee content blocking."
        })");
  loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  loader_->SetTimeoutDuration(base::Seconds(30));
  loader_->DownloadToString(factory_.get(),
                            base::BindOnce(&BaselineListUpdater::Downloaded,
                                           weak_factory_.GetWeakPtr(), index),
                            kMaxBaselineListBytes);
}
void BaselineListUpdater::Downloaded(size_t index,
                                     std::optional<std::string> body) {
  const bool complete_response =
      loader_->NetError() == net::OK && loader_->ResponseInfo() &&
      loader_->ResponseInfo()->headers &&
      loader_->ResponseInfo()->headers->response_code() == net::HTTP_OK;
  loader_.reset();
  if (!body || !complete_response) {
    Completed(false);
    return;
  }
  originals_[index] = std::move(*body);
  if (index == 0) {
    Download(1);
    return;
  }
  worker_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&InstallBaselineLists, directory_, std::move(originals_),
                     base::Time::Now(), running_generation_),
      base::BindOnce(&BaselineListUpdater::Completed,
                     weak_factory_.GetWeakPtr()));
}
void BaselineListUpdater::Completed(bool installed) {
  in_flight_ = false;
  originals_ = {};
  if (installed)
    checked_at_ = base::Time::Now();
  timer_.Start(
      FROM_HERE, installed ? kUpdateInterval : base::Hours(6),
      base::BindOnce(&BaselineListUpdater::Start, weak_factory_.GetWeakPtr()));
  auto callbacks = std::move(completion_callbacks_);
  StatusCallbacks().Notify();
  for (auto& callback : callbacks) {
    std::move(callback).Run(installed);
  }
}
}  // namespace yee::content_blocking
