// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {
constexpr int kMaxIcons = 3;
constexpr int kIconSize = 16;
constexpr int kIconBox = 20;
constexpr int kIconStep = 14;
constexpr int kIconInset = (kIconBox - kIconSize) / 2;
constexpr int kPreviewGap = 6;

class PreviewIcon final : public views::ImageView {
  METADATA_HEADER(PreviewIcon, views::ImageView)

 public:
  explicit PreviewIcon(bool feather_leading_edge)
      : feather_leading_edge_(feather_leading_edge) {}

  void OnPaint(gfx::Canvas* canvas) override {
    if (!feather_leading_edge_) {
      views::ImageView::OnPaint(canvas);
      return;
    }
    // 현재 이미지의 겹친 부분만 마스크한다. 투명하거나 사각형이 아닌 이미지도
    // chip 바탕과 앞쪽 파비콘을 덮지 않는다.
    canvas->SaveLayerAlpha(SK_AlphaOPAQUE, GetLocalBounds());
    views::ImageView::OnPaint(canvas);
    const float start = kIconInset;
    const float end = start + kIconBox - kIconStep;
    const bool rtl = base::i18n::IsRTL();
    const SkPoint points[] = {{rtl ? width() - start : start, 0},
                              {rtl ? width() - end : end, 0}};
    const SkColor4f colors[] = {SkColors::kTransparent, SkColors::kWhite};
    cc::PaintFlags mask;
    mask.setBlendMode(SkBlendMode::kDstIn);
    mask.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                       2, SkTileMode::kClamp));
    canvas->DrawRect(GetLocalBounds(), mask);
    canvas->Restore();
  }

 private:
  const bool feather_leading_edge_;
};

BEGIN_METADATA(PreviewIcon)
END_METADATA

class HorizontalGroupBackground : public views::Background {
 public:
  HorizontalGroupBackground(SkColor color, bool collapsed, bool hovered)
      : color_(color), collapsed_(collapsed), hovered_(hovered) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    if (UsesHorizontalTabGroupCapsule(*view) && !collapsed_ && !hovered_) {
      return;
    }
    gfx::Rect chip = view->GetLocalBounds();
    chip.ClampToCenteredSize(gfx::Size(
        chip.width(),
        std::min(chip.height(), kSidebarMetrics.horizontal_group_chip_height)));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(gfx::RectF(chip),
                          kSidebarMetrics.group_header_corner_radius, flags);
  }

 private:
  const SkColor color_;
  const bool collapsed_;
  const bool hovered_;
};

class HorizontalGroupCapsuleBackground : public views::Background {
 public:
  explicit HorizontalGroupCapsuleBackground(SkColor group_color)
      : group_color_(group_color) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    PaintHorizontalTabGroupCapsule(*canvas, *view, group_color_);
  }

 private:
  const SkColor group_color_;
};
}  // namespace

bool UsesHorizontalTabGroupCapsule(const views::View& view) {
  return IsShellEnabled() &&
         !ShouldUseNativeHorizontalTabTheme(*view.GetNativeTheme(),
                                            view.GetThemeProvider());
}

int ResolveHorizontalTabGroupEdgeInset(const views::View& decoration,
                                       int native_marker_inset,
                                       int native_tab_paint_inset) {
  return UsesHorizontalTabGroupCapsule(decoration)
             ? std::max(0, native_tab_paint_inset -
                               kSidebarMetrics.horizontal_group_capsule_padding)
             : native_marker_inset;
}

gfx::Insets ResolveHorizontalTabGroupHeaderInsets(const views::View& header,
                                                  int native_content_inset,
                                                  int native_tab_paint_inset,
                                                  bool collapsed) {
  const int leading_padding = collapsed
                                  ? 0
                                  : ResolveHorizontalTabGroupEdgeInset(
                                        header, 0, native_tab_paint_inset);
  return gfx::Insets::TLBR(0, native_content_inset + leading_padding, 0,
                           native_content_inset);
}

int ResolveHorizontalTabGroupSeparatorOffset(
    const views::View& tab,
    std::optional<tab_groups::TabGroupId> group,
    std::optional<tab_groups::TabGroupId> adjacent_group) {
  if (!UsesHorizontalTabGroupCapsule(tab) || group == adjacent_group) {
    return 0;
  }
  // 선택 멤버의 여백은 보존하고 그룹 바깥 구분선만 공통 배경에서 떨어뜨린다.
  const int gap = 2 * kSidebarMetrics.horizontal_group_capsule_padding;
  return group.has_value() ? -gap : gap;
}

