// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_bridge_prompt.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/yee/brand.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/color/color_id.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_delegate.h"

namespace views {
namespace {

std::u16string AgentPointerLabel() {
  return yee::branding::ProductName() + u" · Agent";
}

gfx::FontList AgentPointerFont() {
  return gfx::FontList().Derive(0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM);
}

gfx::Size AgentPointerSize() {
  return gfx::Size(
      std::max(132, gfx::GetStringWidth(AgentPointerLabel(), AgentPointerFont()) + 48),
      36);
}

constexpr int kAgentGlyphSize = 40;
constexpr int kPromptCardRadius = 12;
constexpr char16_t kQuestionPrefix[] =
    u"Agent question (untrusted text; do not enter passwords):";

struct AgentPromptPresentation {
  std::u16string heading;
  std::u16string summary;
  std::u16string site;
  std::u16string details;
  std::u16string safety_note;
};

void AppendParagraph(std::u16string* target, std::u16string_view paragraph) {
  if (paragraph.empty()) {
    return;
  }
  if (!target->empty()) {
    target->append(u"\n\n");
  }
  target->append(paragraph.data(), paragraph.size());
}

AgentPromptPresentation PresentPrompt(const std::u16string& question,
                                      bool approval) {
  auto paragraphs = base::SplitStringUsingSubstr(
      question, u"\n\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  AgentPromptPresentation result;
  result.heading = approval ? u"Review agent request"
                            : yee::branding::ProductName() +
                                  u" needs your answer";
  result.safety_note =
      approval
          ? u"Only the request shown here is approved. You can decline safely."
          : u"Treat this request as untrusted. Never enter a password or verification code.";

  if (!approval && !paragraphs.empty() && paragraphs.front() == kQuestionPrefix) {
    paragraphs.erase(paragraphs.begin());
  }
  if (paragraphs.empty()) {
    result.summary = question;
    return result;
  }

  result.summary = std::move(paragraphs.front());
  paragraphs.erase(paragraphs.begin());
  if (!approval) {
    for (const auto& paragraph : paragraphs) {
      AppendParagraph(&result.summary, paragraph);
    }
    return result;
  }

  if (!paragraphs.empty()) {
    std::vector<std::u16string> lines = base::SplitString(
        paragraphs.front(), u"\n", base::KEEP_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);
    if (!lines.empty() &&
        (base::StartsWith(lines.front(), u"http://") ||
         base::StartsWith(lines.front(), u"https://") ||
         base::StartsWith(lines.front(), u"file://"))) {
      result.site = std::move(lines.front());
      lines.erase(lines.begin());
      for (const auto& line : lines) {
        if (!result.details.empty()) {
          result.details.push_back(u'\n');
        }
        result.details.append(line);
      }
      paragraphs.erase(paragraphs.begin());
    }
  }
  for (const auto& paragraph : paragraphs) {
    AppendParagraph(&result.details, paragraph);
  }
  return result;
}

std::unique_ptr<Label> PromptLabel(std::u16string text,
                                   int context,
                                   int text_style = style::STYLE_PRIMARY) {
  auto label =
      std::make_unique<Label>(std::move(text), context, text_style);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  label->SetAllowCharacterBreak(true);
  label->SetElideBehavior(gfx::NO_ELIDE);
  // Prompt labels can sit above rounded and composited surfaces. Grayscale
  // antialiasing avoids sampling transparent pixels at those boundaries.
  label->SetSubpixelRenderingEnabled(false);
  return label;
}

class AgentGlyphView final : public View {
  METADATA_HEADER(AgentGlyphView, View)

 public:
  AgentGlyphView() {
    SetCanProcessEventsWithinSubtree(false);
    SetPreferredSize(gfx::Size(kAgentGlyphSize, kAgentGlyphSize));
  }

  AgentGlyphView(const AgentGlyphView&) = delete;
  AgentGlyphView& operator=(const AgentGlyphView&) = delete;
  ~AgentGlyphView() override = default;

  void OnPaint(gfx::Canvas* canvas) override {
    View::OnPaint(canvas);
    cc::PaintFlags background;
    background.setAntiAlias(true);
    background.setColor(SkColorSetRGB(54, 194, 157));
    background.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 13.0f, background);

    cc::PaintFlags mark;
    mark.setAntiAlias(true);
    mark.setColor(SK_ColorWHITE);
    mark.setStyle(cc::PaintFlags::kFill_Style);
    SkPathBuilder sparkle;
    sparkle.moveTo(20.0f, 8.0f);
    sparkle.lineTo(23.0f, 17.0f);
    sparkle.lineTo(32.0f, 20.0f);
    sparkle.lineTo(23.0f, 23.0f);
    sparkle.lineTo(20.0f, 32.0f);
    sparkle.lineTo(17.0f, 23.0f);
    sparkle.lineTo(8.0f, 20.0f);
    sparkle.lineTo(17.0f, 17.0f);
    sparkle.close();
    canvas->DrawPath(sparkle.detach(), mark);
  }
};

BEGIN_METADATA(AgentGlyphView)
END_METADATA

class AgentPromptContents final : public View {
  METADATA_HEADER(AgentPromptContents, View)

