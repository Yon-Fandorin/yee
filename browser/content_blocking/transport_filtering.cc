// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/transport_filtering.h"

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"
#include "chrome/browser/yee_content_blocking/filtering_url_loader_factory.h"
#include "components/yee_content_blocking/settings.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/web_transport.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"

namespace yee::content_blocking {
namespace {
void Reject(network::mojom::WebSocketHandshakeClient* client) {
  client->OnFailure("Blocked by Yee", net::ERR_BLOCKED_BY_CLIENT, -1);
}

template <typename Client>
class HandshakeCheck {
 public:
  HandshakeCheck(
      content::BrowserContext* context,
      content::RenderFrameHost* frame,
      const GURL& url,
      const url::Origin& initiator,
      const url::Origin& top,
      TransportKind kind,
      mojo::PendingRemote<Client> client,
      base::OnceCallback<void(bool, mojo::PendingRemote<Client>)> allowed)
      : document_(frame ? frame->GetWeakDocumentPtr()
                        : content::WeakDocumentPtr()),
        has_document_(frame != nullptr),
        client_(std::move(client)),
        allowed_(std::move(allowed)) {
    client_.set_disconnect_handler(
        base::BindOnce(&HandshakeCheck::Destroy, base::Unretained(this)));
    CheckTransportRequest(
        context, url, initiator, top, kind,
        base::BindOnce(&HandshakeCheck::Complete, weak_factory_.GetWeakPtr()));
  }

 private:
  void Complete(bool blocked) {
    auto* frame = document_.AsRenderFrameHostIfValid();
    if (has_document_ && !frame) {
      delete this;
      return;
    }
    if (blocked) {
      if (frame && frame->GetPage().IsPrimary()) {
        auto* contents = content::WebContents::FromRenderFrameHost(frame);
        ContentBlockingTabHelper::CreateForWebContents(contents);
        ContentBlockingTabHelper::FromWebContents(contents)
            ->RecordBlockedRequest();
      }
    }
    std::move(allowed_).Run(blocked, client_.Unbind());
    delete this;
  }
  void Destroy() { delete this; }
  content::WeakDocumentPtr document_;
  const bool has_document_;
  mojo::Remote<Client> client_;
  base::OnceCallback<void(bool, mojo::PendingRemote<Client>)> allowed_;
  base::WeakPtrFactory<HandshakeCheck> weak_factory_{this};
};
}  // namespace
void FilterWebSocket(
    content::BrowserContext* context,
    content::RenderFrameHost* frame,
    const GURL& url,
    const url::Origin& initiator,
    const url::Origin& top,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient> client,
    base::OnceCallback<
        void(mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>)>
        allowed) {
  if (!base::FeatureList::IsEnabled(kYeeContentBlocking)) {
    std::move(allowed).Run(std::move(client));
    return;
  }
  new HandshakeCheck<network::mojom::WebSocketHandshakeClient>(
      context, frame, url, initiator, top, TransportKind::kWebSocket,
      std::move(client),
      base::BindOnce(
          [](base::OnceCallback<void(
                 mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>)>
                 allowed,
             bool blocked,
             mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
                 client) {
            if (blocked) {
              mojo::Remote<network::mojom::WebSocketHandshakeClient> remote(
                  std::move(client));
              Reject(remote.get());
              return;
            }
            std::move(allowed).Run(std::move(client));
          },
          std::move(allowed)));
}
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
        complete) {
  if (!base::FeatureList::IsEnabled(kYeeContentBlocking)) {
    std::move(complete).Run(false, std::move(client));
    return;
  }
  new HandshakeCheck<network::mojom::WebTransportHandshakeClient>(
      context, frame, url, initiator, top, TransportKind::kWebTransport,
      std::move(client), std::move(complete));
}
}  // namespace yee::content_blocking
