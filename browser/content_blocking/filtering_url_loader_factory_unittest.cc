// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/filtering_url_loader_factory.h"

#include "base/no_destructor.h"
#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/yee_content_blocking/settings.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe_utils.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/parsed_headers.mojom.h"
#include "services/network/test/test_url_loader_client.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"

namespace yee::content_blocking {
namespace {
class FilteringFactoryTest : public testing::Test {
 public:
  static std::unique_ptr<base::test::TaskEnvironment>& Environment() {
    static base::NoDestructor<std::unique_ptr<base::test::TaskEnvironment>>
        environment;
    return *environment;
  }
  static void SetUpTestSuite() {
    mojo::core::Init();
    // Production's dedicated runner lives as long as its ThreadPool. Keep
    // that same lifetime across the suite.
    Environment() = std::make_unique<base::test::TaskEnvironment>();
  }
  static void TearDownTestSuite() { Environment().reset(); }
  void SetUp() override {
    command_.GetProcessCommandLine()->AppendSwitch(
        "yee-content-blocking-test-rules");
  }
  void TearDown() override {
    loader_.reset();
    factory_.reset();
    Environment()->RunUntilIdle();
  }
  void Connect(
      const url::Origin& initiator =
          url::Origin::Create(GURL("https://page.test/")),
      const url::Origin& top = url::Origin::Create(GURL("https://page.test/")),
      FactoryPurpose purpose = FactoryPurpose::kSubresource) {
    network::URLLoaderFactoryBuilder builder;
    MaybeAppendFilteringFactory(builder, initiator, top, purpose);
    mojo::PendingRemote<network::mojom::URLLoaderFactory> terminal;
    terminal_.Clone(terminal.InitWithNewPipeAndPassReceiver());
    factory_.Bind(
        std::move(builder)
            .Finish<mojo::PendingRemote<network::mojom::URLLoaderFactory>>(
                std::move(terminal)));
  }
  void Start(
      const char* url,
      blink::mojom::ResourceType type = blink::mojom::ResourceType::kXhr) {
    network::ResourceRequest request;
    request.url = GURL(url);
    request.destination = network::mojom::RequestDestination::kEmpty;
    request.resource_type = static_cast<int>(type);
    StartRequest(request);
  }
  void StartRequest(const network::ResourceRequest& request) {
    factory_->CreateLoaderAndStart(
        loader_.BindNewPipeAndPassReceiver(), 7, 0, request,
        client_.CreateRemote(),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  }
  void ConnectNavigation() {
    Connect(url::Origin(), url::Origin(), FactoryPurpose::kNavigation);
  }
  void AddCspResponse(const char* url, bool parsed = true) {
    auto head = network::mojom::URLResponseHead::New();
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        net::HttpUtil::AssembleRawHeaders(
            "HTTP/1.1 201 Created\nContent-Security-Policy: script-src "
            "'self'\nX-Fixture: retained\n\n"));
    if (parsed) {
      head->parsed_headers = network::mojom::ParsedHeaders::New();
      head->parsed_headers->content_security_policy =
          network::ParseContentSecurityPolicies(
              "script-src 'self'",
              network::mojom::ContentSecurityPolicyType::kEnforce,
              network::mojom::ContentSecurityPolicySource::kHTTP, GURL(url));
    }
    terminal_.AddResponse(GURL(url), std::move(head), "retained",
                          network::URLLoaderCompletionStatus(net::OK));
  }
  void StartNavigation(const char* url,
                       bool main_frame = true,
                       const char* top = "https://page.test/",
                       const char* source = "https://page.test/") {
    network::ResourceRequest request;
    request.url = GURL(url);
    request.destination = network::mojom::RequestDestination::kDocument;
    request.resource_type =
        static_cast<int>(main_frame ? blink::mojom::ResourceType::kMainFrame
                                    : blink::mojom::ResourceType::kSubFrame);
    request.is_outermost_main_frame = main_frame;
    request.request_initiator = url::Origin::Create(GURL(source));
    request.trusted_params.emplace();
    const auto top_origin = url::Origin::Create(GURL(main_frame ? url : top));
    request.trusted_params->isolation_info = net::IsolationInfo::Create(
        main_frame ? net::IsolationInfo::RequestType::kMainFrame
                   : net::IsolationInfo::RequestType::kSubFrame,
        top_origin, url::Origin::Create(request.url),
        net::SiteForCookies::FromOrigin(top_origin));
    StartRequest(request);
  }