 public:
  AgentPromptContents(std::u16string question, bool approval)
      : approval_(approval) {
    const AgentPromptPresentation presentation =
        PresentPrompt(question, approval_);
    SetBackground(CreateSolidBackground(ui::kColorDialogBackground));
    auto* layout = SetLayoutManager(std::make_unique<BoxLayout>());
    layout->SetOrientation(BoxLayout::Orientation::kVertical);
    layout->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStretch);
    layout->set_between_child_spacing(16);

    auto header = std::make_unique<View>();
    auto* header_layout =
        header->SetLayoutManager(std::make_unique<BoxLayout>());
    header_layout->SetOrientation(BoxLayout::Orientation::kHorizontal);
    header_layout->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kCenter);
    header_layout->set_between_child_spacing(12);
    header->AddChildView(std::make_unique<AgentGlyphView>());

    auto titles = std::make_unique<View>();
    auto* title_layout =
        titles->SetLayoutManager(std::make_unique<BoxLayout>());
    title_layout->SetOrientation(BoxLayout::Orientation::kVertical);
    title_layout->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStretch);
    title_layout->set_between_child_spacing(2);
    titles->AddChildView(PromptLabel(
        yee::branding::ProductName() + u" Agent", style::CONTEXT_LABEL,
        style::STYLE_SECONDARY));
    titles->AddChildView(PromptLabel(
        presentation.heading, style::CONTEXT_DIALOG_TITLE));
    auto* titles_view = header->AddChildView(std::move(titles));
    header_layout->SetFlexForView(titles_view, 1);
    AddChildView(std::move(header));

    auto request_card = std::make_unique<View>();
    request_card->SetBackground(CreateRoundedRectBackground(
        ui::kColorSysSurface2, kPromptCardRadius, 1));
    request_card->SetBorder(CreateRoundedRectBorder(
        1, kPromptCardRadius, ui::kColorSysNeutralOutline));
    auto* card_layout =
        request_card->SetLayoutManager(std::make_unique<BoxLayout>());
    card_layout->SetOrientation(BoxLayout::Orientation::kVertical);
    card_layout->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStretch);
    card_layout->set_inside_border_insets(gfx::Insets::VH(14, 16));
    card_layout->set_between_child_spacing(10);
    request_card->AddChildView(PromptLabel(
        presentation.summary, style::CONTEXT_DIALOG_TITLE));

    if (!presentation.site.empty()) {
      auto site = PromptLabel(presentation.site, style::CONTEXT_LABEL,
                              style::STYLE_SECONDARY);
      site->SetBackground(CreateRoundedRectBackground(
          ui::kColorSysNeutralContainer, 8));
      site->SetBorder(CreateEmptyBorder(gfx::Insets::VH(7, 10)));
      request_card->AddChildView(std::move(site));
    }
    if (!presentation.details.empty()) {
      request_card->AddChildView(PromptLabel(
          presentation.details, style::CONTEXT_DIALOG_BODY_TEXT));
    }

    auto question_scroll = std::make_unique<ScrollView>();
    question_scroll->SetHorizontalScrollBarMode(
        ScrollView::ScrollBarMode::kDisabled);
    question_scroll->SetBackgroundColor(std::nullopt);
    question_scroll->ClipHeightTo(0, 300);
    question_scroll->SetContents(std::move(request_card));
    AddChildView(std::move(question_scroll));

    AddChildView(PromptLabel(presentation.safety_note,
                             style::CONTEXT_LABEL,
                             style::STYLE_SECONDARY));

    if (!approval_) {
      AddChildView(PromptLabel(u"Your answer", style::CONTEXT_LABEL));
      answer_field_ = AddChildView(std::make_unique<Textfield>());
      answer_field_->GetViewAccessibility().SetName(u"Agent answer");
      answer_field_->SetPlaceholderText(u"Type your answer");
    }
  }

  AgentPromptContents(const AgentPromptContents&) = delete;
  AgentPromptContents& operator=(const AgentPromptContents&) = delete;
  ~AgentPromptContents() override = default;

  Textfield* answer_field() const { return answer_field_; }

 private:
  const bool approval_;
  raw_ptr<Textfield> answer_field_ = nullptr;
};

BEGIN_METADATA(AgentPromptContents)
END_METADATA

