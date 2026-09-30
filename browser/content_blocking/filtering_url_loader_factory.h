// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_FILTERING_URL_LOADER_FACTORY_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_FILTERING_URL_LOADER_FACTORY_H_
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
void MaybeAppendFilteringFactory(
    network::URLLoaderFactoryBuilder& builder,
    const url::Origin& initiator,
    const url::Origin& top_site,
    FactoryPurpose purpose = FactoryPurpose::kSubresource,
    content::BrowserContext* browser_context = nullptr,
    content::GlobalRenderFrameHostId frame_id = {});
}  // namespace yee::content_blocking
#endif
