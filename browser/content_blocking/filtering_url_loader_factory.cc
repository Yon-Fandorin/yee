// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/filtering_url_loader_factory.h"

#include <utility>
#include "base/functional/bind.h"
#include "base/memory/self_deleting.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"
#include "components/yee_content_blocking/engine.h"
#include "components/yee_content_blocking/settings.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "mojo/public/cpp/system/string_data_source.h"
#include "net/base/data_url.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/parsed_headers.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"

namespace yee::content_blocking {
namespace {
struct FactoryContext {
  url::Origin initiator;
  GURL top_site;
  FactoryPurpose purpose;
  scoped_refptr<ContentBlockingSettingsSnapshot> settings;
  content::GlobalRenderFrameHostId frame_id;
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner;
};

std::string_view RequestType(const network::ResourceRequest& request) {
  using R = blink::mojom::ResourceType;
  if (request.resource_type == static_cast<int>(R::kMainFrame))
    return "document";
  if (request.resource_type == static_cast<int>(R::kSubFrame))
    return "subdocument";
  if (request.resource_type == static_cast<int>(R::kPing))
    return "ping";
  if (request.resource_type == static_cast<int>(R::kCspReport))
    return "csp_report";
  if (request.resource_type == static_cast<int>(R::kPrefetch))
    return "other";
  using D = network::mojom::RequestDestination;
  switch (request.destination) {
    case D::kDocument:
      return "document";
    case D::kFrame:
    case D::kIframe:
    case D::kFencedframe:
      return "subdocument";
    case D::kStyle:
    case D::kXslt:
      return "stylesheet";
    case D::kScript:
    case D::kWorker:
    case D::kSharedWorker:
    case D::kServiceWorker:
    case D::kAudioWorklet:
    case D::kPaintWorklet:
    case D::kJson:
      return "script";
    case D::kImage:
      return "image";
    case D::kFont:
      return "font";
    case D::kAudio:
    case D::kVideo:
    case D::kTrack:
      return "media";
    case D::kEmbed:
    case D::kObject:
      return "object";
    case D::kEmpty:
      return "xmlhttprequest";
    default:
      return "other";
  }
}

// A request outlives its factory if the factory pipe is closed after dispatch.
// It owns both loader pipes and never retains Profile, frame or factory
// pointers.
class FilteringRequest final : public network::mojom::URLLoader,
                               public network::mojom::URLLoaderClient,
                               public base::SelfDeleting {
 public:
  FilteringRequest(mojo::PendingReceiver<network::mojom::URLLoader> receiver,
                   mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                   network::ResourceRequest request,
                   FactoryContext context,
                   base::SelfDeletingPassKey key)
      : base::SelfDeleting(key),
        request_(std::move(request)),
        policy_initiator_(request_.request_initiator),
        context_(std::move(context)),
        receiver_(this, std::move(receiver)),
        client_(std::move(client)),
        upstream_client_(this) {
    receiver_.set_disconnect_handler(
        base::BindOnce(&FilteringRequest::Destroy, base::Unretained(this)));
    client_.set_disconnect_handler(
        base::BindOnce(&FilteringRequest::Destroy, base::Unretained(this)));
  }

  void Start(network::mojom::URLLoaderFactory* factory,
             int32_t id,
             uint32_t options,
             const net::MutableNetworkTrafficAnnotationTag& tag) {
    const auto decision = Evaluate(request_.url, request_.method);
    if (decision.blocked) {
      CompleteDecision(decision);
      return;
    }
    if (const auto target = RewrittenUrl(request_.url, decision)) {
      // Keep Fetch's URL/redirect mode and navigation semantics: do not
      // silently change the URL sent to Network Service.
      factory->Clone(deferred_factory_.BindNewPipeAndPassReceiver());
      deferred_factory_.set_disconnect_handler(base::BindOnce(
          &FilteringRequest::UpstreamDisconnected, base::Unretained(this)));
      start_ = StartParams{id, options, tag};
      const auto redirect = net::RedirectInfo::ComputeRedirectInfo(
          request_.method, request_.url, request_.site_for_cookies,
          net::RedirectInfo::FirstPartyURLPolicy::NEVER_CHANGE_URL,
          request_.referrer_policy, request_.referrer.spec(),
          request_.request_initiator, 307, *target, std::nullopt, false);
      auto head = network::mojom::URLResponseHead::New();
      head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
          net::HttpUtil::AssembleRawHeaders(
              "HTTP/1.1 307 Internal Redirect\r\nLocation: " + target->spec() +
              "\r\n\r\n"));
      OnReceiveRedirect(redirect, std::move(head));
      return;
    }
    StartUpstream(factory, id, options, tag);
  }

