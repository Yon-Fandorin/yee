// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_container_host.h"

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/service_worker/embedded_worker_test_helper.h"
#include "content/browser/service_worker/service_worker_client.h"
#include "content/browser/service_worker/service_worker_consts.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_context_core_observer.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_host.h"
#include "content/browser/service_worker/service_worker_metrics.h"
#include "content/browser/service_worker/service_worker_register_job.h"
#include "content/browser/service_worker/service_worker_registration.h"
#include "content/browser/service_worker/service_worker_security_utils.h"
#include "content/browser/service_worker/service_worker_test_utils.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/common/content_navigation_policy.h"
#include "content/common/features.h"
#include "content/common/url_schemes.h"
#include "content/public/browser/child_process_host.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/origin_util.h"
#include "content/public/test/back_forward_cache_util.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_content_browser_client.h"
#include "content/public/test/test_content_client.h"
#include "content/public/test/test_utils.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/functions.h"
#include "net/base/net_errors.h"
#include "net/cookies/site_for_cookies.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-shared.h"
#include "services/network/test/test_url_loader_client.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_ancestor_frame_type.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_object.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration_options.mojom.h"
#include "url/origin.h"
#include "url/url_util.h"

namespace content {
namespace {
const char kServiceWorkerScheme[] = "i-can-use-service-worker";
class ServiceWorkerTestContentClient : public TestContentClient {
 public:
  void AddAdditionalSchemes(Schemes* schemes) override {
    schemes->service_worker_schemes.push_back(kServiceWorkerScheme);
  }
};
class YeeServiceWorkerContainerHostTest : public testing::Test {
 protected:
  YeeServiceWorkerContainerHostTest()
      : task_environment_(BrowserTaskEnvironment::IO_MAINLOOP) {
    SetContentClient(&test_content_client_);
    ReRegisterContentSchemesForTests();
  }
  void SetUp() override {
    old_content_browser_client_ =
        SetBrowserClientForTesting(&test_content_browser_client_);
    mojo::SetDefaultProcessErrorHandler(
        base::BindRepeating(&YeeServiceWorkerContainerHostTest::OnMojoError,
                            base::Unretained(this)));

    helper_ = std::make_unique<EmbeddedWorkerTestHelper>(base::FilePath());
    context_ = helper_->context()->AsWeakPtr();
    script_url_ = GURL("https://www.example.com/service_worker.js");

    blink::mojom::ServiceWorkerRegistrationOptions options1;
    options1.scope = GURL("https://www.example.com/");
    const blink::StorageKey key1 = blink::StorageKey::CreateFirstParty(
        url::Origin::Create(options1.scope));
    registration1_ = ServiceWorkerRegistration::Create(
        options1, key1, 1L, context_,
        blink::mojom::AncestorFrameType::kNormalFrame);
  }
  void TearDown() override {
    registration1_ = nullptr;
    helper_.reset();
    SetBrowserClientForTesting(old_content_browser_client_);
    mojo::SetDefaultProcessErrorHandler(base::NullCallback());
  }
  CommittedServiceWorkerClient PrepareServiceWorkerContainerHost(
      const GURL& document_url,
      const PolicyContainerPolicies& policies = PolicyContainerPolicies()) {
    return CommittedServiceWorkerClient(
        CreateServiceWorkerClient(context_.get(), document_url),
        GlobalRenderFrameHostId(helper_->mock_render_process_id(),
                                /*mock frame_routing_id=*/1),
        policies);
  }
  void ExpectControlledWorkerFactoryForwardsRequest(
      const PolicyContainerPolicies& policies) {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeature(
        blink::features::kServiceWorkerInDedicatedWorker);
    const GURL document_url("https://www.example.com/foo");
    const url::Origin origin = url::Origin::Create(document_url);
    auto service_worker_client =
        context_->service_worker_client_owner()
            .CreateServiceWorkerClientForWorker(
                helper_->mock_render_process_id(),
                ServiceWorkerClientInfo(blink::DedicatedWorkerToken()));
    service_worker_client->UpdateUrls(
        document_url, origin, blink::StorageKey::CreateFirstParty(origin));

    auto version = base::MakeRefCounted<ServiceWorkerVersion>(
        registration1_.get(), script_url_, blink::mojom::ScriptType::kClassic,
        1 /* version_id */,
        mojo::PendingRemote<storage::mojom::ServiceWorkerLiveVersionRef>(),
        context_, std::nullopt, std::nullopt, PolicyContainerPolicies());
    version->set_fetch_handler_type(
        ServiceWorkerVersion::FetchHandlerType::kNotSkippable);
    version->SetStatus(ServiceWorkerVersion::ACTIVATED);
    registration1_->SetActiveVersion(version);
    CommittedServiceWorkerClient committed_client(
        std::move(service_worker_client), policies);
    committed_client->SetControllerRegistration(
        registration1_, /*notify_controllerchange=*/false);
    ASSERT_EQ(version.get(), committed_client->controller());

    network::TestURLLoaderFactory terminal(/*observe_loader_requests=*/true);
    mojo::PendingRemote<network::mojom::URLLoaderFactory> downstream;
    terminal.Clone(downstream.InitWithNewPipeAndPassReceiver());
    mojo::Remote<network::mojom::URLLoaderFactory> wrapped;
    // 워커 fixture는 연결된 associated Mojo 파이프를 만들지 않으므로,
    // 허용 경로에서는 컨테이너 호스트를 직접 호출한다.
    committed_client.container_host().WrapSubresourceLoaderFactory(
        wrapped.BindNewPipeAndPassReceiver(), std::move(downstream));
    wrapped.FlushForTesting();
    ASSERT_TRUE(wrapped.is_connected());
    ASSERT_TRUE(bad_messages_.empty());

    network::ResourceRequest request;
    request.url = GURL("https://www.example.com/resource.js");
    request.request_initiator = origin;
    network::TestURLLoaderClient client;
    mojo::Remote<network::mojom::URLLoader> loader;
    wrapped->CreateLoaderAndStart(
        loader.BindNewPipeAndPassReceiver(), 7, 0, request,
        client.CreateRemote(),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
    wrapped.FlushForTesting();

    ASSERT_EQ(1u, terminal.total_requests());
    const auto* pending = terminal.GetPendingRequest(0);
    ASSERT_TRUE(pending);
    EXPECT_EQ(request.url, pending->request.url);
    EXPECT_EQ(request.request_initiator, pending->request.request_initiator);
    EXPECT_EQ(7, pending->request_id);
    ASSERT_TRUE(terminal.SimulateResponseForPendingRequest(request.url.spec(),
                                                           "local response"));
    client.RunUntilComplete();
    EXPECT_TRUE(client.has_received_response());
    EXPECT_EQ(net::OK, client.completion_status().error_code);
  }
  void OnMojoError(const std::string& error) { bad_messages_.push_back(error); }
  BrowserTaskEnvironment task_environment_;

  std::unique_ptr<EmbeddedWorkerTestHelper> helper_;
  base::WeakPtr<ServiceWorkerContextCore> context_;
  scoped_refptr<ServiceWorkerRegistration> registration1_;
  GURL script_url_;
  ServiceWorkerTestContentClient test_content_client_;
  TestContentBrowserClient test_content_browser_client_;
  raw_ptr<ContentBrowserClient> old_content_browser_client_ = nullptr;
  std::vector<std::string> bad_messages_;

 private:
  url::ScopedSchemeRegistryForTests scoped_registry_;
};

TEST_F(YeeServiceWorkerContainerHostTest,
       WrapSubresourceLoaderFactory_SandboxedDocumentRejected) {
  PolicyContainerPolicies policies;
  policies.sandbox_flags = network::mojom::WebSandboxFlags::kOrigin;
  CommittedServiceWorkerClient service_worker_client =
      PrepareServiceWorkerContainerHost(GURL("https://www.example.com/foo"),
                                        policies);

  // 유효한 factory 파이프를 보내 샌드박스 검사 자체가 거부하는지 확인한다.
  network::TestURLLoaderFactory terminal;
  mojo::PendingRemote<network::mojom::URLLoaderFactory> downstream;
  terminal.Clone(downstream.InitWithNewPipeAndPassReceiver());
  mojo::Remote<network::mojom::URLLoaderFactory> wrapped;
  ASSERT_TRUE(bad_messages_.empty());
  service_worker_client.host_remote()->WrapSubresourceLoaderFactory(
      wrapped.BindNewPipeAndPassReceiver(), std::move(downstream));
  service_worker_client.host_remote().FlushForTesting();
  wrapped.FlushForTesting();

  ASSERT_EQ(1u, bad_messages_.size());
  EXPECT_EQ(ServiceWorkerConsts::kBadMessageFromSandboxedClient,
            bad_messages_.front());
  EXPECT_FALSE(wrapped.is_connected());
  EXPECT_EQ(0u, terminal.total_requests());
}

TEST_F(YeeServiceWorkerContainerHostTest,
       WrapSubresourceLoaderFactory_ControlledWorkerAllowed) {
  // 샌드박스가 없는 제어된 워커의 요청과 응답이 factory를 통과해야 한다.
  ExpectControlledWorkerFactoryForwardsRequest(PolicyContainerPolicies());
}

TEST_F(YeeServiceWorkerContainerHostTest,
       WrapSubresourceLoaderFactory_ControlledWorkerWithAllowSameOrigin) {
  // allow-same-origin을 유지한 샌드박스도 실제 요청을 전달해야 한다.
  PolicyContainerPolicies policies;
  policies.sandbox_flags = network::mojom::WebSandboxFlags::kAutomaticFeatures;
  ExpectControlledWorkerFactoryForwardsRequest(policies);
}

}  // namespace
}  // namespace content