gfx::Rect ResolveHorizontalTabGroupCapsuleBounds(gfx::Rect group_bounds) {
  group_bounds.Inset(gfx::Insets::TLBR(
      kSidebarMetrics.horizontal_group_capsule_top_inset, 0, 0, 0));
  group_bounds.set_height(std::min(
      group_bounds.height(), kSidebarMetrics.horizontal_group_capsule_height));
  return group_bounds;
}

void PaintHorizontalTabGroupCapsule(gfx::Canvas& canvas,
                                    const views::View& decoration,
                                    SkColor group_color) {
  const auto* widget = decoration.GetWidget();
  const SkColor shell = ResolveShellBackgroundColor(
      *decoration.GetColorProvider(), !widget || widget->ShouldPaintAsActive());
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color_utils::AlphaBlend(
      group_color, shell, kSidebarMetrics.horizontal_group_tint_opacity));
  canvas.DrawRoundRect(
      gfx::RectF(decoration.GetLocalBounds()),
      std::min(decoration.height() / 2.f,
               static_cast<float>(kSidebarMetrics.content_corner_radius)),
      flags);
}

std::unique_ptr<views::Background> CreateHorizontalTabGroupCapsuleBackground(
    SkColor group_color) {
  return std::make_unique<HorizontalGroupCapsuleBackground>(group_color);
}

// 파비콘 요약은 장식이며, native 그룹 헤더가 입력과 접근성을 소유한다.
class TabGroupPreview final : public views::View {
  METADATA_HEADER(TabGroupPreview, views::View)

