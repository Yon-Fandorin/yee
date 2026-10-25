// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_FILTERING_URL_LOADER_FACTORY_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_FILTERING_URL_LOADER_FACTORY_H_
#include "base/functional/callback.h"
#include "content/public/browser/global_routing_id.h"
#include "url/origin.h"
namespace content {
class BrowserContext;
}
namespace network {
class URLLoaderFactoryBuilder;
}
namespace yee::content_blocking {
enum class FactoryPurpose { kSubresource, kNavigation, kPrefetch };
enum class TransportKind { kWebSocket, kWebTransport };
// UI에서 설정 snapshot을 얻고 기존 매칭 sequence에서 연결 주소를 검사한다.
void CheckTransportRequest(content::BrowserContext* browser_context,
                           const GURL& url,
                           const url::Origin& initiator,
                           const url::Origin& top_site,
                           TransportKind kind,
                           base::OnceCallback<void(bool)> reply);
void MaybeAppendFilteringFactory(
    network::URLLoaderFactoryBuilder& builder,
    const url::Origin& initiator,
    const url::Origin& top_site,
    FactoryPurpose purpose = FactoryPurpose::kSubresource,
    content::BrowserContext* browser_context = nullptr,
    content::GlobalRenderFrameHostId frame_id = {});
}  // namespace yee::content_blocking
#endif