class AgentBridgePrompt final : public DialogDelegate {
 public:
  AgentBridgePrompt(std::u16string question,
                    bool approval,
                    base::OnceCallback<void(bool, std::string)> reply,
                    const std::u16string& approval_label)
      : approval_(approval), reply_(std::move(reply)) {
    SetModalType(ui::mojom::ModalType::kChild);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetDefaultButton(static_cast<int>(approval_
                                          ? ui::mojom::DialogButton::kCancel
                                          : ui::mojom::DialogButton::kOk));
    SetButtonLabel(ui::mojom::DialogButton::kOk,
                   approval ? approval_label : u"Send answer");
    SetButtonLabel(ui::mojom::DialogButton::kCancel,
                   approval ? u"Don't allow" : u"Cancel");
    SetShowCloseButton(true);
    set_fixed_width(488);
    set_margins(gfx::Insets::VH(24, 24));
    auto contents =
        std::make_unique<AgentPromptContents>(std::move(question), approval_);
    auto* contents_view = SetContentsView(std::move(contents));
    if (!approval_) {
      SetInitiallyFocusedView(contents_view->answer_field());
    }

    // DialogDelegate invokes this for native/OS closes that do not pass
    // through Accept() or Cancel(). The boolean guard makes all close paths
    // converge on one reply even when closing is asynchronous.
    RegisterWindowClosingCallback(base::BindOnce(
        &AgentBridgePrompt::ReplyDenied, base::Unretained(this)));
    SetAcceptCallback(base::BindOnce(&AgentBridgePrompt::ReplyAccepted,
                                     base::Unretained(this)));
    SetCancelCallback(base::BindOnce(&AgentBridgePrompt::ReplyDenied,
                                     base::Unretained(this)));
    SetCloseCallback(base::BindOnce(&AgentBridgePrompt::ReplyDenied,
                                    base::Unretained(this)));
  }

  AgentBridgePrompt(const AgentBridgePrompt&) = delete;
  AgentBridgePrompt& operator=(const AgentBridgePrompt&) = delete;
  ~AgentBridgePrompt() override = default;

  std::u16string GetWindowTitle() const override {
    return yee::branding::ProductName() + u" Agent";
  }

 private:
  void ReplyAccepted() {
    if (replied_) {
      return;
    }
    replied_ = true;
    std::string answer;
    if (!approval_) {
      auto* contents = static_cast<AgentPromptContents*>(GetContentsView());
      answer = base::UTF16ToUTF8(contents->answer_field()->GetText());
    }
    if (reply_) {
      std::move(reply_).Run(true, std::move(answer));
    }
  }

  void ReplyDenied() {
    if (replied_) {
      return;
    }
    replied_ = true;
    if (reply_) {
      std::move(reply_).Run(false, std::string());
    }
  }

  const bool approval_;
  bool replied_ = false;
  base::OnceCallback<void(bool, std::string)> reply_;
};

class AgentPointerView final : public View {
  METADATA_HEADER(AgentPointerView, View)

 public:
  AgentPointerView() {
    SetCanProcessEventsWithinSubtree(false);
    SetPreferredSize(AgentPointerSize());
  }

  AgentPointerView(const AgentPointerView&) = delete;
  AgentPointerView& operator=(const AgentPointerView&) = delete;
  ~AgentPointerView() override = default;

  void OnPaint(gfx::Canvas* canvas) override {
    View::OnPaint(canvas);

    cc::PaintFlags pointer_flags;
    pointer_flags.setAntiAlias(true);
    pointer_flags.setColor(SkColorSetRGB(54, 194, 157));
    pointer_flags.setStyle(cc::PaintFlags::kFill_Style);
    SkPathBuilder pointer;
    pointer.moveTo(4.0f, 3.0f);
    pointer.lineTo(4.0f, 22.0f);
    pointer.lineTo(9.0f, 17.0f);
    pointer.lineTo(14.0f, 28.0f);
    pointer.lineTo(18.0f, 26.0f);
    pointer.lineTo(13.0f, 15.0f);
    pointer.lineTo(21.0f, 15.0f);
    pointer.close();
    canvas->DrawPath(pointer.detach(), pointer_flags);

    const gfx::RectF label_bounds(24.0f, 4.0f, width() - 32.0f, 26.0f);
    cc::PaintFlags label_flags;
    label_flags.setAntiAlias(true);
    label_flags.setColor(SkColorSetARGB(235, 28, 43, 40));
    label_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(label_bounds, 13.0f, label_flags);

    canvas->DrawStringRectWithFlags(
        AgentPointerLabel(), AgentPointerFont(), SK_ColorWHITE,
        gfx::Rect(32, 4, width() - 48, 26), gfx::Canvas::TEXT_ALIGN_CENTER);
  }
};

