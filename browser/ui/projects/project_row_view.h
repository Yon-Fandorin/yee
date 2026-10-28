// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_ROW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_ROW_VIEW_H_

#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/views/yee/disclosure_arrow.h"
#include "ui/base/models/image_model.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class MdTextButton;
class LabelButton;
}  // namespace views

namespace yee::projects {

// 행이 배경을 소유하므로 macOS의 MdTextButton pressed 덧칠도 생략한다.
class ProjectRowButton : public views::MdTextButton {
  METADATA_HEADER(ProjectRowButton, views::MdTextButton)

 public:
  ProjectRowButton(PressedCallback callback, const std::u16string& title);
  ~ProjectRowButton() override;

 private:
  void UpdateBackgroundColor() override;
};

// 행 전체가 상태 면을 소유한다. 자식 조작 사이를 이동해도 선택·hover가
// 끊기지 않으며 보조 조작을 숨길 때에도 제목의 가용 폭은 유지한다.
class ProjectRowView : public views::View,
                       public views::FocusChangeListener,
                       public views::WidgetObserver {
  METADATA_HEADER(ProjectRowView, views::View)

 public:
  ProjectRowView(bool sidebar,
                 bool selected,
                 base::RepeatingClosure open,
                 int spacing = 0);
  ~ProjectRowView() override;
  void SetActivationAction(base::RepeatingClosure action);
  void SetMiddleClickAction(base::RepeatingClosure action);
  void SetPrimary(views::MdTextButton* button);
  void AddTrailing(std::unique_ptr<views::LabelButton> button,
                   bool reveal_on_hover);
  void SetLeadingDisclosure(views::MdTextButton* button,
                            ui::ImageModel resting_icon,
                            bool expanded);
  void SetDisclosureExpanded(bool expanded);
  // 재생성된 행의 숨은 보조 조작도 먼저 드러낸 뒤 논리 초점을 복원한다.
  static bool RestoreFocusTo(views::View* target);

 private:
  void UpdateRowState();
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;
  void VisibilityChanged(views::View* starting_from, bool visible) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnWillChangeFocus(views::View* before, views::View* now) override;
  void OnDidChangeFocus(views::View* before, views::View* now) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  const bool sidebar_;
  const bool selected_;
  base::RepeatingClosure open_;
  base::RepeatingClosure middle_click_;
  raw_ptr<views::MdTextButton> primary_ = nullptr;
  struct TrailingAction {
    raw_ptr<views::LabelButton> button;
    bool reveal_on_hover;
    base::CallbackListSubscription state;
  };
  std::vector<TrailingAction> trailing_actions_;
  raw_ptr<views::MdTextButton> leading_ = nullptr;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  bool hovered_ = false;
  bool pressed_ = false;
  bool middle_pressed_ = false;
  DisclosureArrow disclosure_arrow_;
  ui::ImageModel resting_icon_;
  base::CallbackListSubscription primary_state_;
  base::CallbackListSubscription leading_state_;
  base::CallbackListSubscription paint_active_subscription_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_ROW_VIEW_H_
