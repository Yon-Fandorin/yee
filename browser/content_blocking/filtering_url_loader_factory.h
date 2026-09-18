// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_FILTERING_URL_LOADER_FACTORY_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_FILTERING_URL_LOADER_FACTORY_H_
#include "url/origin.h"
namespace network {
class URLLoaderFactoryBuilder;
}
namespace yee::content_blocking {
enum class FactoryPurpose { kSubresource, kNavigation, kPrefetch };
void MaybeAppendFilteringFactory(
    network::URLLoaderFactoryBuilder& builder,
    const url::Origin& initiator,
    const url::Origin& top_site,
    FactoryPurpose purpose = FactoryPurpose::kSubresource);
}  // namespace yee::content_blocking
#endif