  void StartUpstream(network::mojom::URLLoaderFactory* factory,
                     int32_t id,
                     uint32_t options,
                     const net::MutableNetworkTrafficAnnotationTag& tag) {
    auto upstream_client = upstream_client_.BindNewPipeAndPassRemote();
    // A Mojo Receiver must be bound before its disconnect handler is set.
    upstream_client_.set_disconnect_handler(base::BindOnce(
        &FilteringRequest::UpstreamDisconnected, base::Unretained(this)));
    factory->CreateLoaderAndStart(upstream_.BindNewPipeAndPassReceiver(), id,
                                  options, request_, std::move(upstream_client),
                                  tag);
    if (priority_)
      upstream_->SetPriority(priority_->first, priority_->second);
  }

  void FollowRedirect(network::HttpRequestHeadersUpdateParams headers,
                      const std::optional<GURL>& new_url) override {
    if (!awaiting_redirect_) {
      if (upstream_.is_bound())
        upstream_->FollowRedirect(std::move(headers), new_url);
      return;
    }
    awaiting_redirect_ = false;
    // Renderer overrides also go through evaluation; the downstream loader
    // remains responsible for validating same-origin and redirect checks.
    const GURL target = new_url.value_or(request_.url);
    const auto decision = Evaluate(target, request_.method);
    if (decision.blocked) {
      // Local responses must not bypass Network Service's redirect override
      // validation. A renderer may only override within the target origin.
      if (new_url && !url::Origin::Create(request_.url).IsSameOriginWith(
                         url::Origin::Create(target))) {
        CompleteBlocked();
        return;
      }
      request_.url = target;
      CompleteDecision(decision);
      return;
    }
    auto override = new_url;
    if (const auto rewritten = RewrittenUrl(target, decision))
      override = rewritten;
    if (start_) {
      if (!url::Origin::Create(request_.url).IsSameOriginWith(
              url::Origin::Create(target))) {
        CompleteError(net::ERR_UNSAFE_REDIRECT);
        return;
      }
      headers.Apply(request_.headers, request_.cors_exempt_headers);
      request_.url = override.value_or(target);
      const auto params = *start_;
      start_.reset();
      StartUpstream(deferred_factory_.get(), params.id, params.options,
                    params.tag);
      deferred_factory_.reset();
      return;
    }
    if (!upstream_.is_bound())
      return;
    // A rewritten server redirect was already exposed to the client, so its
    // effective destination must also be passed to the downstream loader.
    if (!override && pending_rewrite_)
      override = request_.url;
    pending_rewrite_ = false;
    request_.url = override.value_or(target);
    upstream_->FollowRedirect(std::move(headers), override);
  }
  void SetPriority(net::RequestPriority priority, int32_t intra) override {
    priority_ = std::make_pair(priority, intra);
    if (upstream_.is_bound())
      upstream_->SetPriority(priority, intra);
  }
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr hints) override {
    client_->OnReceiveEarlyHints(std::move(hints));
  }
  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> metadata) override {
    ApplyCsp(*head);
    client_->OnReceiveResponse(std::move(head), std::move(body),
                               std::move(metadata));
  }
  void OnReceiveRedirect(const net::RedirectInfo& redirect,
                         network::mojom::URLResponseHeadPtr head) override {
    const auto decision = Evaluate(redirect.new_url, redirect.new_method);
    if (decision.blocked && decision.replacement.empty()) {
      CompleteBlocked();
      return;
    }
    auto effective = redirect;
    if (const auto rewritten = RewrittenUrl(redirect.new_url, decision)) {
      effective.new_url = *rewritten;
      pending_rewrite_ = true;
      if (head->headers) {
        auto headers = base::MakeRefCounted<net::HttpResponseHeaders>(
            head->headers->raw_headers());
        headers->SetHeader("Location", rewritten->spec());
        head->headers = std::move(headers);
      }
    }
    // Match Fetch's redirect-origin tainting for local resource responses.
    // Network Service applies this itself for ordinary downstream responses.
    if (request_.request_initiator &&
        !url::Origin::Create(request_.url).IsSameOriginWith(
            url::Origin::Create(effective.new_url)) &&
        !request_.request_initiator->IsSameOriginWith(
            url::Origin::Create(request_.url)))
      request_.request_initiator = url::Origin();
    request_.url = effective.new_url;
    request_.method = redirect.new_method;
    request_.referrer = GURL(redirect.new_referrer);
    request_.referrer_policy = redirect.new_referrer_policy;
    request_.site_for_cookies = redirect.new_site_for_cookies;
    awaiting_redirect_ = true;
    client_->OnReceiveRedirect(effective, std::move(head));
  }
  void OnUploadProgress(int64_t current,
                        int64_t total,
                        OnUploadProgressCallback callback) override {
    client_->OnUploadProgress(current, total, std::move(callback));
  }
  void OnTransferSizeUpdated(int32_t size) override {
    client_->OnTransferSizeUpdated(size);
  }
  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    client_->OnComplete(status);
    Destroy();
  }

 private:
  ~FilteringRequest() override = default;
  std::pair<GURL, GURL> PolicyContext(const GURL& url) const {
    GURL site = context_.top_site;
    GURL source = context_.initiator.GetURL();
    if (context_.purpose == FactoryPurpose::kNavigation ||
        context_.purpose == FactoryPurpose::kPrefetch) {
      // Trusted navigation factories have no fixed origin/IsolationInfo.
      // Use browser-authored per-request metadata, never a retained RFH.
      if (request_.trusted_params) {
        site = request_.trusted_params->isolation_info.top_frame_origin()
                   .value_or(url::Origin())
                   .GetURL();
      }
      source = policy_initiator_.value_or(url::Origin()).GetURL();
      if (context_.purpose == FactoryPurpose::kNavigation &&
          request_.is_outermost_main_frame) {
        site = url;    // Recomputed for every server/client redirect target.
        source = url;  // A primary document is its own first-party context.
      }
    }
    if (!source.SchemeIsHTTPOrHTTPS())
      source = site;
    return {site, source};
  }
  NetworkDecision Evaluate(const GURL& url, std::string_view method) {
    if (!url.SchemeIsHTTPOrHTTPS())
      return {};
    const auto [site, source] = PolicyContext(url);
    if (context_.settings ? !context_.settings->EnabledForSite(site)
                          : !EnabledForSite(site))
      return {};
    if (context_.settings && context_.settings->IsBlockedDomain(url)) {
      NetworkDecision decision;
      decision.blocked = true;
      return decision;
    }
    return BundledEngineForCurrentSequence().Evaluate(
        url.spec(), source.spec(), RequestType(request_), method);
  }
  static std::optional<GURL> RewrittenUrl(const GURL& original,
                                         const NetworkDecision& decision) {
    if (decision.blocked || decision.rewritten_url.empty())
      return std::nullopt;
    const GURL target(decision.rewritten_url);
    // removeparam may only change the query, never credentials, fragments,
    // path or origin. Only immutable bundled filters reach this bridge.
    GURL::Replacements strip_query;
    strip_query.ClearQuery();
    if (!target.is_valid() || target == original ||
        target.ReplaceComponents(strip_query) !=
            original.ReplaceComponents(strip_query))
      return std::nullopt;
    return target;
  }
  void CompleteDecision(const NetworkDecision& decision) {
    ReportBlockedRequest();
    if (decision.replacement.empty()) {
      CompleteBlocked();
      return;
    }
    std::string mime, charset, data;
    if (decision.replacement.size() > 6 * 1024 * 1024 ||
        !net::DataURL::Parse(GURL(decision.replacement), &mime, &charset,
                             &data) || data.size() > 4 * 1024 * 1024) {
      CompleteBlocked();
      return;
    }
    const auto origin = request_.request_initiator.value_or(context_.initiator);
    if (request_.mode == network::mojom::RequestMode::kSameOrigin &&
        !origin.IsSameOriginWith(url::Origin::Create(request_.url))) {
      CompleteError(net::ERR_FAILED);
      return;
    }
    upstream_.reset();
    upstream_client_.reset();
    deferred_factory_.reset();
    start_.reset();
    auto head = network::mojom::URLResponseHead::New();
    head->request_time = head->response_time = base::Time::Now();
    head->mime_type = mime;
    head->charset = charset;
    head->content_length = data.size();
    head->encoded_data_length = 0;
    head->parsed_headers = network::mojom::ParsedHeaders::New();
    const bool cross_origin =
        !origin.IsSameOriginWith(url::Origin::Create(request_.url));
    head->response_type =
        cross_origin && request_.mode == network::mojom::RequestMode::kNoCors
            ? network::mojom::FetchResponseType::kOpaque
            : (cross_origin &&
                       request_.mode == network::mojom::RequestMode::kCors
                   ? network::mojom::FetchResponseType::kCors
                   : network::mojom::FetchResponseType::kBasic);
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        net::HttpUtil::AssembleRawHeaders(
        "HTTP/1.1 200 OK\r\nContent-Type: " + mime +
        (charset.empty() ? "" : ";charset=" + charset) +
        "\r\nContent-Length: " + base::NumberToString(data.size()) +
        "\r\nAccess-Control-Allow-Origin: " + origin.Serialize() +
        "\r\nAccess-Control-Allow-Credentials: true\r\n\r\n"));
    if (request_.method == "HEAD")
      data.clear();
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    if (mojo::CreateDataPipe(0u, producer, consumer) != MOJO_RESULT_OK) {
      CompleteError(net::ERR_INSUFFICIENT_RESOURCES);
      return;
    }
    const size_t size = data.size();
    producer_ = std::make_unique<mojo::DataPipeProducer>(std::move(producer));
    client_->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
    producer_->Write(
        std::make_unique<mojo::StringDataSource>(
            base::span<const char>(data),
            mojo::StringDataSource::AsyncWritingMode::STRING_MAY_BE_INVALIDATED_BEFORE_COMPLETION),
        base::BindOnce(&FilteringRequest::ReplacementWritten,
                       base::Unretained(this), size));
  }
  void ReplacementWritten(size_t size, MojoResult result) {
    network::URLLoaderCompletionStatus status(
        result == MOJO_RESULT_OK ? net::OK : net::ERR_ABORTED);
    status.encoded_body_length = base::ByteSize(size);
    status.decoded_body_length = base::ByteSize(size);
    client_->OnComplete(status);
    Destroy();
  }
  void ApplyCsp(network::mojom::URLResponseHead& head) {
    const auto type = RequestType(request_);
    if ((type != "document" && type != "subdocument") ||
        !request_.url.SchemeIsHTTPOrHTTPS() || !head.headers ||
        !head.parsed_headers)
      return;
    const auto [site, source] = PolicyContext(request_.url);
    if (context_.settings ? !context_.settings->EnabledForSite(site)
                          : !EnabledForSite(site))
      return;
    const auto directives = BundledEngineForCurrentSequence().CspDirectives(
        request_.url.spec(), source.spec(), type, request_.method);
    if (directives.empty())
      return;
    // Only parse directives from the fixed, verified filter bundle here.
    // Server-controlled headers were parsed by Network Service. Keep those
    // policies and all other parsed security metadata intact.
    auto policies = network::ParseContentSecurityPolicies(
        directives, network::mojom::ContentSecurityPolicyType::kEnforce,
        network::mojom::ContentSecurityPolicySource::kHTTP, request_.url);
    auto headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        head.headers->raw_headers());
    headers->AddHeader("Content-Security-Policy", directives);
    head.headers = std::move(headers);
    for (auto& policy : policies)
      head.parsed_headers->content_security_policy.push_back(std::move(policy));
  }
  void CompleteBlocked() {
    ReportBlockedRequest();
    CompleteError(net::ERR_BLOCKED_BY_CLIENT);
  }
  void ReportBlockedRequest() {
    if (reported_blocked_request_ || !context_.frame_id ||
        !context_.ui_task_runner) {
      return;
    }
    reported_blocked_request_ = true;
    context_.ui_task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](content::GlobalRenderFrameHostId frame_id) {
              content::RenderFrameHost* frame =
                  content::RenderFrameHost::FromID(frame_id);
              if (!frame || !frame->GetPage().IsPrimary())
                return;
              content::WebContents* contents =
                  content::WebContents::FromRenderFrameHost(frame);
              if (!contents)
                return;
              ContentBlockingTabHelper::CreateForWebContents(contents);
              ContentBlockingTabHelper::FromWebContents(contents)
                  ->RecordBlockedRequest();
            },
            context_.frame_id));
  }
  void CompleteError(int error) {
    client_->OnComplete(network::URLLoaderCompletionStatus(error));
    Destroy();
  }
  void UpstreamDisconnected() {
    client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_ABORTED));
    Destroy();
  }
  void Destroy() { delete this; }
  network::ResourceRequest request_;
  const std::optional<url::Origin> policy_initiator_;
  const FactoryContext context_;
  mojo::Receiver<network::mojom::URLLoader> receiver_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  mojo::Remote<network::mojom::URLLoader> upstream_;
  mojo::Receiver<network::mojom::URLLoaderClient> upstream_client_;
  struct StartParams {
    int32_t id;
    uint32_t options;
    net::MutableNetworkTrafficAnnotationTag tag;
  };
  std::optional<StartParams> start_;
  mojo::Remote<network::mojom::URLLoaderFactory> deferred_factory_;
  std::optional<std::pair<net::RequestPriority, int32_t>> priority_;
  bool pending_rewrite_ = false;
  bool awaiting_redirect_ = false;
  bool reported_blocked_request_ = false;
  std::unique_ptr<mojo::DataPipeProducer> producer_;
};

