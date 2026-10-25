// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_TRANSPORT_FILTERING_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_TRANSPORT_FILTERING_H_

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/web_transport.mojom-forward.h"
#include "services/network/public/mojom/websocket.mojom-forward.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content
namespace yee::content_blocking {
// 연결을 시작하기 전에 판정하고, 문서·클라이언트가 사라지면 재개하지 않는다.
void FilterWebSocket(
    content::BrowserContext* context,
    content::RenderFrameHost* frame,
    const GURL& url,
    const url::Origin& initiator,
    const url::Origin& top,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient> client,
    base::OnceCallback<
        void(mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>)>
        allowed);
void FilterWebTransport(
    content::BrowserContext* context,
    content::RenderFrameHost* frame,
    const GURL& url,
    const url::Origin& initiator,
    const url::Origin& top,
    mojo::PendingRemote<network::mojom::WebTransportHandshakeClient> client,
    base::OnceCallback<
        void(bool,
             mojo::PendingRemote<network::mojom::WebTransportHandshakeClient>)>
        complete);
}  // namespace yee::content_blocking
#endif
