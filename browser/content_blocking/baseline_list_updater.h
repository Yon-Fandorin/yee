// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_BASELINE_LIST_UPDATER_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_BASELINE_LIST_UPDATER_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/yee_content_blocking/filter_list_store.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network
namespace yee::content_blocking {
struct BaselineListUpdateStatus {
  bool available = false;
  bool in_flight = false;
  bool downloaded = false;
  bool running_downloaded = false;
  bool pending_restart = false;
  bool recovered = false;
  base::Time checked_at;
  std::vector<FilterSubscription> subscriptions;
  std::vector<std::string> failed_urls;
};

// One browser process coordinator, owned by a regular profile. System network
// requests omit cookies. Disk IO/validation never execute on the UI thread.
class BaselineListUpdater {
 public:
  BaselineListUpdater(
      base::FilePath directory,
      scoped_refptr<network::SharedURLLoaderFactory> factory,
      std::string running_generation,
      base::Time checked_at,
      scoped_refptr<base::SequencedTaskRunner> worker = nullptr);
  ~BaselineListUpdater();
  BaselineListUpdater(const BaselineListUpdater&) = delete;
  BaselineListUpdater& operator=(const BaselineListUpdater&) = delete;
  static std::unique_ptr<BaselineListUpdater> MaybeCreate();
  static void SetNetworkFactoryProvider(
      base::RepeatingCallback<scoped_refptr<network::SharedURLLoaderFactory>()>
          provider);
  void Start();
  // Joins an existing download or checks immediately, bypassing the daily
  // timer.
  void CheckNow(base::OnceCallback<void(bool)> callback);
  static bool RequestUpdate(base::OnceCallback<void(bool)> callback);
  static void GetStatus(
      base::OnceCallback<void(BaselineListUpdateStatus)> callback);
  static base::CallbackListSubscription AddChangedCallback(
      base::RepeatingClosure callback);
  static bool AddSubscription(std::string url,
                              base::OnceCallback<void(std::string)> callback);
  static bool ChangeSubscription(
      std::string url,
      std::optional<bool> enabled,
      base::OnceCallback<void(std::string)> callback);
  void AddSubscriptionNow(std::string url,
                          base::OnceCallback<void(std::string)> callback);
  void ChangeSubscriptionNow(std::string url,
                             std::optional<bool> enabled,
                             base::OnceCallback<void(std::string)> callback);
  bool update_in_flight() const { return in_flight_; }

 private:
  void RefreshAndStart();
  void Refreshed(base::Time checked_at);
  void PlanLoaded(std::vector<FilterSubscription> subscriptions);
  void Download(size_t index);
  void Downloaded(size_t index, std::optional<std::string> body);
  void SubscriptionDownloaded(std::string url, std::optional<std::string> body);
  void MutationCompleted(std::string url, std::string error);
  bool BeginMutation(base::OnceCallback<void(std::string)> callback);
  bool ResponseComplete() const;
  void CreateLoader(const std::string& url, bool subscription);
  void UpdatesInstalled(FilterListUpdateResult result);
  void Completed(bool installed);

  const base::FilePath directory_;
  const scoped_refptr<network::SharedURLLoaderFactory> factory_;
  const std::string running_generation_;
  base::Time checked_at_;
  const scoped_refptr<base::SequencedTaskRunner> worker_;
  base::OneShotTimer timer_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  std::array<std::string, 2> originals_;
  size_t download_index_ = 0;
  std::vector<FilterListDownload> downloads_;
  std::vector<std::string> failed_urls_;
  base::OnceCallback<void(std::string)> mutation_callback_;
  bool in_flight_ = false;
  std::vector<base::OnceCallback<void(bool)>> completion_callbacks_;
  base::WeakPtrFactory<BaselineListUpdater> weak_factory_{this};
};
}  // namespace yee::content_blocking
#endif
