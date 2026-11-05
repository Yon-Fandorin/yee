// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_state_manager.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_view.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/base_tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/infobars/infobar_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_aim_presenter.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_presenter_base.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_views.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_container_impl.h"
#include "chrome/browser/ui/views/tabs/tab_style_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation_binding.h"
#include "chrome/browser/ui/views/yee/browser_surface_transition.h"
#include "chrome/browser/ui/views/yee/horizontal_tab_presentation.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "chrome/browser/ui/views/yee/projects/project_navigation_controller.h"
#include "chrome/browser/ui/views/yee/sidebar_footer.h"
#include "chrome/browser/ui/views/yee/site_controls.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/browser/yee_projects/project_service.h"
#include "chrome/browser/yee_projects/project_service_factory.h"
#include "chrome/browser/yee_projects/project_tab_coordinator.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver_observer.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_metadata_provider.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/views/animation/bounds_animator.h"
#include "ui/views/animation/bounds_animator_observer.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee::test {
namespace {

AppliedSnapshot Snapshot(AppliedOwnerToken owner,
                         AppliedConditionValue value,
                         uint64_t serial = 0,
                         AppliedCoordinates coordinates = {}) {
  return {.owner = owner,
          .value = std::move(value),
          .serials = {.apply_serial = serial},
          .coordinates = coordinates};
}

AppliedAxis Axis(split_tabs::SplitTabLayout layout) {
  return layout == split_tabs::SplitTabLayout::kStacked
             ? AppliedAxis::kVertical
             : AppliedAxis::kHorizontal;
}

SurfaceEpochCommitted::Destination Destination(
    SurfaceGeometryStableState state) {
  switch (state) {
    case SurfaceGeometryStableState::kWindowed:
      return SurfaceEpochCommitted::Destination::kWindowed;
    case SurfaceGeometryStableState::kFullscreenHiddenChrome:
      return SurfaceEpochCommitted::Destination::kFullscreen;
    case SurfaceGeometryStableState::kFullscreenWithToolbar:
      return SurfaceEpochCommitted::Destination::kImmersive;
  }
}

}  // namespace