 protected:
  base::test::ScopedCommandLine command_;
  network::TestURLLoaderFactory terminal_{true};
  network::TestURLLoaderClient client_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_;
  mojo::Remote<network::mojom::URLLoader> loader_;
};
TEST_F(FilteringFactoryTest, BlockedRequestNeverReachesTerminal) {
  Connect();
  Start("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, AllowedResponseKeepsHeadAndBody) {
  terminal_.AddResponse("https://yee-block.test/allowed/content", "retained",
                        net::HTTP_CREATED);
  Connect();
  Start("https://yee-block.test/allowed/content");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->headers->response_code(), 201);
  std::string body;
  EXPECT_TRUE(
      mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ(body, "retained");
  EXPECT_EQ(terminal_.total_requests(), 1u);
}
TEST_F(FilteringFactoryTest, RedirectResourceProducesBodyWithoutNetworkRequest) {
  Connect();
  Start("https://yee-redirect.test/ad.js");
  loader_->SetPriority(net::HIGHEST, 0);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->mime_type, "application/javascript");
  EXPECT_EQ(client_.response_head()->headers->response_code(), 200);
  EXPECT_EQ(client_.response_head()->headers->GetNormalizedHeader("Content-Type"), "application/javascript");
  std::string body;
  ASSERT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ(body, "/* Yee empty replacement. */");
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, LegacyRewriteAliasDeliversVp9BlankMp4) {
  Connect();
  Start("https://yee-video.test/ad.mp4");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->mime_type, "video/mp4");
  std::string body;
  ASSERT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ(body.size(), 837u);
  EXPECT_EQ(body.substr(4, 4), "ftyp");
  EXPECT_NE(body.find("vp09"), std::string::npos);
  EXPECT_EQ(body.find("avc1"), std::string::npos);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
// Brave's stub-response tests require the resource MIME to take precedence
// over Accept. Exercise that contract through the actual Mojo response pipe.
TEST_F(FilteringFactoryTest, Utf8ReplacementMimeOverridesAcceptHeader) {
  Connect();
  network::ResourceRequest request;
  request.url = GURL("https://yee-utf8.test/ad");
  request.headers.SetHeader("Accept", "image/svg+xml");
  StartRequest(request);
  client_.RunUntilComplete();
  ASSERT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->mime_type, "text/html");
  EXPECT_EQ(client_.response_head()->headers->GetNormalizedHeader("Content-Type"), "text/html");
  std::string body;
  ASSERT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ(body, "<strong>π</strong>");
  EXPECT_EQ(client_.response_head()->content_length, static_cast<int64_t>(body.size()));
  EXPECT_EQ(client_.completion_status().decoded_body_length, base::ByteSize(body.size()));
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, BinaryReplacementPreservesNulsAndInvalidUtf8) {
  Connect();
  Start("https://yee-binary.test/ad");
  client_.RunUntilComplete();
  ASSERT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->mime_type, "image/png");
  std::string body;
  ASSERT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ(body, std::string("\x89PNG\r\n\x1a\n\0\xff\0", 11));
  EXPECT_EQ(client_.response_head()->content_length, 11);
  EXPECT_EQ(client_.completion_status().encoded_body_length, base::ByteSize(11));
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, EmptyReplacementCompletesSuccessfully) {
  Connect();
  Start("https://yee-zero.test/ad");
  client_.RunUntilComplete();
  ASSERT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->content_length, 0);
  std::string body;
  ASSERT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_TRUE(body.empty());
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, RedirectRuleAloneDoesNotReplaceAnAllowedRequest) {
  terminal_.AddResponse("https://yee-redirect-rule.test/ad", "retained");
  Connect();
  Start("https://yee-redirect-rule.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  EXPECT_EQ(terminal_.total_requests(), 1u);
}
TEST_F(FilteringFactoryTest, ReplacementHeadHasNoBodyAndRetainsResourceLength) {
  Connect();
  network::ResourceRequest request;
  request.url = GURL("https://yee-redirect.test/ad.js");
  request.method = "HEAD";
  request.mode = network::mojom::RequestMode::kCors;
  request.credentials_mode = network::mojom::CredentialsMode::kInclude;
  request.request_initiator = url::Origin::Create(GURL("https://page.test/"));
  StartRequest(request);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.response_head()->response_type, network::mojom::FetchResponseType::kCors);
  EXPECT_EQ(client_.response_head()->headers->GetNormalizedHeader("Access-Control-Allow-Origin"), "https://page.test");
  EXPECT_EQ(client_.response_head()->content_length, 28);
  std::string body;
  EXPECT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_TRUE(body.empty());
}
TEST_F(FilteringFactoryTest, ReplacementDoesNotBypassSameOriginMode) {
  Connect();
  network::ResourceRequest request;
  request.url = GURL("https://yee-redirect.test/ad.js");
  request.mode = network::mojom::RequestMode::kSameOrigin;
  request.request_initiator = url::Origin::Create(GURL("https://page.test/"));
  StartRequest(request);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_FAILED);
  EXPECT_FALSE(client_.response_head());
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, RedirectReplacementPreservesCorsOriginTaint) {
  Connect();
  network::ResourceRequest request;
  request.url = GURL("https://third-party.test/start");
  request.mode = network::mojom::RequestMode::kCors;
  request.request_initiator = url::Origin::Create(GURL("https://page.test/"));
  StartRequest(request);
  terminal_.WaitForRequest(request.url);
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-redirect.test/ad");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilRedirectReceived();
  loader_->FollowRedirect({}, std::nullopt);
  client_.RunUntilComplete();
  ASSERT_TRUE(client_.response_head());
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  EXPECT_EQ(client_.response_head()->headers->GetNormalizedHeader("Access-Control-Allow-Origin"), "null");
}
TEST_F(FilteringFactoryTest, QueryRedirectCancellationDoesNotDispatchNetworkRequest) {
  Connect();
  Start("https://yee-query.test/?tracking=1");
  client_.RunUntilRedirectReceived();
  loader_.reset();
  Environment()->RunUntilIdle();
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, QueryRedirectRejectsCrossOriginClientOverride) {
  Connect();
  Start("https://yee-query.test/?tracking=1");
  client_.RunUntilRedirectReceived();
  loader_->FollowRedirect({}, GURL("https://other.test/"));
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_UNSAFE_REDIRECT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, ServerRedirectResourceWaitsForFollowAndCancelsUpstream) {
  Connect();
  Start("https://allowed.test/start");
  terminal_.WaitForRequest(GURL("https://allowed.test/start"));
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-redirect.test/ad");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilRedirectReceived();
  EXPECT_FALSE(client_.has_received_response());
  loader_->FollowRedirect({}, std::nullopt);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  EXPECT_EQ(terminal_.total_requests(), 1u);
  std::string body;
  EXPECT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ(body, "/* Yee empty replacement. */");
}
TEST_F(FilteringFactoryTest, QueryRewriteKeepsMethodBodyHeadersAndFactoryLifetime) {
  Connect();
  network::ResourceRequest request;
  request.url = GURL("https://yee-query.test/?tracking=1&keep=1");
  request.method = "POST";
  request.headers.SetHeader("X-Original", "retained");
  request.request_body = network::ResourceRequestBody::CreateFromCopyOfBytes(
      base::byte_span_from_cstring("retained"));
  StartRequest(request);
  client_.RunUntilRedirectReceived();
  EXPECT_EQ(client_.redirect_info().status_code, 307);
  EXPECT_EQ(client_.redirect_info().new_url, GURL("https://yee-query.test/?keep=1"));
  EXPECT_EQ(terminal_.total_requests(), 0u);
  factory_.reset();
  network::HttpRequestHeadersUpdateParams headers;
  headers.modified_headers.SetHeader("X-Follow", "updated");
  loader_->FollowRedirect(std::move(headers), std::nullopt);
  terminal_.WaitForRequest(GURL("https://yee-query.test/?keep=1"));
  const auto& actual = terminal_.GetPendingRequest(0)->request;
  EXPECT_EQ(actual.method, "POST");
  EXPECT_EQ(actual.headers.GetHeader("X-Original"), "retained");
  EXPECT_EQ(actual.headers.GetHeader("X-Follow"), "updated");
  ASSERT_TRUE(actual.request_body);
  EXPECT_EQ(actual.request_body->elements()->size(), 1u);
  terminal_.SimulateResponseForPendingRequest("https://yee-query.test/?keep=1", "retained");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, ServerRedirectQueryRewriteReachesDownstreamFollow) {
  Connect();
  Start("https://allowed.test/start");
  terminal_.WaitForRequest(GURL("https://allowed.test/start"));
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-query.test/?tracking=1&keep=1");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilRedirectReceived();
  EXPECT_EQ(client_.redirect_info().new_url, GURL("https://yee-query.test/?keep=1"));
  loader_->FollowRedirect({}, std::nullopt);
  loader_.FlushForTesting();
  Environment()->RunUntilIdle();
  const auto& follow = terminal_.GetPendingRequest(0)->test_url_loader->follow_redirect_params();
  ASSERT_EQ(follow.size(), 1u);
  EXPECT_EQ(follow[0].new_url, GURL("https://yee-query.test/?keep=1"));
}
TEST_F(FilteringFactoryTest, PingRuleAppliesToBeacon) {
  Connect();
  Start("https://yee-ping.test/beacon", blink::mojom::ResourceType::kPing);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, PingRuleDoesNotBlockFetch) {
  terminal_.AddResponse("https://yee-ping.test/fetch", "retained");
  Connect();
  Start("https://yee-ping.test/fetch");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, ServerRedirectIsBlockedBeforeFollow) {
  Connect();
  Start("https://allowed.test/start");
  terminal_.WaitForRequest(GURL("https://allowed.test/start"));
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-block.test/ad");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_FALSE(client_.has_received_redirect());
}
TEST_F(FilteringFactoryTest, ClientRedirectOverrideIsRechecked) {
  Connect();
  Start("https://allowed.test/start");
  terminal_.WaitForRequest(GURL("https://allowed.test/start"));
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-block.test/allowed/redirect");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilRedirectReceived();
  loader_->FollowRedirect({}, GURL("https://yee-block.test/ad"));
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_TRUE(terminal_.GetPendingRequest(0)
                  ->test_url_loader->follow_redirect_params()
                  .empty());
}
TEST_F(FilteringFactoryTest, LoaderCancellationClosesUpstreamClient) {
  Connect();
  Start("https://allowed.test/pending");
  terminal_.WaitForRequest(GURL("https://allowed.test/pending"));
  loader_.reset();
  Environment()->RunUntilIdle();
  EXPECT_EQ(terminal_.NumPending(), 0);
}
TEST_F(FilteringFactoryTest, RequestOutlivesFactory) {
  Connect();
  Start("https://allowed.test/pending");
  terminal_.WaitForRequest(GURL("https://allowed.test/pending"));
  factory_.reset();
  Environment()->RunUntilIdle();
  terminal_.SimulateResponseForPendingRequest("https://allowed.test/pending",
                                              "retained");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, UpstreamDisconnectCompletesAsAborted) {
  Connect();
  Start("https://allowed.test/pending");
  terminal_.WaitForRequest(GURL("https://allowed.test/pending"));
  terminal_.GetPendingRequest(0)->client.reset();
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_ABORTED);
}
TEST_F(FilteringFactoryTest, CloneKeepsFilteringAfterOriginalCloses) {
  Connect();
  mojo::Remote<network::mojom::URLLoaderFactory> clone;
  factory_->Clone(clone.BindNewPipeAndPassReceiver());
  factory_.reset();
  factory_ = std::move(clone);
  Start("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest, SiteExceptionUsesTopOrigin) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "PAGE.test");
  terminal_.AddResponse("https://yee-block.test/ad", "retained");
  Connect(url::Origin::Create(GURL("https://iframe.test/")));
  Start("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, OpaqueWorkerMetadataFallsBackToWebInitiator) {
  Connect(url::Origin::Create(GURL("https://page.test/")), url::Origin());
  Start("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest, WebSubresourceFromInternalInitiatorIsProtected) {
  Connect(url::Origin::Create(GURL("chrome://newtab/")));
  Start("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest, EntirelyOpaqueFactoryIsOutsideProtection) {
  terminal_.AddResponse("https://yee-block.test/ad", "retained");
  Connect(url::Origin(), url::Origin());
  Start("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, MainDocumentUsesItsOwnFirstPartyContext) {
  terminal_.AddResponse("https://yee-document-party.test/page", "retained");
  ConnectNavigation();
  StartNavigation("https://yee-document-party.test/page");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  EXPECT_EQ(terminal_.total_requests(), 1u);
}
TEST_F(FilteringFactoryTest, CspAddsPolicyWithoutReplacingServerPolicy) {
  AddCspResponse("https://yee-csp.test/page");
  ConnectNavigation();
  StartNavigation("https://yee-csp.test/page");
  client_.RunUntilComplete();
  ASSERT_EQ(client_.completion_status().error_code, net::OK);
  const auto& head = client_.response_head();
  ASSERT_TRUE(head);
  EXPECT_EQ(head->headers->response_code(), 201);
  EXPECT_EQ(head->headers->GetNormalizedHeader("X-Fixture"), "retained");
  ASSERT_TRUE(head->parsed_headers);
  ASSERT_EQ(head->parsed_headers->content_security_policy.size(), 2u);
  EXPECT_EQ(
      head->parsed_headers->content_security_policy[0]->header->header_value,
      "script-src 'self'");
  EXPECT_EQ(
      head->parsed_headers->content_security_policy[1]->header->header_value,
      "img-src 'none'");
  EXPECT_EQ(head->headers->GetNormalizedHeader("Content-Security-Policy"),
            "script-src 'self', img-src 'none'");
}
TEST_F(FilteringFactoryTest, CspNavigationHonorsPublisherException) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "page.test");
  AddCspResponse("https://yee-csp.test/frame");
  ConnectNavigation();
  StartNavigation("https://yee-csp.test/frame", false);
  client_.RunUntilComplete();
  ASSERT_TRUE(client_.response_head()->parsed_headers);
  EXPECT_EQ(
      client_.response_head()->parsed_headers->content_security_policy.size(),
      1u);
}
TEST_F(FilteringFactoryTest, CspFilterExceptionPreservesOnlyServerPolicy) {
  AddCspResponse("https://yee-csp.test/except/page");
  ConnectNavigation();
  StartNavigation("https://yee-csp.test/except/page");
  client_.RunUntilComplete();
  EXPECT_EQ(
      client_.response_head()->parsed_headers->content_security_policy.size(),
      1u);
}
TEST_F(FilteringFactoryTest, CspDoesNotInventParsedServerHeaders) {
  AddCspResponse("https://yee-csp.test/page", false);
  ConnectNavigation();
  StartNavigation("https://yee-csp.test/page");
  client_.RunUntilComplete();
  EXPECT_FALSE(client_.response_head()->parsed_headers);
  EXPECT_EQ(client_.response_head()->headers->GetNormalizedHeader(
                "Content-Security-Policy"),
            "script-src 'self'");
}
TEST_F(FilteringFactoryTest, OpaqueNavigationFactoryBlocksMainDocument) {
  ConnectNavigation();
  StartNavigation("https://yee-block.test/ad", true, "https://page.test/",
                  "chrome://newtab/");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, OpaqueNavigationFactoryBlocksIframe) {
  ConnectNavigation();
  StartNavigation("https://yee-block.test/ad", false);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, IframeNavigationUsesPublisherException) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "PAGE.test");
  terminal_.AddResponse("https://yee-block.test/ad", "retained");
  ConnectNavigation();
  StartNavigation("https://yee-block.test/ad", false);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, IframeTargetExceptionDoesNotDisablePublisher) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "yee-block.test");
  ConnectNavigation();
  StartNavigation("https://yee-block.test/ad", false);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest, MainNavigationUsesTargetException) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "yee-block.test");
  terminal_.AddResponse("https://yee-block.test/ad", "retained");
  ConnectNavigation();
  StartNavigation("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, MainNavigationRechecksExceptionAfterRedirect) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "allowed.test");
  ConnectNavigation();
  StartNavigation("https://allowed.test/start");
  terminal_.WaitForRequest(GURL("https://allowed.test/start"));
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-block.test/ad");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest, MainNavigationHonorsRedirectTargetException) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "yee-block.test");
  ConnectNavigation();
  StartNavigation("https://allowed.test/start");
  terminal_.WaitForRequest(GURL("https://allowed.test/start"));
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://yee-block.test/ad");
  redirect.new_method = "GET";
  terminal_.GetPendingRequest(0)->client->OnReceiveRedirect(
      redirect, network::mojom::URLResponseHead::New());
  client_.RunUntilRedirectReceived();
  EXPECT_TRUE(client_.has_received_redirect());
}
TEST_F(FilteringFactoryTest, DisabledFeatureDoesNotInstallNavigationProxy) {
  base::test::ScopedFeatureList feature;
  feature.InitAndDisableFeature(kYeeContentBlocking);
  terminal_.AddResponse("https://yee-block.test/ad", "retained");
  ConnectNavigation();
  StartNavigation("https://yee-block.test/ad");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, IframeResourceTypeOverridesDocumentDestination) {
  ConnectNavigation();
  StartNavigation("https://yee-frame-type.test/ad", false);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest, NavigationUsesPerRequestInitiatorForDomainRules) {
  ConnectNavigation();
  StartNavigation("https://yee-request-source.test/ad", false,
                  "https://page.test/", "https://iframe-source.test/");
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
}
TEST_F(FilteringFactoryTest,
       MissingIframeSiteDoesNotInventTargetExceptionContext) {
  terminal_.AddResponse("https://yee-block.test/ad", "retained");
  ConnectNavigation();
  network::ResourceRequest request;
  request.url = GURL("https://yee-block.test/ad");
  request.resource_type =
      static_cast<int>(blink::mojom::ResourceType::kSubFrame);
  request.destination = network::mojom::RequestDestination::kIframe;
  StartRequest(request);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
}
TEST_F(FilteringFactoryTest, PrefetchUsesProductionOtherRule) {
  const char* url = "https://cdn.fixture.test/client_204?event=fixture";
  terminal_.AddResponse(url, "prefetched");
  Connect();
  Start(url, blink::mojom::ResourceType::kPrefetch);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}