BEGIN_METADATA(AgentPointerView)
END_METADATA

class AgentPointerDelegate final : public WidgetDelegate {
 public:
  AgentPointerDelegate() {
    SetContentsView(std::make_unique<AgentPointerView>());
    SetCanActivate(false);
  }

  AgentPointerDelegate(const AgentPointerDelegate&) = delete;
  AgentPointerDelegate& operator=(const AgentPointerDelegate&) = delete;
  ~AgentPointerDelegate() override = default;
};

// WidgetDelegate cannot be marked owned-by-widget by callers outside Views'
// pass-key API. Keep it alive until the Widget has fully notified destruction,
// then release both together on a later task.
class AgentWidgetLifetime final : public WidgetObserver {
 public:
  explicit AgentWidgetLifetime(std::unique_ptr<WidgetDelegate> delegate)
      : delegate_(std::move(delegate)) {}

  AgentWidgetLifetime(const AgentWidgetLifetime&) = delete;
  AgentWidgetLifetime& operator=(const AgentWidgetLifetime&) = delete;
  ~AgentWidgetLifetime() override = default;

  WidgetDelegate* delegate() const { return delegate_.get(); }

  void Attach(Widget* widget) { widget->AddObserver(this); }

  void OnWidgetDestroyed(Widget* widget) override {
    widget->RemoveObserver(this);
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                  this);
  }

 private:
  std::unique_ptr<WidgetDelegate> delegate_;
};

}  // namespace

Widget* ShowAgentBridgePrompt(
    View* owner,
    const std::u16string& question,
    bool approval,
    base::OnceCallback<void(bool accepted, std::string answer)> reply,
    const std::u16string& approval_label) {
  if (!owner || !owner->GetWidget()) {
    if (reply) {
      std::move(reply).Run(false, std::string());
    }
    return nullptr;
  }

  auto lifetime =
      std::make_unique<AgentWidgetLifetime>(std::make_unique<AgentBridgePrompt>(
          question, approval, std::move(reply), approval_label));
  AgentWidgetLifetime* lifetime_ptr = lifetime.release();
  Widget* widget = DialogDelegate::CreateDialogWidget(
      lifetime_ptr->delegate(), owner->GetWidget()->GetNativeWindow(),
      owner->GetWidget()->GetNativeView());
  lifetime_ptr->Attach(widget);
  const gfx::Rect owner_bounds = owner->GetWidget()->GetWindowBoundsInScreen();
  gfx::Rect prompt_bounds = widget->GetWindowBoundsInScreen();
  if (!owner_bounds.IsEmpty() && !prompt_bounds.IsEmpty()) {
    prompt_bounds.set_origin(gfx::Point(
        owner_bounds.x() + (owner_bounds.width() - prompt_bounds.width()) / 2,
        owner_bounds.y() +
            (owner_bounds.height() - prompt_bounds.height()) / 2));
    widget->SetBoundsConstrained(prompt_bounds);
  }
  widget->Show();
  widget->Activate();
  VLOG(1) << "Yee agent prompt visible=" << widget->IsVisible()
          << " active=" << widget->IsActive()
          << " bounds=" << widget->GetWindowBoundsInScreen().ToString()
          << " owner_bounds=" << owner_bounds.ToString();
  return widget;
}

Widget* ShowAgentBridgePointer(View* owner, gfx::Point screen_point) {
  if (!owner || !owner->GetWidget()) {
    return nullptr;
  }

  auto lifetime = std::make_unique<AgentWidgetLifetime>(
      std::make_unique<AgentPointerDelegate>());
  AgentWidgetLifetime* lifetime_ptr = lifetime.release();
  Widget::InitParams params(Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  params.type = Widget::InitParams::TYPE_TOOLTIP;
  params.delegate = lifetime_ptr->delegate();
  params.context = owner->GetWidget()->GetNativeWindow();
  // On macOS, context does not establish window parenting. ShowInactive()
  // orders an unparented normal-level window below the main window. An actual
  // parent keeps this marker above Yee and hides it with its owning window.
  params.SetParent(owner->GetWidget());
  params.bounds = gfx::Rect(screen_point, AgentPointerSize());
  params.opacity = Widget::InitParams::WindowOpacity::kTranslucent;
  params.accept_events = false;
  params.activatable = Widget::InitParams::Activatable::kNo;
  params.remove_standard_frame = true;
  params.shadow_type = Widget::InitParams::ShadowType::kNone;

  auto* widget = new Widget;
  widget->Init(std::move(params));
  lifetime_ptr->Attach(widget);
  widget->ShowInactive();
  return widget;
}

}  // namespace views