class AppliedBrowserObservers::Impl : public BrowserCollectionObserver {
 public:
  Impl() {
    collection.Observe(GlobalBrowserCollection::GetInstance());
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser) {
          AddBrowser(*browser);
          return true;
        });
    wake = signals.SubscribeWake(
        base::BindRepeating(&Impl::OnNativeWake, weak.GetWeakPtr()));
  }
  ~Impl() override = default;

  struct Lifetime : views::ViewObserver {
    Lifetime(Impl& owner, views::View& view) : owner(owner), view(&view) {
      owner.signals.ObserveView(view);
      observation.Observe(&view);
    }
    void Own(AppliedOwnerToken token) {
      tokens.push_back(token);
      if (view.view())
        owner->signals.TieToView(token, *view.view());
    }
    void OnViewIsDeleting(views::View*) override {
      observation.Reset();
      view.SetView(nullptr);
      subscriptions.clear();
      for (auto token : tokens) {
        owner->signals.OwnerDestroyed(token);
      }
    }
    raw_ref<Impl> owner;
    views::ViewTracker view;
    std::vector<AppliedOwnerToken> tokens;
    std::vector<base::CallbackListSubscription> subscriptions;
    base::ScopedObservation<views::View, views::ViewObserver> observation{this};
  };

  struct RenderWidgetWatch : content::RenderWidgetHostObserver {
    RenderWidgetWatch(Impl& owner, content::RenderWidgetHost& host)
        : owner(owner), host(&host) {
      host.AddObserver(this);
    }
    ~RenderWidgetWatch() override {
      if (host)
        host->RemoveObserver(this);
    }
    void RenderWidgetHostVisibilityChanged(content::RenderWidgetHost*,
                                           bool) override {
      owner->signals.Notify();
    }
    void RenderWidgetHostDidUpdateVisualProperties(
        content::RenderWidgetHost*) override {
      owner->signals.Notify();
    }
    void RenderWidgetHostDestroyed(content::RenderWidgetHost* dying) override {
      dying->RemoveObserver(this);
      host = nullptr;
      owner->signals.OwnerDestroyed(token);
    }
    std::optional<AppliedSnapshot> ReadWidget() const {
      if (!host)
        return std::nullopt;
      auto* view = host->GetView();
      return Snapshot(token,
                      WidgetStateMatches{
                          .visible = view && view->IsShowing(),
                          .bounds = view ? view->GetViewBounds() : gfx::Rect{},
                          .closed = !view || !view->IsShowing()},
                      0, {.space = AppliedCoordinates::Space::kScreen});
    }
    std::optional<AppliedSnapshot> ReadFocus() const {
      if (!host)
        return std::nullopt;
      auto* view = host->GetView();
      return Snapshot(
          token, FocusMatches{.kind = FocusMatches::Kind::kRendererDocument,
                              .target = token,
                              .document = token,
                              .focused = view && view->HasFocus()});
    }
    raw_ref<Impl> owner;
    raw_ptr<content::RenderWidgetHost> host;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
  };

  struct OmniboxWatch : AutocompleteController::Observer {
    OmniboxWatch(Impl& owner, LocationBarView& view, bool aim = false)
        : owner(owner),
          view(&view),
          aim(aim),
          controller(view.GetOmniboxController()->autocomplete_controller()) {
      controller->AddObserver(this);
      popup_state =
          view.GetOmniboxController()
              ->popup_state_manager()
              ->AddPopupStateChangedCallback(base::BindRepeating(
                  [](OmniboxWatch* self, OmniboxPopupState, OmniboxPopupState) {
                    ++self->serial;
                    self->owner->signals.Notify();
                  },
                  base::Unretained(this)));
      owner.signals.ObserveWidget(*view.GetWidget());
      BindPopup();
    }
    ~OmniboxWatch() override {
      if (controller)
        controller->RemoveObserver(this);
    }
    views::Widget* Popup() const {
      auto* location = static_cast<const LocationBarView*>(view.view());
      if (!location)
        return nullptr;
      if (aim) {
        auto* presenter = location->GetOmniboxPopupAimPresenter();
        return presenter ? presenter->get_widget_for_testing() : nullptr;
      }
      auto* popup =
          const_cast<LocationBarView*>(location)->GetOmniboxPopupView();
      if (!popup)
        return nullptr;
      if (auto* presenter = popup->presenter())
        return presenter->get_widget_for_testing();
      return static_cast<OmniboxPopupViewViews*>(popup)->GetWidget();
    }
    void BindPopup() {
      if (auto* popup = Popup(); popup && popup != bound_popup.get()) {
        bound_popup = popup->GetWeakPtr();
        owner->signals.ObserveWidget(*popup);
        owner->signals.ObserveView(*popup->GetRootView());
      }
    }
    void OnResultChanged(AutocompleteController*, bool) override {
      BindPopup();
      ++serial;
      owner->signals.Notify();
    }
    void OnControllerDestroying(AutocompleteController*) override {
      controller->RemoveObserver(this);
      controller = nullptr;
      owner->signals.OwnerDestroyed(token);
    }
    std::optional<AppliedSnapshot> Read() const {
      auto* location = static_cast<const LocationBarView*>(view.view());
      if (!location || !controller)
        return std::nullopt;
      const auto* omni = location->GetOmniboxController();
      auto* popup = Popup();
      return Snapshot(
          token,
          ViewStateMatches{
              .visible = popup && popup->IsVisible(),
              .popup_open = aim ? omni->popup_state_manager()->popup_state() ==
                                      OmniboxPopupState::kAim
                                : omni->IsPopupOpen(),
              .editing = omni->edit_model()->user_input_in_progress(),
              .popup_transition = location->in_popup_state_transition(),
              .autocomplete_empty = controller->result().empty(),
              .presenter_shown =
                  aim && location->GetOmniboxPopupAimPresenter() &&
                  location->GetOmniboxPopupAimPresenter()->IsShown()},
          serial, GetAppliedViewCoordinates(*location));
    }
    std::optional<AppliedSnapshot> ReadPresentation() const {
      auto* location = static_cast<const LocationBarView*>(view.view());
      auto* presenter =
          location ? location->GetOmniboxPopupAimPresenter() : nullptr;
      if (!presenter)
        return std::nullopt;
      return Snapshot(
          token,
          PresentationMatches{
              .presentation_id = static_cast<uint64_t>(
                  presenter->GetCompactShellProviderRebuildCountForTesting())},
          serial, GetAppliedViewCoordinates(*location));
    }
    raw_ref<Impl> owner;
    views::ViewTracker view;
    bool aim;
    base::WeakPtr<views::Widget> bound_popup;
    base::CallbackListSubscription popup_state;
    raw_ptr<AutocompleteController> controller;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    uint64_t serial = 0;
  };

  struct FaviconWatch : content::WebContentsObserver,
                        favicon::FaviconDriverObserver {
    FaviconWatch(Impl& owner, content::WebContents& contents)
        : content::WebContentsObserver(&contents), owner(owner) {
      driver = favicon::ContentFaviconDriver::FromWebContents(&contents);
      if (driver)
        driver->AddObserver(this);
    }
    ~FaviconWatch() override {
      if (driver)
        driver->RemoveObserver(this);
    }
    void WebContentsDestroyed() override {
      if (driver)
        driver->RemoveObserver(this);
      driver = nullptr;
      owner->signals.OwnerDestroyed(token);
    }
    void OnFaviconUpdated(favicon::FaviconDriver*,
                          NotificationIconType,
                          const GURL&,
                          bool,
                          const gfx::Image&) override {
      ++serial;
      owner->signals.Notify();
    }
    std::optional<AppliedSnapshot> Read() const {
      if (!driver)
        return std::nullopt;
      const auto image = driver->GetFavicon();
      uint64_t digest = 0;
      if (!image.IsEmpty()) {
        auto bytes = image.As1xPNGBytes();
        digest = base::PersistentHash(std::string_view(
            reinterpret_cast<const char*>(bytes->front()), bytes->size()));
      }
      return Snapshot(
          token,
          PresentationMatches{
              .icon_id = digest,
              .icon_ready = driver->FaviconIsValid() && !image.IsEmpty()},
          serial);
    }
    raw_ref<Impl> owner;
    raw_ptr<favicon::FaviconDriver> driver = nullptr;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    uint64_t serial = 0;
  };

  struct ColorWatch : content::WebContentsObserver {
    ColorWatch(Impl& owner,
               content::WebContents& contents,
               LocationBarView* view,
               bool renderer_background = false)
        : content::WebContentsObserver(&contents),
          owner(owner),
          consumer(view),
          renderer_background(renderer_background) {
      auto* source =
          BrowserSurfaceColorController::GetOrCreateForWebContents(&contents);
      subscription = source->AddPresentationChangedCallback(base::BindRepeating(
          [](ColorWatch* watch) { watch->owner->signals.Notify(); },
          base::Unretained(this)));
      sampling_subscription =
          source->AddSamplingStateChangedCallback(base::BindRepeating(
              [](ColorWatch* watch,
                 BrowserSurfaceColorController::SamplingSnapshot) {
                watch->owner->signals.Notify();
              },
              base::Unretained(this)));
    }
    void WebContentsDestroyed() override {
      owner->signals.OwnerDestroyed(token);
    }
    void OnBackgroundColorChanged() override { owner->signals.Notify(); }
    void TitleWasSet(content::NavigationEntry*) override {
      owner->signals.Notify();
    }
    std::optional<AppliedSnapshot> Read() const {
      if (!web_contents())
        return std::nullopt;
      const auto* source =
          BrowserSurfaceColorController::GetOrCreateForWebContents(
              web_contents());
      const auto& presentation = source->GetPresentation();
      if (title)
        return Snapshot(
            token, PresentationMatches{
                       .presentation_id = base::PersistentHash(
                           base::UTF16ToUTF8(web_contents()->GetTitle()))});
      if (renderer_background) {
        const auto color = web_contents()->GetBackgroundColor();
        if (!color)
          return std::nullopt;
        return Snapshot(token,
                        PresentationMatches{
                            .background_color = *color,
                            .use_native_theme = color_utils::IsDark(*color)});
      }
      if (!presentation)
        return std::nullopt;
      auto* view = static_cast<const LocationBarView*>(consumer.view());
      return Snapshot(
          token,
          PresentationMatches{
              .source_id = source->source_id(),
              .presentation_id = presentation->revision,
              .background_color = view ? view->GetBackgroundColorForTesting()
                                       : presentation->surface,
              .foreground_color = presentation->primary,
              .popup_revision = presentation->popup_revision,
              .sampling_settled = source->GetSamplingSnapshot().settled},
          presentation->revision,
          view ? GetAppliedViewCoordinates(*view) : AppliedCoordinates{});
    }
    raw_ref<Impl> owner;
    views::ViewTracker consumer;
    bool renderer_background;
    bool title = false;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    base::CallbackListSubscription subscription;
    base::CallbackListSubscription sampling_subscription;
  };

  struct ModelWatch : TabStripModelObserver {
    ModelWatch(Impl& owner, TabStripModel& model)
        : owner(owner), model(&model) {
      model.AddObserver(this);
    }
    ~ModelWatch() override {
      if (model)
        model->RemoveObserver(this);
    }
    void Changed() {
      ++serial;
      owner->BindBrowsers();
      owner->RetireChangedPanes();
      owner->signals.Notify();
      owner->ScheduleBindings();
    }
    void OnTabStripModelChanged(TabStripModel*,
                                const TabStripModelChange&,
                                const TabStripSelectionChange&) override {
      Changed();
    }
    void OnTabPinnedStateChanged(tabs::TabInterface*, int) override {
      Changed();
    }
    void OnTabGroupChanged(const TabGroupChange&) override { Changed(); }
    void TabGroupedStateChanged(TabStripModel*,
                                std::optional<tab_groups::TabGroupId>,
                                std::optional<tab_groups::TabGroupId>,
                                tabs::TabInterface*,
                                int) override {
      Changed();
    }
    void OnSplitTabChanged(const SplitTabChange&) override { Changed(); }
    void OnTabStripModelDestroyed(TabStripModel*) override {
      model = nullptr;
      owner->signals.OwnerDestroyed(token);
      owner->RetireChangedPanes();
    }
    raw_ref<Impl> owner;
    raw_ptr<TabStripModel> model;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    uint64_t serial = 0;
  };

  struct PaneBinding {
    raw_ptr<ModelWatch> model;
    raw_ptr<Lifetime> lifetime;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    AppliedOwnerToken bound_owner;
    uint64_t pane_id = 0;
    uint64_t handle = 0;
    uint64_t split_peer = 0;
    bool bound = false;
    bool require_new_apply = true;
    uint64_t bound_baseline = 0;
    std::map<AppliedOwnerToken, uint64_t> baselines;
    std::unique_ptr<views::ViewTracker> container;
    std::optional<split_tabs::SplitTabId> split;
    base::CallbackListSubscription contents_subscription;
  };

  struct RendererWatch : content::WebContentsObserver,
                         content::RenderWidgetHostObserver {
    struct Metadata : content::RenderFrameSubmissionObserver {
      Metadata(RendererWatch& owner, content::RenderFrameHost& frame)
          : content::RenderFrameSubmissionObserver(&frame), owner(owner) {}
      void OnRenderFrameMetadataChangedBeforeActivation(
          const cc::RenderFrameMetadata&) override {}
      void OnRenderFrameMetadataChangedAfterActivation(
          base::TimeTicks) override {
        owner->Changed(LastRenderFrameMetadata());
      }
      void OnRenderFrameSubmission() override {}
      void OnLocalSurfaceIdChanged(const cc::RenderFrameMetadata&) override {}
      raw_ref<RendererWatch> owner;
    };
    RendererWatch(Impl& owner,
                  content::WebContents& contents,
                  std::optional<gfx::Size> target = std::nullopt,
                  double scale = 1,
                  bool next_document = false,
                  bool allow_current_surface = false)
        : content::WebContentsObserver(&contents),
          owner(owner),
          target(target),
          target_scale(scale),
          awaiting_document(next_document),
          next_document(next_document) {
      Attach(*contents.GetPrimaryMainFrame());
      if (allow_current_surface && !next_document && target &&
          metadata.local_surface_id &&
          metadata.viewport_size_in_pixels == *target &&
          metadata.device_scale_factor == target_scale) {
        bound_surface = metadata.local_surface_id;
        ++serial;
      }
    }
    ~RendererWatch() override = default;
    void Attach(content::RenderFrameHost& host) {
      metadata_observer.reset();
      host_observation.Reset();
      document = host.GetWeakDocumentPtr();
      frame_id = host.GetGlobalId();
      host_observation.Observe(host.GetRenderWidgetHost());
      metadata_observer = std::make_unique<Metadata>(*this, host);
      metadata = metadata_observer->LastRenderFrameMetadata();
      baseline_surface = metadata.local_surface_id;
      if (!target)
        UpdateSurface();
    }
    void Destroy() {
      metadata_observer.reset();
      host_observation.Reset();
      alive = false;
      owner->signals.OwnerDestroyed(token);
    }
    void PrimaryPageChanged(content::Page&) override {
      if (awaiting_document) {
        awaiting_document = false;
        Attach(*web_contents()->GetPrimaryMainFrame());
      } else {
        Destroy();
      }
    }
    void WebContentsDestroyed() override { Destroy(); }
    void RenderFrameDeleted(content::RenderFrameHost* host) override {
      if (host->GetGlobalId() == frame_id && !awaiting_document)
        Destroy();
    }
    void RenderWidgetHostDestroyed(content::RenderWidgetHost*) override {
      if (awaiting_document) {
        metadata_observer.reset();
        host_observation.Reset();
      } else {
        Destroy();
      }
    }
    void UpdateSurface() {
      if (metadata.local_surface_id != surface_id) {
        surface_id = metadata.local_surface_id;
        surface = AppliedOwnerToken::Create();
      }
    }
    void Changed(const cc::RenderFrameMetadata& value) {
      if (!alive || awaiting_document)
        return;
      metadata = value;
      if (target) {
        if (!bound_surface) {
          if (!metadata.local_surface_id ||
              (!next_document &&
               metadata.local_surface_id == baseline_surface) ||
              metadata.viewport_size_in_pixels != *target ||
              metadata.device_scale_factor != target_scale)
            return;
          bound_surface = metadata.local_surface_id;
        } else if (metadata.local_surface_id != bound_surface) {
          // 이미 결합한 surface를 다른 resize의 표본으로 승계하지 않는다.
          Destroy();
          return;
        }
      } else {
        UpdateSurface();
      }
      ++serial;
      owner->signals.Notify();
    }
    RendererViewportMatches Value(gfx::Size pixels, double scale) const {
      return {contents, frame, document_token, surface, pixels, scale};
    }
    std::optional<AppliedSnapshot> Read() const {
      if (!alive || awaiting_document || !document.AsRenderFrameHostIfValid() ||
          !metadata.local_surface_id || (target && !bound_surface))
        return std::nullopt;
      auto result = Snapshot(
          token,
          Value(metadata.viewport_size_in_pixels, metadata.device_scale_factor),
          serial);
      result.serials.native_generation =
          (uint64_t{metadata.local_surface_id->parent_sequence_number()}
           << 32) |
          metadata.local_surface_id->child_sequence_number();
      result.coordinates.units = AppliedCoordinates::Units::kPixel;
      result.coordinates.device_scale_factor = metadata.device_scale_factor;
      result.coordinates.rtl = base::i18n::IsRTL();
      return result;
    }
    raw_ref<Impl> owner;
    content::WeakDocumentPtr document;
    content::GlobalRenderFrameHostId frame_id;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    AppliedOwnerToken contents = AppliedOwnerToken::Create();
    AppliedOwnerToken frame = AppliedOwnerToken::Create();
    AppliedOwnerToken document_token = AppliedOwnerToken::Create();
    AppliedOwnerToken surface = AppliedOwnerToken::Create();
    cc::RenderFrameMetadata metadata;
    std::optional<gfx::Size> target;
    double target_scale;
    std::optional<viz::LocalSurfaceId> baseline_surface;
    std::optional<viz::LocalSurfaceId> bound_surface;
    std::optional<viz::LocalSurfaceId> surface_id;
    uint64_t serial = 0;
    bool alive = true;
    bool awaiting_document;
    bool next_document;
    std::unique_ptr<Metadata> metadata_observer;
    base::ScopedObservation<content::RenderWidgetHost,
                            content::RenderWidgetHostObserver>
        host_observation{this};
  };

  struct DomWatch : content::WebContentsObserver {
    DomWatch(Impl& owner,
             content::RenderFrameHost& frame,
             base::OnceCallback<void(bool)> ready)
        : content::WebContentsObserver(
              content::WebContents::FromRenderFrameHost(&frame)),
          owner(owner),
          document(frame.GetWeakDocumentPtr()),
          ready(std::move(ready)) {
      value.document = token;
    }
    ~DomWatch() override {
      if (auto* frame = document.AsRenderFrameHostIfValid()) {
        content::ExecuteScriptAsync(
            frame, content::JsReplace("window[$1]?.stop()", Key()));
      }
    }
    std::string Key() const { return "yee-applied-" + token.ToString(); }
    void Destroy() {
      installed = false;
      owner->signals.OwnerDestroyed(token);
      if (ready) {
        std::move(ready).Run(false);
      }
    }
    void PrimaryPageChanged(content::Page&) override { Destroy(); }
    void WebContentsDestroyed() override { Destroy(); }
    void RenderFrameDeleted(content::RenderFrameHost* frame) override {
      if (!document.AsRenderFrameHostIfValid() ||
          document.AsRenderFrameHostIfValid() == frame) {
        Destroy();
      }
    }
    void DomOperationResponse(content::RenderFrameHost* frame,
                              const std::string& json) override {
      if (document.AsRenderFrameHostIfValid() != frame) {
        return;
      }
      auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_string()) {
        parsed =
            base::JSONReader::Read(parsed->GetString(), base::JSON_PARSE_RFC);
      }
      if (!parsed || !parsed->is_dict()) {
        return;
      }
      const auto& data = parsed->GetDict();
      const auto* key = data.FindString("yeeWait");
      if (!key || *key != Key()) {
        return;
      }
      const auto* rows = data.FindList("rows");
      const auto x = data.FindDouble("x");
      const auto y = data.FindDouble("y");
      const auto width = data.FindDouble("width");
      const auto height = data.FindDouble("height");
      const auto opacity = data.FindDouble("opacity");
      const auto selected = data.FindInt("selected");
      const auto focused = data.FindBool("focused");
      const auto scale = data.FindDouble("scale");
      const auto rtl = data.FindBool("rtl");
      if (!rows || !x || !y || !width || !height || !opacity || !selected ||
          !focused || !scale || *scale <= 0 || !rtl) {
        Destroy();
        return;
      }
      value.popup_rows.clear();
      for (const auto& row : *rows) {
        if (!row.is_int() || row.GetInt() <= 0) {
          Destroy();
          return;
        }
        value.popup_rows.push_back(row.GetInt());
      }
      value.selected_index = *selected;
      value.rect = gfx::RectF(*x, *y, *width, *height);
      value.opacity = *opacity;
      value.focused = *focused;
      value.hovered_index = data.FindInt("hovered").value_or(-1);
      value.padding_top = data.FindInt("paddingTop").value_or(0);
      value.results_padding = data.FindInt("resultsPadding").value_or(0);
      value.forced_colors = data.FindBool("forcedColors").value_or(false);
      value.prefers_contrast_more =
          data.FindBool("contrastMore").value_or(false);
      value.focus_indicator_visible =
          data.FindBool("focusIndicator").value_or(false);
      value.hover_background =
          static_cast<uint32_t>(data.FindDouble("hoverColor").value_or(0));
      value.customization_ready =
          data.FindBool("customizationReady").value_or(false);
      value.click_count = data.FindInt("clickCount").value_or(0);
      value.row_bounds.clear();
      if (const auto* row_rects = data.FindList("rowBounds")) {
        for (const auto& rect : *row_rects) {
          const auto& list = rect.GetList();
          if (list.size() == 4)
            value.row_bounds.emplace_back(
                list[0].GetDouble(), list[1].GetDouble(), list[2].GetDouble(),
                list[3].GetDouble());
        }
      }
      coordinates.device_scale_factor = *scale;
      coordinates.rtl = *rtl;
      installed = true;
      ++serial;
      owner->signals.Notify();
      if (ready) {
        std::move(ready).Run(true);
      }
    }
    raw_ref<Impl> owner;
    content::WeakDocumentPtr document;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    WebUiStateMatches value;
    views::ViewTracker native_view;
    AppliedCoordinates coordinates;
    uint64_t serial = 0;
    bool installed = false;
    base::OnceCallback<void(bool)> ready;
  };

  Lifetime& Life(views::View& view) {
    auto life = std::make_unique<Lifetime>(*this, view);
    auto* ptr = life.get();
    lifetimes.push_back(std::move(life));
    return *ptr;
  }
  ModelWatch& Model(TabStripModel& model) {
    for (const auto& watch : models) {
      if (watch->model == &model) {
        return *watch;
      }
    }
    auto watch = std::make_unique<ModelWatch>(*this, model);
    auto* ptr = watch.get();
    models.push_back(std::move(watch));
    signals.RegisterSignal(
        ptr->token, AppliedConditionKind::kTabModelMatches,
        base::BindRepeating(&Impl::ReadModel, base::Unretained(this),
                            base::Unretained(ptr)));
    return *ptr;
  }
  uint64_t Split(const split_tabs::SplitTabId& id) {
    auto [it, inserted] = splits.try_emplace(id);
    if (inserted) {
      it->second = splits.size();
    }
    return it->second;
  }
  std::optional<AppliedSnapshot> ReadModel(ModelWatch* watch) {
    if (!watch->model) {
      return std::nullopt;
    }
    TabModelMatches value;
    for (int i = 0; i < watch->model->count(); ++i) {
      const auto* tab = watch->model->GetTabAtIndex(i);
      value.tabs.push_back(
          {static_cast<uint64_t>(tab->GetHandle().raw_value()), tab->IsPinned(),
           tab->GetGroup() ? *AppliedOwnerToken::Deserialize(
                                 tab->GetGroup()->token().high(),
                                 tab->GetGroup()->token().low())
                           : AppliedOwnerToken{},
           tab->GetSplit() ? Split(*tab->GetSplit()) : 0});
    }
    if (auto* active = watch->model->GetActiveTab()) {
      value.active_tab = active->GetHandle().raw_value();
    }
    for (const auto& id : watch->model->ListSplits()) {
      const auto* data = watch->model->GetSplitData(id);
      AppliedSplitState split{Split(id),
                              Axis(data->visual_data()->split_layout()),
                              data->visual_data()->split_ratio()};
      for (const auto* tab : data->ListTabs()) {
        split.tab_handles.push_back(tab->GetHandle().raw_value());
      }
      value.splits.push_back(std::move(split));
    }
    return Snapshot(watch->token, value, watch->serial);
  }
  struct BrowserBinding {
    uint64_t tab_handle;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    std::set<AppliedOwnerToken> existing;
    bool bound = false;
  };
  AppliedOwnerToken BrowserIdentity(AppliedOwnerToken canonical) const {
    const auto alias = browser_aliases.find(canonical);
    return alias == browser_aliases.end() ? canonical : alias->second;
  }
  void BindBrowsers() {
    for (auto& binding : browser_bindings) {
      if (binding.bound)
        continue;
      for (const auto& [browser, canonical] : browsers) {
        if (binding.existing.contains(canonical) ||
            browser_aliases.contains(canonical))
          continue;
        auto* model = browser->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          if (static_cast<uint64_t>(
                  model->GetTabAtIndex(i)->GetHandle().raw_value()) ==
              binding.tab_handle) {
            browser_aliases[canonical] = binding.token;
            binding.bound = true;
            break;
          }
        }
        if (binding.bound)
          break;
      }
    }
  }
  void AddBrowser(BrowserWindowInterface& browser) {
    browsers[&browser] = AppliedOwnerToken::Create();
    Model(*browser.GetTabStripModel());
  }
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    AddBrowser(*browser);
    created.push_back(browsers.at(browser));
    BindBrowsers();
    ++browser_serial;
    signals.Notify();
  }
  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    if (const auto found = browsers.find(browser); found != browsers.end()) {
      closed.push_back(found->second);
      signals.OwnerDestroyed(found->second);
      signals.OwnerDestroyed(BrowserIdentity(found->second));
      browsers.erase(found);
    }
    ++browser_serial;
    signals.Notify();
  }
  std::optional<AppliedSnapshot> ReadBrowsers() {
    BrowserSetMatches value;
    for (auto token : created)
      value.created.push_back(BrowserIdentity(token));
    for (auto token : closed)
      value.closed.push_back(BrowserIdentity(token));
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser) {
          AppliedBrowserState state{.browser =
                                        BrowserIdentity(browsers.at(browser))};
          auto* model = browser->GetTabStripModel();
          for (int i = 0; i < model->count(); ++i) {
            state.tab_handles.push_back(
                model->GetTabAtIndex(i)->GetHandle().raw_value());
          }
          value.browsers.push_back(std::move(state));
          return true;
        });
    return Snapshot(browser_set, value, browser_serial);
  }
  bool PaneIsCurrent(const PaneBinding& binding) const {
    if (!binding.model->model || !binding.lifetime->view.view())
      return false;
    if (!binding.bound)
      return true;
    auto* container =
        static_cast<ContentsContainerView*>(binding.container->view());
    auto* contents =
        container ? container->contents_view()->web_contents() : nullptr;
    const auto* tab =
        contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
    if (!tab ||
        static_cast<uint64_t>(tab->GetHandle().raw_value()) != binding.handle ||
        tab->GetSplit() != binding.split ||
        !signals.IsOwnerAlive(binding.bound_owner))
      return false;
    auto* outlet = container->GetYeeBrowserSurfacePresentationBinding();
    return !outlet || outlet->web_contents() == contents;
  }
  void RetireChangedPanes() {
    for (const auto& binding : panes) {
      if (signals.IsOwnerAlive(binding->token) && !PaneIsCurrent(*binding)) {
        binding->contents_subscription = {};
        signals.OwnerDestroyed(binding->token);
      }
    }
  }
  void OnNativeWake() {
    for (const auto& omnibox : omniboxes)
      omnibox->BindPopup();
    RetireChangedPanes();
    ScheduleBindings();
  }
  void ScheduleBindings() {
    if (binding_queued || !std::ranges::any_of(panes, [&](const auto& binding) {
          return !binding->bound && signals.IsOwnerAlive(binding->token);
        })) {
      return;
    }
    binding_queued = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Impl::BindPanes, weak.GetWeakPtr()));
  }
  void BindPanes() {
    binding_queued = false;
    bool changed = false;
    for (const auto& binding : panes) {
      if (binding->bound || !signals.IsOwnerAlive(binding->token) ||
          !binding->model->model || !binding->lifetime->view.view()) {
        continue;
      }
      auto* view =
          static_cast<MultiContentsView*>(binding->lifetime->view.view());
      for (const auto& container : view->contents_container_views()) {
        auto* contents = container->contents_view()->web_contents();
        const auto* tab =
            contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
        if (!tab || static_cast<uint64_t>(tab->GetHandle().raw_value()) !=
                        binding->handle) {
          continue;
        }
        if (binding->split_peer) {
          if (!tab->GetSplit())
            continue;
          const auto peers =
              binding->model->model->GetSplitData(*tab->GetSplit())->ListTabs();
          if (!std::ranges::any_of(peers, [&](const auto* peer) {
                return static_cast<uint64_t>(peer->GetHandle().raw_value()) ==
                       binding->split_peer;
              }))
            continue;
        }
        auto* clip = container->GetViewByID(kPageViewportClipHostViewId);
        if (!clip || !PageViewportAppliedState::From(*clip)) {
          continue;
        }
        binding->bound_owner =
            signals.ObservePageHosts(*clip, binding->pane_id, binding->handle);
        if (const auto baseline = binding->baselines.find(binding->bound_owner);
            baseline != binding->baselines.end())
          binding->bound_baseline = baseline->second;
        binding->container =
            std::make_unique<views::ViewTracker>(container.get());
        binding->bound = true;
        binding->split = tab->GetSplit();
        if (auto* outlet =
                container->GetYeeBrowserSurfacePresentationBinding()) {
          binding->contents_subscription =
              outlet->AddPresentationChangedCallback(base::BindRepeating(
                  &Impl::RetireChangedPanes, weak.GetWeakPtr()));
        }
        changed = true;
        break;
      }
    }
    if (changed) {
      signals.Notify();
    }
  }

  AppliedViewsObservers signals;
  std::map<BrowserWindowInterface*, AppliedOwnerToken> browsers;
  std::map<AppliedOwnerToken, AppliedOwnerToken> browser_aliases;
  std::vector<BrowserBinding> browser_bindings;
  std::map<split_tabs::SplitTabId, uint64_t> splits;
  std::vector<AppliedOwnerToken> created;
  std::vector<AppliedOwnerToken> closed;
  AppliedOwnerToken browser_set = AppliedOwnerToken::Create();
  uint64_t browser_serial = 0;
  bool browser_set_registered = false;
  bool binding_queued = false;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      collection{this};
  std::vector<std::unique_ptr<ModelWatch>> models;
  std::vector<std::unique_ptr<ColorWatch>> colors;
  std::vector<std::unique_ptr<FaviconWatch>> favicons;
  std::vector<std::unique_ptr<OmniboxWatch>> omniboxes;
  std::vector<std::unique_ptr<RenderWidgetWatch>> render_widgets;
  std::vector<std::unique_ptr<Lifetime>> lifetimes;
  std::vector<std::unique_ptr<PaneBinding>> panes;
  std::vector<std::unique_ptr<RendererWatch>> renderers;
  std::vector<std::unique_ptr<DomWatch>> doms;
  base::CallbackListSubscription wake;
  base::WeakPtrFactory<Impl> weak{this};
};