 public:
  explicit TabGroupPreview(TabGroupPreviewStyle style) : style_(style) {
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
    SetProperty(views::kMarginsKey,
                gfx::Insets::TLBR(
                    0, kPreviewGap, 0,
                    style == TabGroupPreviewStyle::kSidebar ? kPreviewGap : 0));
    SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                 views::MaximumFlexSizeRule::kPreferred));
  }

  void Update(const TabGroup& group, bool collapsed) {
    color_ = group.visual_data()->color();
    if (!collapsed) {
      group_changed_subscription_ = {};
      group_.reset();
    }
    std::vector<tabs::TabHandle> tabs;
    int count = 0;
    if (collapsed && group.GetFirstTab()) {
      auto* browser = group.GetFirstTab()->GetBrowserWindowInterface();
      auto* model = browser ? browser->GetTabStripModel() : nullptr;
      if (model) {
        if (group_.get() != &group) {
          group_ = model->group_model()->GetTabGroup(group.id())->AsWeakPtr();
          // 제목·URL이 같은 탭의 재배치는 Header 비교만으로 감지되지 않는다.
          // 접힌 동안 native 그룹 신호를 구독하며 별도 모델 observer는 두지
          // 않는다.
          group_changed_subscription_ = group_->RegisterOnGroupChanged(
              base::BindRepeating(&TabGroupPreview::OnGroupChanged,
                                  weak_factory_.GetWeakPtr()));
        }
        // 컬렉션 이동 알림 중에는 탭별 그룹 ID 캐시가 늦을 수 있으므로,
        // TabGroupDataObserver와 같은 컬렉션 snapshot을 사용한다.
        for (auto* tab :
             model->GetTabsAtIndices(group.ListTabs().ToIntVector())) {
          ++count;
          if (tabs.size() < kMaxIcons) {
            tabs.push_back(tab->GetHandle());
          }
        }
      }
    }
    if (count == count_ && tabs == tabs_) {
      UpdateImages();
      return;
    }
    count_ = count;
    subscriptions_.clear();
    tabs_ = std::move(tabs);
    icons_.clear();
    overflow_ = nullptr;
    RemoveAllChildViews();
    for (const auto& handle : tabs_) {
      auto* icon = AddChildView(std::make_unique<PreviewIcon>(!icons_.empty()));
      icon->GetViewAccessibility().SetIsIgnored(true);
      icon->SetImageSize(gfx::Size(kIconSize, kIconSize));
      icon->SetBorder(views::CreateEmptyBorder(kIconInset));
      icons_.push_back(icon);
      if (auto* helper = TabUIHelper::From(handle.Get())) {
        subscriptions_.push_back(
            helper->AddTabUIChangeCallback(base::BindRepeating(
                &TabGroupPreview::UpdateImages, weak_factory_.GetWeakPtr())));
      }
    }
    int width = icons_.empty() ? 0 : kIconBox + (icons_.size() - 1) * kIconStep;
    if (count > kMaxIcons) {
      overflow_ = AddChildView(std::make_unique<views::Label>(
          u"+" + base::NumberToString16(count - kMaxIcons)));
      overflow_->GetViewAccessibility().SetIsIgnored(true);
      overflow_->SetAutoColorReadabilityEnabled(false);
      overflow_->SetSubpixelRenderingEnabled(false);
      overflow_->SetBounds(width + 4, 0, overflow_->GetPreferredSize().width(),
                           kIconBox);
      width = overflow_->bounds().right();
    }
    for (size_t index = 0; index < icons_.size(); ++index) {
      icons_[index]->SetBounds(index * kIconStep, 0, kIconBox, kIconBox);
    }
    // 모델의 첫 탭을 맨 앞에 놓는다. native View 미러링으로 RTL에서도
    // 앞쪽 탭이 겹친 부분을 차지하도록 유지한다.
    for (auto it = icons_.rbegin(); it != icons_.rend(); ++it) {
      ReorderChildView(*it, children().size());
    }
    SetPreferredSize(gfx::Size(width, kIconBox));
    SetVisible(width > 0);
    UpdateImages();
  }

  void SetHovered(bool hovered) {
    hovered_ = hovered;
    UpdateImages();
  }

 private:
  void OnGroupChanged() {
    if (group_) {
      Update(*group_, group_->visual_data()->is_collapsed());
    }
  }

  void AddedToWidget() override {
    views::View::AddedToWidget();
    paint_as_active_subscription_ =
        GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
            &TabGroupPreview::UpdateImages, weak_factory_.GetWeakPtr()));
    UpdateImages();
  }

  void RemovedFromWidget() override {
    paint_as_active_subscription_ = {};
    views::View::RemovedFromWidget();
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateImages();
  }

  void UpdateImages() {
    SkColor foreground = SK_ColorBLACK;
    if (GetColorProvider() && parent()) {
      const bool active = !GetWidget() || GetWidget()->ShouldPaintAsActive();
      if (style_ == TabGroupPreviewStyle::kSidebar) {
        foreground =
            ResolveSidebarItemColors(
                *GetColorProvider(),
                hovered_ ? SidebarItemVisualState::kHovered
                         : SidebarItemVisualState::kResting,
                hovered_ ? 1.0 : 0.0, active, /*persistent_surface=*/false)
                .foreground;
      } else {
        const SkColor group_color = GetColorProvider()->GetColor(
            GetTabGroupTabStripColorId(color_, active));
        const auto colors =
            ResolveHorizontalTabGroupColors(*parent(), group_color, hovered_);
        foreground = colors ? colors->foreground
                            : color_utils::GetColorWithMaxContrast(group_color);
      }
    }
    for (size_t index = 0; index < icons_.size(); ++index) {
      auto* tab = tabs_[index].Get();
      auto* helper = tab ? TabUIHelper::From(tab) : nullptr;
      auto image = helper ? helper->GetFavicon() : ui::ImageModel();
      if (image.IsEmpty()) {
        image = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                               foreground, kIconSize);
      }
      icons_[index]->SetImage(image);
    }
    if (overflow_) {
      overflow_->SetEnabledColor(foreground);
    }
  }

  const TabGroupPreviewStyle style_;
  bool hovered_ = false;
  int count_ = -1;
  tab_groups::TabGroupColorId color_ = tab_groups::TabGroupColorId::kGrey;
  raw_ptr<views::Label> overflow_ = nullptr;
  std::vector<raw_ptr<views::ImageView>> icons_;
  std::vector<tabs::TabHandle> tabs_;
  std::vector<base::CallbackListSubscription> subscriptions_;
  base::WeakPtr<TabGroup> group_;
  base::CallbackListSubscription group_changed_subscription_;
  base::CallbackListSubscription paint_as_active_subscription_;
  base::WeakPtrFactory<TabGroupPreview> weak_factory_{this};
};

BEGIN_METADATA(TabGroupPreview)
END_METADATA
}  // namespace yee

DEFINE_UI_CLASS_PROPERTY_TYPE(yee::TabGroupPreview*)