TEST_F(FilteringFactoryTest, TrustedPrefetchTargetException) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "yee-block.test");
  Connect(url::Origin::Create(GURL("https://page.test/")),
          url::Origin::Create(GURL("https://page.test/")), FactoryPurpose::kPrefetch);
  network::ResourceRequest request;
  terminal_.AddResponse("https://yee-block.test/ad", "allowed target");
  request.url = GURL("https://yee-block.test/ad");
  request.destination = network::mojom::RequestDestination::kEmpty;
  request.resource_type = static_cast<int>(blink::mojom::ResourceType::kPrefetch);
  request.request_initiator = url::Origin::Create(GURL("https://page.test/"));
  request.trusted_params.emplace();
  const auto target = url::Origin::Create(request.url);
  request.trusted_params->isolation_info = net::IsolationInfo::Create(
      net::IsolationInfo::RequestType::kMainFrame, target, target,
      net::SiteForCookies());
  StartRequest(request);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::OK);
  EXPECT_EQ(terminal_.total_requests(), 1u);
}
TEST_F(FilteringFactoryTest, TrustedPrefetchProtectedTarget) {
  command_.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "page.test");
  const char* url = "https://yee-block.test/ad";
  terminal_.AddResponse(url, "prefetched");
  Connect(url::Origin::Create(GURL("https://page.test/")),
          url::Origin::Create(GURL("https://page.test/")), FactoryPurpose::kPrefetch);
  network::ResourceRequest request;
  request.url = GURL(url);
  request.destination = network::mojom::RequestDestination::kEmpty;
  request.resource_type = static_cast<int>(blink::mojom::ResourceType::kPrefetch);
  request.request_initiator = url::Origin::Create(GURL("https://page.test/"));
  request.trusted_params.emplace();
  const auto target = url::Origin::Create(request.url);
  request.trusted_params->isolation_info = net::IsolationInfo::Create(
      net::IsolationInfo::RequestType::kMainFrame, target, target,
      net::SiteForCookies());
  StartRequest(request);
  client_.RunUntilComplete();
  EXPECT_EQ(client_.completion_status().error_code, net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_EQ(terminal_.total_requests(), 0u);
}

}  // namespace
}  // namespace yee::content_blocking