AppliedBrowserObservers::AppliedBrowserObservers()
    : impl_(std::make_unique<Impl>()) {}
AppliedBrowserObservers::~AppliedBrowserObservers() = default;
AppliedViewsObservers& AppliedBrowserObservers::views() {
  return impl_->signals;
}
AppliedOwnerToken AppliedBrowserObservers::BrowserToken(
    BrowserWindowInterface& browser) {
  return impl_->BrowserIdentity(impl_->browsers.at(&browser));
}
uint64_t AppliedBrowserObservers::SplitId(const TabStripModel& model,
                                          int tab_index) {
  const auto split = model.GetSplitForTab(tab_index);
  return split ? impl_->Split(*split) : 0;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveTabModel(
    TabStripModel& model) {
  return impl_->Model(model).token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveBrowserSet() {
  if (!impl_->browser_set_registered) {
    impl_->browser_set_registered = true;
    impl_->signals.RegisterSignal(
        impl_->browser_set, AppliedConditionKind::kBrowserSetMatches,
        base::BindRepeating(&Impl::ReadBrowsers,
                            base::Unretained(impl_.get())));
  }
  return impl_->browser_set;
}
AppliedOwnerToken AppliedBrowserObservers::ExpectBrowserForTab(
    uint64_t handle) {
  Impl::BrowserBinding binding{.tab_handle = handle};
  for (const auto& [browser, canonical] : impl_->browsers)
    binding.existing.insert(canonical);
  const auto token = binding.token;
  impl_->browser_bindings.push_back(std::move(binding));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObservePaneLayout(
    BrowserWindowInterface& browser,
    MultiContentsView& panes) {
  auto& model = impl_->Model(*browser.GetTabStripModel());
  auto& lifetime = impl_->Life(panes);
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kPaneLayoutMatches,
      base::BindRepeating(
          [](Impl* impl, Impl::Lifetime* life, Impl::ModelWatch* watch,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            auto* view = static_cast<MultiContentsView*>(life->view.view());
            if (!view || !watch->model) {
              return std::nullopt;
            }
            PaneLayoutMatches value{
                .axis = Axis(view->GetSplitLayout()),
                .parent_extent = view->size(),
                .ratio = view->GetSplitRatio(),
                .insets = view->split_view_insets_for_testing(),
                .divider = view->resize_area_for_testing()->bounds()};
            if (const auto* active = watch->model->GetActiveTab();
                active && active->GetSplit()) {
              value.split_id = impl->Split(*active->GetSplit());
            }
            for (const auto& container : view->contents_container_views()) {
              if (container->GetVisible() &&
                  container->contents_view()->web_contents()) {
                value.tab_handles.push_back(
                    tabs::TabInterface::GetFromContents(
                        container->contents_view()->web_contents())
                        ->GetHandle()
                        .raw_value());
              }
            }
            if (view->IsInSplitView()) {
              value.start_parent_extent =
                  view->start_contents_view_for_testing()->parent()->size();
              value.end_parent_extent =
                  view->end_contents_view_for_testing()->parent()->size();
              value.start_container_extent =
                  view->contents_container_views()[0]->size();
              value.end_container_extent =
                  view->contents_container_views()[1]->size();
              const bool horizontal = value.axis == AppliedAxis::kHorizontal;
              const int start = horizontal
                                    ? value.start_container_extent.width()
                                    : value.start_container_extent.height();
              const int end = horizontal ? value.end_container_extent.width()
                                         : value.end_container_extent.height();
              const int divider =
                  horizontal ? value.divider.width() : value.divider.height();
              const int extent =
                  horizontal
                      ? value.parent_extent.width() - value.insets.width()
                      : value.parent_extent.height() - value.insets.height();
              value.geometry_matches_ratio =
                  !value.start_parent_extent.IsEmpty() &&
                  !value.end_parent_extent.IsEmpty() && start > 0 && end > 0 &&
                  start + end + divider == extent &&
                  start == std::round(value.ratio * (extent - divider));
            } else {
              value.geometry_matches_ratio = true;
            }
            return Snapshot(token, value, watch->serial,
                            GetAppliedViewCoordinates(*view));
          },
          base::Unretained(impl_.get()), base::Unretained(&lifetime),
          base::Unretained(&model), token));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ExpectPageHosts(
    BrowserWindowInterface& browser,
    MultiContentsView& panes,
    uint64_t pane_id,
    uint64_t tab_handle,
    bool require_new_apply,
    uint64_t split_peer) {
  auto binding = std::make_unique<Impl::PaneBinding>();
  auto* ptr = binding.get();
  ptr->model = &impl_->Model(*browser.GetTabStripModel());
  ptr->lifetime = &impl_->Life(panes);
  ptr->lifetime->Own(ptr->token);
  ptr->pane_id = pane_id;
  ptr->handle = tab_handle;
  ptr->split_peer = split_peer;
  ptr->require_new_apply = require_new_apply;
  for (const auto& container : panes.contents_container_views()) {
    if (auto* clip = container->GetViewByID(kPageViewportClipHostViewId)) {
      if (auto* state = PageViewportAppliedState::From(*clip)) {
        const auto value = state->GetSnapshot();
        ptr->baselines[value.owner] = value.apply_serial;
      }
    }
  }
  impl_->panes.push_back(std::move(binding));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPageHostsApplied,
      base::BindRepeating(
          [](Impl* impl,
             Impl::PaneBinding* binding) -> std::optional<AppliedSnapshot> {
            if (!binding->bound || !impl->PaneIsCurrent(*binding)) {
              return std::nullopt;
            }
            auto snapshot =
                impl->signals.Read({binding->bound_owner,
                                    AppliedConditionKind::kPageHostsApplied});
            if (snapshot && binding->require_new_apply &&
                snapshot->serials.apply_serial <= binding->bound_baseline)
              return std::nullopt;
            if (snapshot) {
              snapshot->owner = binding->token;
              auto& value = std::get<PageHostsApplied>(snapshot->value);
              value.pane_id = binding->pane_id;
              value.tab_handle = binding->handle;
            }
            return snapshot;
          },
          base::Unretained(impl_.get()), base::Unretained(ptr)));
  impl_->BindPanes();
  return ptr->token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveSurfaceEpoch(
    views::View& view) {
  auto& lifetime = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  if (view.GetWidget()) {
    impl_->signals.ObserveWidget(*view.GetWidget());
    impl_->signals.ObserveNativeWindow(*view.GetWidget());
  }
  if (!GetSurfaceGeometryTransition(view)) {
    return token;
  }
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kSurfaceEpochCommitted,
      base::BindRepeating(
          [](Impl::Lifetime* life,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            if (!life->view.view()) {
              return std::nullopt;
            }
            const auto* epoch =
                GetSurfaceGeometryTransition(*life->view.view());
            if (!epoch) {
              return std::nullopt;
            }
            auto result = Snapshot(
                token,
                SurfaceEpochCommitted{
                    epoch->generation,
                    epoch->phase == SurfaceGeometryTransitionPhase::kStable
                        ? SurfaceEpochCommitted::Phase::kStable
                        : SurfaceEpochCommitted::Phase::kTransitioning,
                    epoch->committed_generation,
                    Destination(epoch->destination.stable_state)},
                epoch->committed_generation);
            result.coordinates = GetAppliedViewCoordinates(*life->view.view());
            result.request_epoch = epoch->generation;
            result.serials.native_generation = epoch->generation;
            return result;
          },
          base::Unretained(&lifetime), token));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveBrowserAnimation(
    views::View& view,
    BrowserAnimationController& controller,
    BrowserAnimationGroup group,
    BrowserAnimationSequence sequence,
    uint64_t group_id) {
  auto& lifetime = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  auto serial = std::make_shared<uint64_t>(0);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, BrowserAnimationController* controller,
             BrowserAnimationGroup group, BrowserAnimationSequence sequence,
             uint64_t id, AppliedOwnerToken token,
             std::shared_ptr<uint64_t> serial)
              -> std::optional<AppliedSnapshot> {
            if (!life->view.view()) {
              return std::nullopt;
            }
            const auto progress = controller->GetCurrentValue(group, sequence);
            const double value =
                progress.value_or(controller->IsAnimating(group) ? 0.0 : 1.0);
            return Snapshot(token,
                            AnimationMatches{id, controller->IsAnimating(group),
                                             value, value},
                            *serial,
                            GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(&lifetime), base::Unretained(&controller), group,
          sequence, group_id, token, serial));
  lifetime.subscriptions.push_back(controller.Subscribe(
      group, base::BindRepeating(
                 [](Impl* impl, std::shared_ptr<uint64_t> serial,
                    const BrowserAnimationController*, BrowserAnimationUpdate) {
                   ++*serial;
                   impl->signals.Notify();
                 },
                 base::Unretained(impl_.get()), serial)));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveInfoBarAnimation(
    const infobars::InfoBar& bar,
    uint64_t group_id) {
  auto& view = static_cast<InfoBarView&>(const_cast<infobars::InfoBar&>(bar));
  auto& life = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  life.Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, AppliedOwnerToken token,
             uint64_t group) -> std::optional<AppliedSnapshot> {
            const auto* view =
                static_cast<const InfoBarView*>(life->view.view());
            if (!view)
              return std::nullopt;
            const auto& animation = view->animation();
            return Snapshot(token,
                            AnimationMatches{group, animation.is_animating(),
                                             animation.GetCurrentValue(),
                                             animation.GetCurrentValue()},
                            0, GetAppliedViewCoordinates(*view));
          },
          base::Unretained(&life), token, group_id));
  return token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveTabContainerAnimation(
    TabContainerImpl& host,
    uint64_t group_id,
    views::View* animated_view) {
  auto& life = impl_->Life(host);
  auto* target = animated_view ? &impl_->Life(*animated_view) : nullptr;
  const auto token = AppliedOwnerToken::Create();
  life.Own(token);
  if (target)
    target->Own(token);
  // BoundsAnimator는 파생 View의 멤버여서 base View 삭제 알림보다 먼저 죽는다.
  // View bounds/invalidation·compositor commit을 깨움으로 쓰고 현재 상태만
  // 읽는다.
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, Impl::Lifetime* target,
             AppliedOwnerToken token,
             uint64_t group) -> std::optional<AppliedSnapshot> {
            auto* host = static_cast<TabContainerImpl*>(life->view.view());
            if (!host)
              return std::nullopt;
            if (target) {
              auto* view = target->view.view();
              if (!view || view->parent() != host)
                return std::nullopt;
              // 공개 getter는 const animator를 반환하지만 조회 함수 자체는
              // non-const다. delegate·observer·animation은 변경하지 않는다.
              auto& animator = const_cast<views::BoundsAnimator&>(
                  host->GetBoundsAnimatorForTesting());
              const auto* animation = animator.GetAnimationForView(view);
              const double progress =
                  animation ? animation->GetCurrentValue() : 1.0;
              return Snapshot(
                  token,
                  AnimationMatches{group, animator.IsAnimating(view), progress,
                                   progress},
                  0, GetAppliedViewCoordinates(*host));
            }
            const bool running = host->IsAnimating();
            return Snapshot(token, AnimationMatches{group, running, 0, 0}, 0,
                            GetAppliedViewCoordinates(*host));
          },
          base::Unretained(&life), base::Unretained(target), token, group_id));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveUnifiedAnimation(
    views::View& view,
    TabCollectionAnimatingLayoutManager& manager,
    uint64_t group_id) {
  CHECK_EQ(view.GetLayoutManager(), &manager);
  auto& lifetime = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  const auto host_token = impl_->signals.ObserveView(view);
  lifetime.Own(token);
  impl_->signals.TieToLayoutManager(token, view, manager);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl* impl, AppliedOwnerToken host_token, Impl::Lifetime* life,
             uint64_t group_id,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            // TieToLayoutManager가 교체된 manager의 읽기를 먼저 막는다.
            if (!life->view.view() || !impl->signals.IsOwnerAlive(token)) {
              return std::nullopt;
            }
            auto* manager = static_cast<TabCollectionAnimatingLayoutManager*>(
                life->view.view()->GetLayoutManager());
            const bool running = manager->is_animating();
            const double progress =
                running ? manager->animation_coordinator().GetCurrentProgress()
                        : 1.0;
            const auto host = impl->signals.Read(
                {host_token, AppliedConditionKind::kViewStateMatches});
            return Snapshot(
                token, AnimationMatches{group_id, running, progress, progress},
                host ? host->serials.apply_serial : 0,
                GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(impl_.get()), host_token,
          base::Unretained(&lifetime), group_id, token));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveTabCollectionAnimations(
    BrowserView& root,
    uint64_t group_id) {
  auto& life = impl_->Life(root);
  const auto token = AppliedOwnerToken::Create();
  life.Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, AppliedOwnerToken token,
             uint64_t group) -> std::optional<AppliedSnapshot> {
            auto* browser = static_cast<BrowserView*>(life->view.view());
            if (!browser || !browser->tab_strip_view())
              return std::nullopt;
            auto* root = browser->tab_strip_view()->GetTabStripView();
            if (!root)
              return std::nullopt;
            const auto running = [&](auto&& self, views::View* view) -> bool {
              // 생성 직후 예약된 layout도 첫 animation tick 전에 완료로 보지
              // 않는다.
              if (view->IsDrawn() && view->needs_layout())
                return true;
              if ((views::IsViewClass<TabGroupView>(view) ||
                   views::IsViewClass<UnpinnedTabContainerView>(view)) &&
                  view->GetLayoutManager() &&
                  static_cast<TabCollectionAnimatingLayoutManager*>(
                      view->GetLayoutManager())
                      ->is_animating())
                return true;
              for (views::View* child : view->children())
                if (self(self, child))
                  return true;
              return false;
            };
            const bool animating = running(running, root);
            const double progress = animating ? 0.0 : 1.0;
            return Snapshot(
                token, AnimationMatches{group, animating, progress, progress},
                0, GetAppliedViewCoordinates(*root));
          },
          base::Unretained(&life), token, group_id));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveRendererViewport(
    content::WebContents& contents) {
  auto watch = std::make_unique<Impl::RendererWatch>(*impl_, contents);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kRendererViewportMatches,
      base::BindRepeating(&Impl::RendererWatch::Read, base::Unretained(ptr)));
  impl_->renderers.push_back(std::move(watch));
  return ptr->token;
}
AppliedCondition AppliedBrowserObservers::ExpectRendererViewport(
    content::WebContents& contents,
    gfx::Size pixels,
    double scale,
    bool next_document,
    bool allow_current_surface) {
  CHECK_GT(scale, 0);
  auto watch = std::make_unique<Impl::RendererWatch>(
      *impl_, contents, pixels, scale, next_document, allow_current_surface);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kRendererViewportMatches,
      base::BindRepeating(&Impl::RendererWatch::Read, base::Unretained(ptr)));
  impl_->renderers.push_back(std::move(watch));
  AppliedCondition condition{ptr->token, ptr->Value(pixels, scale)};
  if (allow_current_surface)
    condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
  condition.coordinates.units = AppliedCoordinates::Units::kPixel;
  condition.coordinates.device_scale_factor = scale;
  condition.coordinates.rtl = base::i18n::IsRTL();
  return condition;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveRenderWidgetHost(
    content::RenderWidgetHost& host) {
  auto watch = std::make_unique<Impl::RenderWidgetWatch>(*impl_, host);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kWidgetStateMatches,
      base::BindRepeating(&Impl::RenderWidgetWatch::ReadWidget,
                          base::Unretained(ptr)));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kFocusMatches,
      base::BindRepeating(&Impl::RenderWidgetWatch::ReadFocus,
                          base::Unretained(ptr)));
  impl_->render_widgets.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::InstallWebUiObserver(
    content::RenderFrameHost& frame,
    WebUiAppliedSelectors selectors,
    base::OnceCallback<void(bool)> ready) {
  auto watch =
      std::make_unique<Impl::DomWatch>(*impl_, frame, std::move(ready));
  auto* ptr = watch.get();
  impl_->doms.push_back(std::move(watch));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kWebUiStateMatches,
      base::BindRepeating(
          [](Impl::DomWatch* watch) -> std::optional<AppliedSnapshot> {
            if (!watch->installed ||
                !watch->document.AsRenderFrameHostIfValid()) {
              return std::nullopt;
            }
            auto value = watch->value;
            if (const auto* view = watch->native_view.view();
                view && view->GetWidget()) {
              value.native_view_bounds_in_screen = view->GetBoundsInScreen();
              value.native_widget_bounds_in_screen =
                  view->GetWidget()->GetWindowBoundsInScreen();
            }
            return Snapshot(watch->token, std::move(value), watch->serial,
                            watch->coordinates);
          },
          base::Unretained(ptr)));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kFocusMatches,
      base::BindRepeating(
          [](Impl::DomWatch* watch) -> std::optional<AppliedSnapshot> {
            if (!watch->installed ||
                !watch->document.AsRenderFrameHostIfValid()) {
              return std::nullopt;
            }
            return Snapshot(
                watch->token,
                FocusMatches{FocusMatches::Kind::kRendererDocument,
                             watch->token, watch->token, watch->value.focused},
                watch->serial, watch->coordinates);
          },
          base::Unretained(ptr)));
  base::ListValue shadow;
  for (const auto& selector : selectors.shadow_hosts) {
    shadow.Append(selector);
  }
  content::ExecuteScriptAsync(
      &frame, content::JsReplace(R"JS(
    (() => {
      const key = $1;
      let tree = document;
      for (const selector of $2) {
        tree = tree?.querySelector(selector)?.shadowRoot;
        if (!tree) {
          domAutomationController.send({yeeWait:key});
          return;
        }
      }
      const root = tree?.querySelector($3);
      if (!root) {
        domAutomationController.send({yeeWait:key});
        return;
      }
      const probe = $6;
      let next = 0, clickCount = 0;
      const watched = new WeakSet();
      const ids = new WeakMap();
      let queued = false, stopped = false;
      const send = () => {
        queued = false;
        if (stopped) return;
        let rows = $4 ? Array.from(root.querySelectorAll($4)) : [];
        let selected = rows.findIndex(row => $5 && row.matches($5));
        let hovered = -1, paddingTop = 0, resultsPadding = 0, hoverColor = 0;
        let focusIndicator = false, customizationReady = false;
        let measured = root;
        if (probe === 1) {
          const app = document.querySelector('omnibox-popup-app')?.shadowRoot;
          const dropdown = app?.querySelector('cr-searchbox-dropdown')?.shadowRoot;
          const matches = Array.from(dropdown?.querySelectorAll('cr-searchbox-match') || []);
          rows = matches.map(match => match.shadowRoot?.querySelector('.container:not(.actions)'))
              .filter(row => row && row.getBoundingClientRect().height > 0);
          selected = matches.findIndex(match => match.matches('[selected]'));
          hovered = rows.findIndex(row => row.matches(':hover'));
          const wrapper = app?.querySelector('.dropdownContainer');
          if (wrapper) { measured = wrapper; paddingTop = parseInt(getComputedStyle(wrapper).paddingTop) || 0; }
          resultsPadding = parseInt(document.documentElement.style.getPropertyValue('--omnibox-popup-results-padding-top')) || 0;
          const indicator = matches[selected]?.shadowRoot?.querySelector('#focusIndicator');
          focusIndicator = !!indicator && getComputedStyle(indicator).display !== 'none';
          if (rows[0]) {
            const canvas = document.createElement('canvas');
            canvas.width = canvas.height = 1;
            const context = canvas.getContext('2d');
            context.fillStyle = getComputedStyle(rows[0]).getPropertyValue('--color-searchbox-results-background-hovered');
            context.fillRect(0,0,1,1);
            const c = context.getImageData(0,0,1,1).data;
            hoverColor = ((c[3]<<24) | (c[0]<<16) | (c[1]<<8) | c[2]) >>> 0;
          }
        } else if (probe === 2) {
          const app = document.querySelector('customize-chrome-app')?.shadowRoot;
          const layout = app?.querySelector('yee-tab-layout')?.shadowRoot?.querySelector('#layout:not(:disabled)');
          const shortcuts = app?.querySelector('customize-chrome-shortcuts')?.shadowRoot;
          const toggle = shortcuts?.querySelector('#showToggle');
          const collapse = shortcuts?.querySelector('cr-collapse');
          const radio = shortcuts?.querySelector('#radioSelection');
          customizationReady = !!(layout && toggle && collapse && radio?.selected &&
              shortcuts.querySelector('#radioSelection cr-radio-button') &&
              collapse.classList.contains(toggle.checked ? 'collapse-opened' : 'collapse-closed'));
        } else if (probe === 4) {
          const app = document.querySelector('customize-chrome-app')?.shadowRoot;
          customizationReady = !!app?.querySelector('yee-tab-layout')?.shadowRoot
              ?.querySelector('#layout:not(:disabled)');
        }
        const watch = node => {
          if (!node) return;
          if (!watched.has(node)) {
            watched.add(node);
            mutation.observe(node, {subtree:true, attributes:true, childList:true});
            if (node.nodeType === Node.ELEMENT_NODE) resize.observe(node);
          }
          for (const child of node.querySelectorAll('*')) if (child.shadowRoot) watch(child.shadowRoot);
        };
        watch(root);
        for (const row of rows) { watch(row.getRootNode()); resize.observe(row); }
        const rect = measured.getBoundingClientRect();
        domAutomationController.send({yeeWait:key,
          rows: rows.map(row => {
            if (!ids.has(row)) ids.set(row, ++next);
            return ids.get(row);
          }),
          selected, hovered, paddingTop, resultsPadding, hoverColor, focusIndicator,
          forcedColors:matchMedia('(forced-colors: active)').matches,
          contrastMore:matchMedia('(prefers-contrast: more)').matches,
          rowBounds:rows.map(row => { const r=row.getBoundingClientRect(); return [r.x,r.y,r.width,r.height]; }),
          customizationReady, clickCount,
          x:rect.x, y:rect.y, width:rect.width, height:rect.height,
          opacity:Number(getComputedStyle(root).opacity), focused:document.hasFocus(),
          scale:window.devicePixelRatio, rtl:getComputedStyle(root).direction === 'rtl'});
      };
      const wake = () => {
        if (!queued && !stopped) { queued = true; queueMicrotask(send); }
      };
      const mutation = new MutationObserver(wake);
      mutation.observe(root, {subtree:true, attributes:true, childList:true});
      const resize = new ResizeObserver(wake);
      resize.observe(root);
      const events = ['mouseover','mouseout','pointerover','pointerout','keydown','keyup','focus','blur','focusin','focusout','transitionrun',
                      'transitionend','transitioncancel','animationstart',
                      'animationend','animationcancel'];
      for (const name of events) window.addEventListener(name, wake, true);
      const click = () => { ++clickCount; wake(); };
      window.addEventListener('click', click, true);
      const media = [matchMedia('(forced-colors: active)'), matchMedia('(prefers-contrast: more)')];
      for (const query of media) query.addEventListener('change', wake);
      window[key] = {stop() {
        stopped = true; mutation.disconnect(); resize.disconnect();
        for (const name of events) window.removeEventListener(name, wake, true);
        window.removeEventListener('click', click, true);
        for (const query of media) query.removeEventListener('change', wake);
        delete window[key];
      }};
      send();
    })();
  )JS",
                                 ptr->Key(), std::move(shadow), selectors.root,
                                 selectors.rows, selectors.selected,
                                 static_cast<int>(selectors.probe)));
  return ptr->token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveProjectNavigation(
    BrowserView& view) {
  auto& life = impl_->Life(view);
  auto* controller =
      projects::ProjectNavigationController::From(view.browser());
  if (!controller)
    return AppliedOwnerToken::Create();
  const auto token = controller->GetAppliedSnapshot().owner;
  if (impl_->signals.Read({token, AppliedConditionKind::kViewStateMatches}))
    return token;
  life.Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kViewStateMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life,
             base::WeakPtr<projects::ProjectNavigationController> controller,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            if (!life->view.view() || !controller)
              return std::nullopt;
            const auto snapshot = controller->GetAppliedSnapshot();
            return Snapshot(
                token,
                ViewStateMatches{.visible = snapshot.showing_hub,
                                 .refresh_pending = snapshot.refresh_pending},
                snapshot.apply_serial,
                GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(&life), controller->GetWeakPtr(), token));
  life.subscriptions.push_back(controller->SubscribeApplied(base::BindRepeating(
      [](Impl* impl, const projects::ProjectNavigationAppliedSnapshot& value) {
        if (value.destroyed)
          impl->signals.OwnerDestroyed(value.owner);
        else
          impl->signals.Notify();
      },
      base::Unretained(impl_.get()))));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveProjectPage(
    BrowserView& view,
    std::string page_id) {
  auto& life = impl_->Life(view);
  auto* service = projects::ProjectServiceFactory::GetForProfile(
      view.browser()->GetProfile());
  auto* coordinator = projects::ProjectTabCoordinator::GetForProfile(
      view.browser()->GetProfile());
  const auto token = AppliedOwnerToken::Create();
  life.Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kTabModelMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, projects::ProjectService* service,
             projects::ProjectTabCoordinator* coordinator, std::string id,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            auto* view = static_cast<BrowserView*>(life->view.view());
            if (!view)
              return std::nullopt;
            const auto* page = service->FindPage(id);
            const auto* tab = coordinator->FindLiveTab(id, view->browser());
            return Snapshot(
                token,
                TabModelMatches{
                    .project_page = base::PersistentHash(id),
                    .project_title = page ? base::PersistentHash(
                                                base::UTF16ToUTF8(page->title))
                                          : 0,
                    .project_live_tab = tab ? static_cast<uint64_t>(
                                                  tab->GetHandle().raw_value())
                                            : 0},
                0, GetAppliedViewCoordinates(*view));
          },
          base::Unretained(&life), base::Unretained(service),
          base::Unretained(coordinator), page_id, token));
  life.subscriptions.push_back(service->Subscribe(base::BindRepeating(
      &AppliedViewsObservers::Notify, base::Unretained(&impl_->signals),
      std::vector<AppliedSnapshot>{})));
  life.subscriptions.push_back(coordinator->Subscribe(base::BindRepeating(
      &AppliedViewsObservers::Notify, base::Unretained(&impl_->signals),
      std::vector<AppliedSnapshot>{})));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveOmnibox(
    LocationBarView& view) {
  auto watch = std::make_unique<Impl::OmniboxWatch>(*impl_, view);
  auto* ptr = watch.get();
  impl_->Life(view).Own(ptr->token);
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kViewStateMatches,
      base::BindRepeating(&Impl::OmniboxWatch::Read, base::Unretained(ptr)));
  impl_->omniboxes.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveOmniboxAim(
    LocationBarView& view) {
  auto watch = std::make_unique<Impl::OmniboxWatch>(*impl_, view, true);
  auto* ptr = watch.get();
  impl_->Life(view).Own(ptr->token);
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kViewStateMatches,
      base::BindRepeating(&Impl::OmniboxWatch::Read, base::Unretained(ptr)));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(&Impl::OmniboxWatch::ReadPresentation,
                          base::Unretained(ptr)));
  impl_->omniboxes.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveFavicon(
    content::WebContents& contents) {
  auto watch = std::make_unique<Impl::FaviconWatch>(*impl_, contents);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(&Impl::FaviconWatch::Read, base::Unretained(ptr)));
  impl_->favicons.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveDocumentTitle(
    content::WebContents& contents) {
  auto watch = std::make_unique<Impl::ColorWatch>(*impl_, contents, nullptr);
  auto* ptr = watch.get();
  ptr->title = true;
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(&Impl::ColorWatch::Read, base::Unretained(ptr)));
  impl_->colors.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveWebUiPopupBounds(
    AppliedOwnerToken document,
    views::View& native_view) {
  const auto found = std::ranges::find_if(
      impl_->doms, [&](const auto& dom) { return dom->token == document; });
  CHECK(found != impl_->doms.end());
  CHECK((*found)->installed);
  CHECK(native_view.GetWidget());
  (*found)->native_view.SetView(&native_view);
  impl_->Life(native_view).Own(document);
  impl_->signals.ObserveWidget(*native_view.GetWidget());
  return document;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveUiState(views::View& view,
                                                          UiProbe probe) {
  auto& life = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  life.Own(token);
  if (probe == UiProbe::kSiteControlsPopup) {
    auto* button = views::AsViewClass<SiteControlsButton>(&view);
    if (!button)
      return token;
    life.subscriptions.push_back(
        button->SubscribePopupApplied(base::BindRepeating(
            [](Impl* impl, const SiteControlsPopupAppliedSnapshot&) {
              impl->signals.Notify();
            },
            base::Unretained(impl_.get()))));
  }
  if (probe == UiProbe::kFooterMenu || probe == UiProbe::kFooterContexts) {
    struct MenuState {
      SidebarFooterAppliedSnapshot applied;
      views::ViewTracker menu;
      AppliedOwnerToken menu_owner;
      AppliedOwnerToken widget_owner;
    };
    auto state = std::make_shared<MenuState>();
    auto capture = base::BindRepeating(
        [](Impl* impl, Impl::Lifetime* life, std::shared_ptr<MenuState> state,
           const SidebarFooterAppliedSnapshot& applied) {
          state->applied = applied;
          if (!applied.menu_open) {
            state->menu.SetView(nullptr);
            state->menu_owner = {};
            state->widget_owner = {};
          } else if (auto* footer = life->view.view()) {
            // 제품의 open 적용값은 FooterBubble 생성이 끝난 뒤에만 발행된다.
            // 임의 View 알림에서는 생성 중 menu/Widget을 다시 찾지 않는다.
            auto* menu = GetSidebarFooterMenuForTesting(*footer);
            if (menu && menu != state->menu.view()) {
              state->menu.SetView(menu);
              state->menu_owner = impl->signals.ObserveView(*menu);
              if (auto* widget = menu->GetWidget())
                state->widget_owner = impl->signals.ObserveWidget(*widget);
            }
          }
          impl->signals.Notify();
        },
        base::Unretained(impl_.get()), base::Unretained(&life), state);
    capture.Run(GetSidebarFooterAppliedSnapshot(view));
    life.subscriptions.push_back(SubscribeSidebarFooterApplied(view, capture));
    impl_->signals.RegisterSignal(
        token, AppliedConditionKind::kViewStateMatches,
        base::BindRepeating(
            [](Impl* impl, Impl::Lifetime* life,
               std::shared_ptr<MenuState> state, AppliedOwnerToken token,
               UiProbe probe) -> std::optional<AppliedSnapshot> {
              auto* footer = life->view.view();
              if (!footer)
                return std::nullopt;
              ViewStateMatches value;
              const auto menu = impl->signals.Read(
                  {state->menu_owner, AppliedConditionKind::kViewStateMatches});
              const auto widget = impl->signals.Read(
                  {state->widget_owner,
                   AppliedConditionKind::kWidgetStateMatches});
              if (state->applied.menu_open && menu && widget &&
                  state->menu.view()) {
                value.drawn =
                    std::get<WidgetStateMatches>(widget->value).active;
                value.visible =
                    std::get<WidgetStateMatches>(widget->value).visible &&
                    (probe == UiProbe::kFooterMenu ||
                     (!state->menu.view()->GetViewByID(
                          kSidebarFooterRootContextViewId) &&
                      std::get<ViewStateMatches>(menu->value).opacity == 1));
              }
              return Snapshot(token, value, state->applied.apply_serial,
                              GetAppliedViewCoordinates(*footer));
            },
            base::Unretained(impl_.get()), base::Unretained(&life), state,
            token, probe));
    return token;
  }
  if (probe == UiProbe::kFooterInkHidden) {
    life.subscriptions.push_back(SubscribeSidebarFooterApplied(
        view, base::BindRepeating(
                  [](Impl* impl, const SidebarFooterAppliedSnapshot&) {
                    impl->signals.Notify();
                  },
                  base::Unretained(impl_.get()))));
  }
  const bool presentation = probe == UiProbe::kImageCenter ||
                            probe == UiProbe::kPaintedBackground ||
                            probe == UiProbe::kSidePanelEntry;
  impl_->signals.RegisterSignal(
      token,
      presentation ? AppliedConditionKind::kPresentationMatches
                   : AppliedConditionKind::kViewStateMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, AppliedOwnerToken token,
             UiProbe probe) -> std::optional<AppliedSnapshot> {
            auto* view = life->view.view();
            if (!view)
              return std::nullopt;
            if (probe == UiProbe::kToolbarMenu) {
              auto* button = views::AsViewClass<ToolbarButton>(view);
              if (!button)
                return std::nullopt;
              return Snapshot(
                  token, ViewStateMatches{.visible = button->IsMenuShowing()},
                  0, GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kSidebarCollapsed) {
              auto* browser_view = views::AsViewClass<BrowserView>(view);
              if (!browser_view)
                return std::nullopt;
              return Snapshot(
                  token,
                  ViewStateMatches{
                      .visible = tabs::VerticalTabStripStateController::From(
                                     browser_view->browser())
                                     ->IsCollapsed()},
                  0, GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kSidePanelEntry) {
              auto* browser_view = views::AsViewClass<BrowserView>(view);
              auto* panel = browser_view
                                ? SidePanelUI::From(browser_view->browser())
                                : nullptr;
              if (!panel)
                return std::nullopt;
              const auto entry = panel->GetCurrentEntryId();
              return Snapshot(
                  token,
                  PresentationMatches{
                      .presentation_id = panel->IsSidePanelShowing() && entry
                                             ? static_cast<uint64_t>(*entry) + 1
                                             : 0},
                  0, GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kSiteControlsPopup) {
              auto* button = views::AsViewClass<SiteControlsButton>(view);
              if (!button)
                return std::nullopt;
              const auto state = button->GetPopupAppliedSnapshot();
              return Snapshot(token, ViewStateMatches{.visible = state.showing},
                              state.apply_serial,
                              GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kScrollTargets) {
              auto* strip = views::AsViewClass<TabStripView>(view);
              if (!strip)
                return std::nullopt;
              return Snapshot(
                  token,
                  ViewStateMatches{
                      .scroll_animation_running =
                          strip->GetPrimaryScrollTargetForTesting() ||
                          strip->GetSecondaryScrollTargetForTesting()},
                  0, GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kFooterInkHidden) {
              const bool hidden =
                  views::InkDrop::Get(view)
                      ->GetInkDrop()
                      ->GetTargetInkDropState() == views::InkDropState::HIDDEN;
              return Snapshot(token, ViewStateMatches{.visible = hidden}, 0,
                              GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kSidebarHover) {
              auto* region =
                  views::AsViewClass<VerticalTabStripRegionView>(view);
              if (!region)
                return std::nullopt;
              return Snapshot(
                  token,
                  ViewStateMatches{.expanded_on_hover =
                                       region->is_expanded_on_hover()},
                  0, GetAppliedViewCoordinates(*view));
            }
            if (probe == UiProbe::kTabAndAdjacentHover) {
              const TabStyleViews* style = nullptr;
              if (auto* tab = views::AsViewClass<Tab>(view))
                style = tab->tab_style_views();
              if (auto* tab = views::AsViewClass<TabView>(view))
                style = tab->tab_styling();
              if (!style)
                return std::nullopt;
              auto* delegate = style->delegate();
              ViewStateMatches value{.opacity = 0};
              for (auto* item : {delegate, delegate->GetAdjacentTab(true),
                                 delegate->GetAdjacentTab(false)}) {
                if (!item)
                  continue;
                value.hovered |= item->IsHovering();
                value.hover_animation_running |= item->IsHoverAnimationActive();
                value.opacity = std::max(
                    value.opacity,
                    static_cast<double>(item->GetHoverAnimationValue()));
              }
              return Snapshot(token, value, 0,
                              GetAppliedViewCoordinates(*view));
            }
            PresentationMatches value;
            if (probe == UiProbe::kImageCenter) {
              auto* image = views::AsViewClass<views::ImageView>(view);
              if (!image)
                return std::nullopt;
              if (!image->GetImage().isNull()) {
                const auto bitmap =
                    image->GetImage().GetRepresentation(1.f).GetBitmap();
                if (bitmap.width() > 8 && bitmap.height() > 8)
                  value.icon_id = bitmap.getColor(8, 8);
              }
            } else {
              if (!view->background() || view->size().IsEmpty())
                return std::nullopt;
              gfx::Canvas canvas(view->size(), 1.f, false);
              view->background()->Paint(&canvas, view);
              value.background_color = canvas.GetBitmap().getColor(
                  view->width() / 2, view->height() / 2);
            }
            return Snapshot(token, value, 0, GetAppliedViewCoordinates(*view));
          },
          base::Unretained(&life), token, probe));
  return token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveRendererBackground(
    content::WebContents& contents) {
  auto watch =
      std::make_unique<Impl::ColorWatch>(*impl_, contents, nullptr, true);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(&Impl::ColorWatch::Read, base::Unretained(ptr)));
  impl_->colors.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::ObserveSurfaceColor(
    content::WebContents& contents,
    LocationBarView* location_bar) {
  auto watch =
      std::make_unique<Impl::ColorWatch>(*impl_, contents, location_bar);
  auto* ptr = watch.get();
  if (location_bar)
    impl_->Life(*location_bar).Own(ptr->token);
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(&Impl::ColorWatch::Read, base::Unretained(ptr)));
  impl_->colors.push_back(std::move(watch));
  return ptr->token;
}

AppliedOwnerToken AppliedBrowserObservers::ObservePresentation(
    views::View& consumer,
    BrowserSurfacePresentationBinding& binding,
    views::View* horizontal_icon) {
  auto& lifetime = impl_->Life(consumer);
  auto* icon_life = horizontal_icon ? &impl_->Life(*horizontal_icon) : nullptr;
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  if (icon_life)
    icon_life->Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, Impl::Lifetime* icon_life,
             BrowserSurfacePresentationBinding* binding,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            if (!life->view.view() || !binding->GetPresentation()) {
              return std::nullopt;
            }
            const auto& value = *binding->GetPresentation();
            auto* icon = icon_life ? icon_life->view.view() : nullptr;
            if (icon_life && !icon)
              return std::nullopt;
            const auto foreground =
                icon ? GetHorizontalTabIconForeground(*icon) : std::nullopt;
            const bool native =
                icon ? ShouldUseNativeHorizontalTabTheme(
                           *icon->GetNativeTheme(), icon->GetThemeProvider())
                     : value.palette_mode == BrowserSurfacePresentation::
                                                 PaletteMode::kNativeColors ||
                           ShouldUseNativeBrowserSurfaceColors(
                               *life->view.view()->GetNativeTheme());
            // icon_id 1은 Yee의 전경색 정책, 0은 native/default 정책이다.
            return Snapshot(token,
                            PresentationMatches{
                                value.source_id, value.revision, value.surface,
                                foreground.value_or(value.primary),
                                foreground && !native ? 1u : 0u, native},
                            value.revision,
                            GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(&lifetime), base::Unretained(icon_life),
          base::Unretained(&binding), token));
  lifetime.subscriptions.push_back(
      binding.AddPresentationChangedCallback(base::BindRepeating(
          &AppliedViewsObservers::Notify, base::Unretained(&impl_->signals),
          std::vector<AppliedSnapshot>{})));
  return token;
}

bool AppliedBrowserObservers::Supports(
    const AppliedCondition& condition) const {
  for (const auto& dom : impl_->doms) {
    if (condition.owner == dom->token && !dom->installed) {
      return false;
    }
  }
  return impl_->signals.Supports(condition);
}
bool AppliedBrowserObservers::IsOwnerAlive(AppliedOwnerToken owner) const {
  if (!impl_->signals.IsOwnerAlive(owner))
    return false;
  for (const auto& binding : impl_->panes) {
    if (binding->token == owner)
      return impl_->PaneIsCurrent(*binding);
  }
  return true;
}
std::optional<AppliedSnapshot> AppliedBrowserObservers::Read(
    const AppliedSignalKey& key) const {
  if (!IsOwnerAlive(key.owner))
    return std::nullopt;
  return impl_->signals.Read(key);
}
base::CallbackListSubscription AppliedBrowserObservers::Subscribe(
    const std::vector<AppliedSignalKey>& keys,
    Observer observer) {
  return impl_->signals.Subscribe(keys, std::move(observer));
}

}  // namespace yee::test