namespace yee {
namespace {
DEFINE_UI_CLASS_PROPERTY_KEY(int, kGroupChipWidthKey, 0)
DEFINE_UI_CLASS_PROPERTY_KEY(TabGroupPreview*, kGroupPreviewKey, nullptr)
}  // namespace

std::optional<HorizontalTabColors> ResolveHorizontalTabGroupColors(
    const views::View& header,
    SkColor group_color,
    bool hovered) {
  if (!IsShellEnabled() || !header.GetColorProvider() ||
      ShouldUseNativeHorizontalTabTheme(*header.GetNativeTheme(),
                                        header.GetThemeProvider())) {
    return std::nullopt;
  }
  const auto* widget = header.GetWidget();
  const SkColor shell = ResolveShellBackgroundColor(
      *header.GetColorProvider(), !widget || widget->ShouldPaintAsActive());
  const SkColor background = color_utils::AlphaBlend(
      group_color, shell,
      hovered ? 0.26f : kSidebarMetrics.horizontal_group_tint_opacity);
  return HorizontalTabColors{background,
                             color_utils::BlendForMinContrast(
                                 group_color, background, std::nullopt,
                                 color_utils::kMinimumReadableContrastRatio)
                                 .color};
}

std::unique_ptr<views::Background> CreateHorizontalTabGroupBackground(
    SkColor color,
    bool collapsed,
    bool hovered) {
  return std::make_unique<HorizontalGroupBackground>(color, collapsed, hovered);
}

void UpdateTabGroupPreview(views::View& header,
                           const TabGroup& group,
                           bool collapsed,
                           TabGroupPreviewStyle style,
                           views::View* before) {
  if (!IsShellEnabled()) {
    return;
  }
  auto* preview = header.GetProperty(kGroupPreviewKey);
  if (!preview) {
    preview = header.AddChildViewAt(
        std::make_unique<TabGroupPreview>(style),
        before ? header.GetIndexOf(before).value() : header.children().size());
    header.SetProperty(kGroupPreviewKey, preview);
  }
  preview->Update(group, collapsed);
}

void UpdateTabGroupPreviewColors(views::View& header, bool hovered) {
  if (auto* preview = header.GetProperty(kGroupPreviewKey)) {
    preview->SetHovered(hovered);
  }
}

SkColor ResolveHorizontalTabGroupForeground(const views::View& header,
                                            SkColor group_color) {
  const auto colors =
      ResolveHorizontalTabGroupColors(header, group_color, false);
  return colors ? colors->foreground
                : color_utils::GetColorWithMaxContrast(group_color);
}

void UpdateHorizontalTabGroupChip(views::View& header,
                                  views::View& chip,
                                  const TabGroup& group,
                                  SkColor group_color,
                                  bool collapsed) {
  if (!IsShellEnabled()) {
    return;
  }
  UpdateTabGroupPreview(header, group, collapsed,
                        TabGroupPreviewStyle::kHorizontal);
  auto* preview = header.GetProperty(kGroupPreviewKey);
  if (preview->GetVisible()) {
    gfx::Rect bounds = chip.bounds();
    const bool has_native_indicator =
        std::ranges::any_of(chip.children(), [](const views::View* child) {
          return child->GetVisible() && !child->bounds().IsEmpty();
        });
    // 이름이 없어도 공유·알림 아이콘이 차지한 chip 영역은 보존한다.
    const int start =
        group.visual_data()->title().empty() && !has_native_indicator
            ? bounds.x() + kPreviewGap
            : bounds.right();
    preview->SetBounds(start, bounds.CenterPoint().y() - kIconBox / 2,
                       preview->GetPreferredSize().width(), kIconBox);
    bounds.set_width(preview->bounds().right() + kPreviewGap - bounds.x());
    chip.SetBoundsRect(bounds);
  }
  const auto colors =
      ResolveHorizontalTabGroupColors(header, group_color, false);
  if (colors) {
    chip.SetBackground(
        CreateHorizontalTabGroupBackground(colors->background, collapsed));
  }
  if (auto* ring = views::FocusRing::Get(&header)) {
    ring->SetColorId(colors
                         ? std::make_optional(ResolveBrowserSurfaceFocusColorId(
                               *header.GetColorProvider(), colors->background,
                               ui::kColorSysStateFocusRing))
                         : std::nullopt);
  }
  if (header.GetProperty(kGroupChipWidthKey) != chip.width()) {
    header.SetProperty(kGroupChipWidthKey, chip.width());
    header.InvalidateLayout();
  }
}

int GetTabGroupPreviewWidth(const views::View& header) {
  const auto* preview = header.GetProperty(kGroupPreviewKey);
  return preview && preview->GetVisible()
             ? preview->GetPreferredSize().width() + kPreviewGap
             : 0;
}
}  // namespace yee