class FilteringFactory final : public network::SelfDeletingURLLoaderFactory {
 public:
  FilteringFactory(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> downstream,
      FactoryContext context,
      base::SelfDeletingPassKey key)
      : SelfDeletingURLLoaderFactory(std::move(receiver), key),
        downstream_(std::move(downstream)),
        context_(std::move(context)) {
    downstream_.set_disconnect_handler(
        base::BindOnce(&FilteringFactory::DisconnectReceiversAndDestroy,
                       base::Unretained(this)));
  }
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& tag) override {
    auto* loader = base::MakeSelfDeleting<FilteringRequest>(
        std::move(receiver), std::move(client), request, context_);
    loader->Start(downstream_.get(), id, options, tag);
  }

 private:
  ~FilteringFactory() override = default;
  mojo::Remote<network::mojom::URLLoaderFactory> downstream_;
  const FactoryContext context_;
};

scoped_refptr<base::SingleThreadTaskRunner> MatchingRunner() {
  static const base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>>
      runner(base::ThreadPool::CreateSingleThreadTaskRunner(
          {base::TaskPriority::USER_BLOCKING},
          base::SingleThreadTaskRunnerThreadMode::DEDICATED));
  return *runner;
}
}  // namespace

void MaybeAppendFilteringFactory(network::URLLoaderFactoryBuilder& builder,
                                 const url::Origin& initiator,
                                 const url::Origin& top_site,
                                 FactoryPurpose purpose,
                                 content::BrowserContext* browser_context,
                                 content::GlobalRenderFrameHostId frame_id) {
  // A known web top-level origin owns site exceptions. Opaque factory metadata
  // may still have a web initiator (workers); never invent a web context for
  // browser-owned or entirely opaque traffic.
  const GURL initiator_url = initiator.GetURL();
  const GURL site =
      top_site.GetURL().SchemeIsHTTPOrHTTPS()
          ? top_site.GetURL()
          : (top_site.opaque() ? initiator_url : top_site.GetURL());
  // Navigation factories are browser-trusted and deliberately carry opaque
  // metadata. Their HTTP/top-site policy is evaluated when each request starts.
  scoped_refptr<ContentBlockingSettingsSnapshot> settings;
  if (browser_context) {
    Profile* profile = Profile::FromBrowserContext(browser_context);
    if (auto* service = ContentBlockingServiceFactory::GetForProfile(profile))
      settings = service->settings_snapshot();
  }
  // Only immutable process overrides may omit a subresource proxy. Profile
  // exceptions can change while existing tabs and shared/service workers keep
  // their factories alive; each request reads the current settings snapshot.
  if (!base::FeatureList::IsEnabled(kYeeContentBlocking) ||
      (purpose == FactoryPurpose::kSubresource && !EnabledForSite(site)))
    return;
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner =
      base::SingleThreadTaskRunner::GetCurrentDefault();
  auto [receiver, remote] = builder.Append();
  MatchingRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
             mojo::PendingRemote<network::mojom::URLLoaderFactory> remote,
             FactoryContext context) {
            base::MakeSelfDeleting<FilteringFactory>(
                std::move(receiver), std::move(remote), std::move(context));
          },
          std::move(receiver), std::move(remote),
          FactoryContext{initiator, site, purpose, std::move(settings),
                         frame_id, std::move(ui_task_runner)}));
}
}  // namespace yee::content_blocking
