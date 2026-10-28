// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_navigation_controller.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/browser_surface_layout.h"
#include "chrome/browser/ui/views/yee/projects/project_hub_view.h"
#include "chrome/browser/ui/views/yee/projects/project_navigation_presentation.h"
#include "chrome/browser/ui/views/yee/projects/project_navigation_view.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/browser/yee_projects/project_service.h"
#include "chrome/browser/yee_projects/project_service_factory.h"
#include "chrome/browser/yee_projects/project_tab_coordinator.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace yee::projects {
namespace {

// 포인터로 허브를 열 때 강제 초점 ring을 남기지 않는다. 키보드 진입은
// 기존 초기 초점을 유지하며 Mac의 별도 Toolbar FocusManager도 처리한다.
void ClearHubPointerFocus(views::View* header, views::View* body) {
  views::ViewTracker header_tracker(header);
  views::ViewTracker body_tracker(body);
  for (auto* tracker : {&header_tracker, &body_tracker}) {
    if (auto* target = tracker->view(); target && target->GetFocusManager()) {
      auto* manager = target->GetFocusManager();
      const bool clear_current = target->Contains(manager->GetFocusedView());
      if (clear_current || target->Contains(manager->GetStoredFocusView()))
        manager->SetStoredFocusView(nullptr);
      if (clear_current)
        manager->ClearFocus();
    }
  }
}

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProjectNameField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProjectNameAccept);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProjectDescriptionField);

class NameDialogDelegate : public ui::DialogModelDelegate {
 public:
  explicit NameDialogDelegate(
      base::OnceCallback<void(std::u16string, std::u16string)> accept)
      : accept_(std::move(accept)) {}
  void ObserveName() {
    subscription_ =
        dialog_model()
            ->GetTextfieldByUniqueId(kProjectNameField)
            ->AddOnFieldChangedCallback(base::BindRepeating(
                &NameDialogDelegate::Validate, base::Unretained(this)));
    description_subscription_ =
        dialog_model()
            ->GetTextfieldByUniqueId(kProjectDescriptionField)
            ->AddOnFieldChangedCallback(base::BindRepeating(
                &NameDialogDelegate::Validate, base::Unretained(this)));
    Validate();
  }
  bool Accept() {
    auto name =
        dialog_model()->GetTextfieldByUniqueId(kProjectNameField)->text();
    std::u16string trimmed;
    base::TrimWhitespace(name, base::TRIM_ALL, &trimmed);
    auto description = dialog_model()
                           ->GetTextfieldByUniqueId(kProjectDescriptionField)
                           ->text();
    if (trimmed.empty() || trimmed.size() > 128 || description.size() > 2048) {
      return false;
    }
    std::move(accept_).Run(std::move(trimmed), std::move(description));
    return true;
  }

 private:
  void Validate() {
    const auto& name =
        dialog_model()->GetTextfieldByUniqueId(kProjectNameField)->text();
    std::u16string trimmed;
    base::TrimWhitespace(name, base::TRIM_ALL, &trimmed);
    dialog_model()->SetButtonEnabled(
        dialog_model()->GetButtonByUniqueId(kProjectNameAccept),
        !trimmed.empty() && trimmed.size() <= 128 &&
            dialog_model()
                    ->GetTextfieldByUniqueId(kProjectDescriptionField)
                    ->text()
                    .size() <= 2048);
  }
  base::OnceCallback<void(std::u16string, std::u16string)> accept_;
  base::CallbackListSubscription subscription_;
  base::CallbackListSubscription description_subscription_;
};

std::unique_ptr<views::BubbleDialogModelHost> MakeNameDialog(
    int title,
    const std::u16string& initial,
    const std::u16string& description,
    base::OnceCallback<void(std::u16string, std::u16string)> accept) {
  auto delegate = std::make_unique<NameDialogDelegate>(std::move(accept));
  auto* delegate_ptr = delegate.get();
  auto model =
      ui::DialogModel::Builder(std::move(delegate))
          .SetTitle(l10n_util::GetStringUTF16(title))
          .AddTextfield(kProjectNameField,
                        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_NAME),
                        initial)
          .AddTextfield(kProjectDescriptionField,
                        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_DESCRIPTION),
                        description)
          .SetInitiallyFocusedField(kProjectNameField)
          .AddOkButton(
              base::BindRepeating(&NameDialogDelegate::Accept,
                                  base::Unretained(delegate_ptr)),
              ui::DialogModel::Button::Params()
                  .SetId(kProjectNameAccept)
                  .SetLabel(l10n_util::GetStringUTF16(
                      title == IDS_YEE_PROJECT_NEW ? IDS_YEE_PROJECT_CREATE
                                                   : IDS_YEE_PROJECT_SAVE)))
          .AddCancelButton(base::DoNothing())
          .Build();
  delegate_ptr->ObserveName();
  return views::BubbleDialogModelHost::CreateModal(
      std::move(model), ui::mojom::ModalType::kWindow);
}

std::unique_ptr<views::BubbleDialogModelHost> MakeConfirmDialog(
    int title,
    int message,
    int action,
    base::OnceClosure accept,
    const std::u16string& detail = {}) {
  auto model = ui::DialogModel::Builder()
                   .SetTitle(l10n_util::GetStringUTF16(title))
                   .AddParagraph(ui::DialogModelLabel(
                       l10n_util::GetStringUTF16(message) +
                       (detail.empty() ? u"" : u"\n\n" + detail)))
                   .AddOkButton(std::move(accept),
                                ui::DialogModel::Button::Params().SetLabel(
                                    l10n_util::GetStringUTF16(action)))
                   .AddCancelButton(base::DoNothing())
                   .Build();
  return views::BubbleDialogModelHost::CreateModal(
      std::move(model), ui::mojom::ModalType::kWindow);
}

ProjectPageItem MakePageItem(const ProjectPage& page,
                             const ProjectTabCoordinator& tabs,
                             BrowserWindowInterface& browser,
                             bool showing_hub) {
  auto* tab = tabs.FindLiveTab(page.id, &browser);
  auto* contents = tab ? tab->GetContents() : nullptr;
  const GURL url = contents && !contents->GetLastCommittedURL().is_empty()
                       ? contents->GetLastCommittedURL()
                       : page.url;
  const std::u16string title =
      contents ? (contents->GetTitle().empty() ? base::UTF8ToUTF16(url.host())
                                               : contents->GetTitle())
               : page.title;
  return ProjectPageItem{
      page.id,
      title,
      base::UTF8ToUTF16(url.host()),
      tab != nullptr,
      url,
      tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>(),
      !showing_hub && tab && tab == browser.GetTabStripModel()->GetActiveTab()};
}

}  // namespace

namespace {
void CollectMenuCommands(const ui::MenuModel& model, std::set<int>& commands) {
  for (size_t index = 0; index < model.GetItemCount(); ++index) {
    if (model.GetTypeAt(index) != ui::MenuModel::TYPE_SEPARATOR) {
      commands.insert(model.GetCommandIdAt(index));
    }
    if (auto* submenu = model.GetSubmenuModelAt(index)) {
      CollectMenuCommands(*submenu, commands);
    }
  }
}

int AllocateMenuCommand(std::set<int>& commands, int& next) {
  while (commands.contains(next)) {
    CHECK_LT(next, std::numeric_limits<int>::max());
    ++next;
  }
  CHECK_LT(next, std::numeric_limits<int>::max());
  commands.insert(next);
  return next++;
}
}  // namespace

class ProjectPicker : public ui::SimpleMenuModel,
                      public ui::SimpleMenuModel::Delegate {
 public:
  ProjectPicker(const ProjectService& service,
                const std::vector<ProjectPageItem>& pages,
                const std::string& selected,
                bool writable,
                base::RepeatingCallback<void(std::string)> select,
                base::RepeatingClosure create,
                base::RepeatingCallback<void(std::string)> open_page,
                const ui::MenuModel* parent,
                int container_command)
      : SimpleMenuModel(this), writable_(writable) {
    if (parent) {
      CollectMenuCommands(*parent, reserved_commands_);
    }
    reserved_commands_.insert(container_command);
    // 사용자 제목의 &는 메뉴 접근키로 해석하지 않는다.
    for (const auto& page : pages) {
      AddItem(AddCommand(base::BindRepeating(open_page, page.id)), page.title);
      SetMayHaveMnemonicsAt(GetItemCount() - 1, false);
    }
    if (!pages.empty()) {
      AddSeparator(ui::NORMAL_SEPARATOR);
    }
    for (const auto& project : service.projects()) {
      const int command = AddCommand(base::BindRepeating(select, project.id));
      AddRadioItem(command, project.name, 1);
      SetMayHaveMnemonicsAt(GetItemCount() - 1, false);
      if (project.id == selected) {
        selected_ = command;
      }
    }
    if (!service.projects().empty()) {
      AddSeparator(ui::NORMAL_SEPARATOR);
    }
    create_command_ = AddCommand(std::move(create));
    AddItem(create_command_, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_NEW));
    if (!writable) {
      AddItem(AllocateMenuCommand(reserved_commands_, next_command_),
              l10n_util::GetStringUTF16(IDS_YEE_PROJECT_READ_ONLY));
    }
  }
  bool IsCommandIdChecked(int id) const override { return id == selected_; }
  bool IsCommandIdEnabled(int id) const override {
    return commands_.contains(id) && (id != create_command_ || writable_);
  }
  void ExecuteCommand(int id, int event_flags) override {
    if (!IsCommandIdEnabled(id)) {
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, commands_.at(id));
  }

 private:
  int AddCommand(base::RepeatingClosure action) {
    const int command = AllocateMenuCommand(reserved_commands_, next_command_);
    commands_.emplace(command, std::move(action));
    return command;
  }

  const bool writable_;
  int selected_ = -1;
  int create_command_ = -1;
  int next_command_ = 1;
  std::set<int> reserved_commands_;
  std::map<int, base::RepeatingClosure> commands_;
};

DEFINE_USER_DATA(ProjectNavigationController);

ProjectNavigationController::ProjectNavigationController(
    BrowserWindowInterface* browser)
    : browser_(browser),
      service_(ProjectServiceFactory::GetForProfile(browser->GetProfile())),
      tabs_(ProjectTabCoordinator::GetForProfile(browser->GetProfile())),
      scoped_data_(browser->GetUnownedUserDataHost(), *this) {
  if (!service_ || !tabs_) {
    return;
  }
  browser->GetTabStripModel()->AddObserver(this);
  ObserveTabDialogs();
  service_subscription_ = service_->Subscribe(
      base::BindRepeating(&ProjectNavigationController::ScheduleRefresh,
                          weak_factory_.GetWeakPtr()));
  tabs_subscription_ = tabs_->Subscribe(
      base::BindRepeating(&ProjectNavigationController::ScheduleRefresh,
                          weak_factory_.GetWeakPtr()));
  session_subscription_ =
      SessionRestore::RegisterOnSessionRestoredCallback(base::BindRepeating(
          [](base::WeakPtr<ProjectNavigationController> controller,
             Profile* profile, int count) {
            if (controller && controller->browser_->GetProfile() == profile)
              controller->ScheduleRefresh();
          },
          weak_factory_.GetWeakPtr()));
}

ProjectNavigationController::~ProjectNavigationController() {
  RestoreNativeViews();
}

ProjectNavigationController* ProjectNavigationController::From(
    BrowserWindowInterface* browser) {
  return browser ? Get(browser->GetUnownedUserDataHost()) : nullptr;
}

bool ProjectNavigationController::IsHubShowing(
    BrowserWindowInterface* browser) {
  const auto* controller = From(browser);
  return controller && controller->showing_hub();
}

bool ProjectNavigationController::IsHubShowingForView(BrowserView* view) {
  return view && IsHubShowing(view->browser());
}

bool ProjectNavigationController::ShouldShowNativeTab(
    const tabs::TabInterface* tab) {
  if (!tab || tab->IsPinned())
    return true;
  auto* controller =
      From(const_cast<tabs::TabInterface*>(tab)->GetBrowserWindowInterface());
  if (!controller || !controller->service_ || !controller->tabs_ ||
      !controller->sidebar_expanded_ || !controller->sidebar_tracker_.view() ||
      !controller->sidebar_tracker_.view()->GetVisible() ||
      !controller->browser_view() ||
      !controller->browser_view()->ShouldDrawVerticalTabStrip())
    return true;
  const auto id =
      controller->tabs_->PageId(const_cast<tabs::TabInterface*>(tab));
  return !controller->service_->FindPage(id) ||
         controller->tabs_->FindLiveTab(id, tab->GetBrowserWindowInterface()) !=
             tab;
}

void ProjectNavigationController::InvalidateNativeTabLayouts() {
  auto* view = browser_view();
  if (!view || !view->tab_strip_view())
    return;
  // 하위 collection의 선호 크기 캐시도 함께 갱신한다. 모델·TabView는 보존한다.
  auto invalidate = [](auto&& self, views::View* node) -> void {
    for (const auto& child : node->children())
      self(self, child);
    node->InvalidateLayout();
  };
  if (auto* root = view->tab_strip_view()->GetTabStripView())
    invalidate(invalidate, root);
}

std::optional<BrowserSurfacePresentation> GetProjectHubSurfacePresentation(
    const BrowserView* view) {
  if (!view ||
      !ProjectNavigationController::IsHubShowing(
          const_cast<Browser*>(view->browser())) ||
      !view->GetColorProvider())
    return std::nullopt;
  return ResolveBrowserSurfacePresentation(
      view->GetColorProvider()->GetColor(ui::kColorSysBase),
      ProjectNavigationController::kHubViewId, 0, 0);
}

std::unique_ptr<ui::SimpleMenuModel>
ProjectNavigationController::AppendNavigationMenu(ui::SimpleMenuModel& parent) {
  if (!service_ || !tabs_ ||
      GetBrowserSurfaceMode(*browser_) != BrowserSurfaceMode::kConnectedTabs) {
    return nullptr;
  }
  std::set<int> commands;
  CollectMenuCommands(parent, commands);
  int next = 1;
  const int container = AllocateMenuCommand(commands, next);
  auto menu = CreateNavigationMenu(&parent, container);
  if (parent.GetItemCount() > 0 &&
      parent.GetTypeAt(parent.GetItemCount() - 1) !=
          ui::MenuModel::TYPE_SEPARATOR) {
    parent.AddSeparator(ui::NORMAL_SEPARATOR);
  }
  parent.AddSubMenu(container, l10n_util::GetStringUTF16(IDS_YEE_PROJECTS),
                    menu.get());
  return menu;
}

std::unique_ptr<views::View> ProjectNavigationController::CreateSidebarView() {
  if (!service_)
    return nullptr;
  auto sidebar = std::make_unique<ProjectSidebarView>(
      browser_->GetProfile(),
      base::BindRepeating(
          [](base::WeakPtr<ProjectNavigationController> controller,
             std::string project, bool pointer) {
            if (!controller)
              return;
            controller->ShowHub(std::move(project));
            if (pointer && controller) {
              ClearHubPointerFocus(controller->hub_toolbar_tracker_.view(),
                                   controller->hub_tracker_.view());
            }
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::OpenPage,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::ClosePage,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::RenameProject,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::DeleteProject,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::ShowPageMenu,
                          weak_factory_.GetWeakPtr()));
  sidebar->SetID(kSidebarViewId);
  revealed_page_id_.clear();
  revealed_project_id_.clear();
  sidebar_tracker_.SetView(sidebar.get());
  ScheduleRefresh();
  return sidebar;
}

void ProjectNavigationController::SetSidebarExpandedPresentation(
    bool expanded) {
  const bool changed = sidebar_expanded_ != expanded;
  sidebar_expanded_ = expanded;
  if (changed)
    InvalidateNativeTabLayouts();
  if (auto* sidebar = sidebar_tracker_.view()) {
    auto* view = browser_view();
    const bool visible = expanded && view &&
                         view->ShouldDrawVerticalTabStrip() && service_ &&
                         !service_->projects().empty();
    if (sidebar->GetVisible() != visible) {
      sidebar->SetVisible(visible);
      InvalidateNativeTabLayouts();
    }
  }
}

bool ProjectNavigationController::can_create_project() const {
  return service_ && service_->writable();
}

void ProjectNavigationController::AttachBrowserView(BrowserView* view) {
  browser_view_tracker_.SetView(view);
}

BrowserView* ProjectNavigationController::browser_view() {
  return static_cast<BrowserView*>(browser_view_tracker_.view());
}

void ProjectNavigationController::CreateProject() {
  auto* view = browser_view();
  if (!service_ || !service_->writable() || !view || !view->GetWidget()) {
    return;
  }
  auto dialog = MakeNameDialog(
      IDS_YEE_PROJECT_NEW, u"", u"",
      base::BindOnce(
          [](base::WeakPtr<ProjectNavigationController> controller,
             std::u16string name, std::u16string description) {
            if (controller) {
              auto id = controller->service_->CreateProject(name, description);
              if (id) {
                controller->ShowHub(*id);
              }
            }
          },
          weak_factory_.GetWeakPtr()));
  views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), view->GetWidget()->GetNativeWindow(),
      view->GetWidget()->GetNativeView())
      ->Show();
}

void ProjectNavigationController::ShowHub(std::string project_id) {
  auto* view = browser_view();
  if (!service_ || !service_->FindProject(project_id) || !view) {
    return;
  }
  auto* model = browser_->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    auto* tab = model->GetTabAtIndex(i);
    if (IsInCurrentPageSurface(tab) &&
        (!tab->CanShowModalUI() || tab->IsBlocked()))
      return;
  }
  selected_project_id_ = project_id;
  showing_hub_ = true;
  Refresh();
  if (auto* header = hub_toolbar_tracker_.view();
      header && header->IsDrawn() && !header->children().empty()) {
    static_cast<ProjectNavigationView*>(hub_selector_tracker_.view())
        ->FocusName();
  } else if (auto* hub = static_cast<ProjectHubView*>(hub_tracker_.view())) {
    hub->FocusInitial();
  }
}

void ProjectNavigationController::ExitHub(bool focus_page) {
  if (!showing_hub_) {
    return;
  }
  showing_hub_ = false;
  if (auto* hub = hub_tracker_.view()) {
    hub->SetVisible(false);
  }
  if (auto* header = hub_toolbar_tracker_.view()) {
    header->SetVisible(false);
  }
  RestoreNativeViews();
  ScheduleRefresh();
  if (focus_page) {
    auto* contents = browser_->GetTabStripModel()->GetActiveWebContents();
    if (contents) {
      contents->Focus();
    }
  }
}

bool ProjectNavigationController::HandleCommand(int command) {
  if (!showing_hub_) {
    return false;
  }
  switch (command) {
    case IDC_TOGGLE_VERTICAL_TABS:
    case IDC_TOGGLE_VERTICAL_TABS_COLLAPSE:
    case IDC_FULLSCREEN:
    case IDC_SHOW_CUSTOMIZE_CHROME_SIDE_PANEL:
    case IDC_SHOW_BOOKMARK_SIDE_PANEL:
    case IDC_SHOW_HISTORY_SIDE_PANEL:
      return false;
    case IDC_BACK:
    case IDC_CLOSE_TAB:
      ExitHub();
      return true;
    case IDC_FORWARD:
    case IDC_RELOAD:
    case IDC_RELOAD_BYPASSING_CACHE:
    case IDC_STOP:
      return true;
    default:
      // 주소줄, 인쇄, 찾기, 창 닫기 등 실제 페이지 명령은 대상 페이지를
      // 드러낸다.
      ExitHub(false);
      return false;
  }
}

std::optional<bool> ProjectNavigationController::CommandEnabledOverride(
    int command) const {
  if (!showing_hub_)
    return std::nullopt;
  switch (command) {
    case IDC_BACK:
      return true;
    case IDC_FORWARD:
    case IDC_RELOAD:
    case IDC_RELOAD_BYPASSING_CACHE:
    case IDC_STOP:
      return false;
    default:
      return std::nullopt;
  }
}

bool ProjectNavigationController::IsHubHeaderControlAt(
    const gfx::Point& browser_view_point) {
  auto* view = browser_view();
  auto* header = hub_toolbar_tracker_.view();
  if (!showing_hub_ || !view || !header || !header->IsDrawn())
    return false;
  gfx::Point local = browser_view_point;
  views::View::ConvertPointToScreen(view, &local);
  views::View::ConvertPointFromScreen(header, &local);
  if (!header->HitTestPoint(local))
    return false;
  for (auto* target = header->GetEventHandlerForPoint(local);
       target && target != header; target = target->parent()) {
    if (views::AsViewClass<views::Button>(target) && target->GetEnabled())
      return true;
  }
  return false;
}

void ProjectNavigationController::AfterBrowserLayout() {
  auto* view = browser_view();
  if (view) {
    SetSidebarExpandedPresentation(sidebar_expanded_);
  }
  auto* hub = hub_tracker_.view();
  auto* header = hub_toolbar_tracker_.view();
  if (!showing_hub_ || !view) {
    return;
  }
  if (!hub || !header) {
    ScheduleRefresh();
    return;
  }
  auto conceal = [this](views::View* target, bool hide) {
    if (!target) {
      return;
    }
    auto it = std::ranges::find_if(native_states_, [target](const auto& state) {
      return state.tracker->view() == target;
    });
    if (it == native_states_.end()) {
      native_states_.push_back({std::make_unique<views::ViewTracker>(target),
                                target->GetVisible(), target->GetEnabled(),
                                target->GetViewAccessibility().GetIsIgnored(),
                                target->GetViewAccessibility().IsLeaf(), hide});
    }
    if (hide) {
      target->SetVisible(false);
    } else {
      // 자식 AX 상태는 이 루트의 leaf 처리로 이미 가려진다. 조상에 의해
      // pruned된 자식의 ignored 값을 다시 저장·복원하면 복귀 후에도 숨는다.
      target->SetEnabled(false);
      target->GetViewAccessibility().SetIsLeaf(true);
      target->GetViewAccessibility().SetIsIgnored(true);
    }
  };
  auto* multi = view->multi_contents_view();
  // MCV 자체의 가시성은 native layout 불변식이다. 실제 웹 면만 숨긴다.
  conceal(multi, false);
  for (const auto& container : multi->contents_container_views()) {
    conceal(container->contents_view(), true);
    if (container->devtools_web_view()->GetVisible()) {
      conceal(container->devtools_web_view(), true);
    }
  }
  conceal(view->toolbar(), false);
  // Native Toolbar의 배경·클립은 남기고 페이지 조작만 가린다.
  for (const auto& child : view->toolbar()->children())
    conceal(child, true);
  hub->SetBoundsRect(multi->bounds());
  hub->layer()->SetRoundedCornerRadius(multi->GetBackgroundRadii());
  hub->layer()->SetIsFastRoundedCorner(true);
  // 본문 다음에 두어 InfoBar·공통 outline·Sidebar flyout의 순서를 보존한다.
  if (auto index = view->GetIndexOf(multi))
    view->ReorderChildView(hub, *index + 1);

  auto* toolbar = view->toolbar();
  // macOS immersive에서는 Toolbar가 다른 부모로 이동할 수 있다.
  if (header->parent() != toolbar->parent()) {
    toolbar->parent()->AddChildView(header->parent()->RemoveChildViewT(header));
  }
  const auto& resolved_header = toolbar->GetCompactShellHeaderBoundsForLayout();
  const bool has_header = resolved_header && !resolved_header->IsEmpty();
  const auto* focused = view->GetFocusManager()
                            ? view->GetFocusManager()->GetFocusedView()
                            : nullptr;
  const bool header_had_focus = focused && header->Contains(focused);
  auto* hub_view = static_cast<ProjectHubView*>(hub);
  header->SetVisible(has_header);
  if (has_header) {
    const gfx::Rect physical = view->GetMirroredRect(*resolved_header);
    // Mac 전체화면에서는 Toolbar와 본문이 서로 다른 Widget에 있다.
    // 같은 View 트리를 요구하는 RectToTarget 대신 화면 좌표를 거친다.
    gfx::Rect screen_header = physical;
    views::View::ConvertRectToScreen(view, &screen_header);
    const gfx::Rect parent_physical =
        views::View::ConvertRectFromScreen(toolbar->parent(), screen_header);
    header->SetBoundsRect(header->parent()->GetMirroredRect(parent_physical));
    const int control_exclusion =
        view->ShouldDrawVerticalTabStrip()
            ? std::max(
                  0, view->GetYeeLeadingWindowControlsSafeEdge() - physical.x())
            : 0;
    const int inset =
        yee::kSidebarMetrics.browser_surface_header_control_edge_inset;
    const gfx::Insets padding =
        base::i18n::IsRTL()
            ? gfx::Insets::TLBR(2, inset, 2, inset + control_exclusion)
            : gfx::Insets::TLBR(2, inset + control_exclusion, 2, inset);
    auto* layout = static_cast<views::FlexLayout*>(header->GetLayoutManager());
    if (layout->interior_margin() != padding) {
      layout->SetInteriorMargin(padding);
    }
  }
  header->DeprecatedLayoutImmediately();
  hub->DeprecatedLayoutImmediately();
  header->parent()->ReorderChildView(header,
                                     header->parent()->children().size() - 1);
  if (!has_header && header_had_focus) {
    hub_view->FocusInitial();
  }
}

void ProjectNavigationController::RestoreNativeViews() {
  for (auto& state : native_states_) {
    if (auto* view = state.tracker->view()) {
      if (state.visibility_changed) {
        view->SetVisible(state.visible);
      } else {
        view->SetEnabled(state.enabled);
        view->GetViewAccessibility().SetIsLeaf(state.leaf);
        view->GetViewAccessibility().SetIsIgnored(state.ignored);
      }
    }
  }
  native_states_.clear();
}

void ProjectNavigationController::OnTabStripModelChanged(
    TabStripModel* model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    ExitHub(false);
  }
  ObserveTabDialogs();
  ScheduleRefresh();
}

void ProjectNavigationController::ObserveTabDialogs() {
  auto* model = browser_->GetTabStripModel();
  modal_subscriptions_.clear();
  for (int i = 0; i < model->count(); ++i) {
    modal_subscriptions_.push_back(
        model->GetTabAtIndex(i)->RegisterModalUIChanged(
            base::BindRepeating(&ProjectNavigationController::OnModalChanged,
                                weak_factory_.GetWeakPtr())));
    modal_subscriptions_.push_back(
        model->GetTabAtIndex(i)->RegisterBlockedStateChanged(
            base::BindRepeating(
                [](base::WeakPtr<ProjectNavigationController> controller,
                   tabs::TabInterface* tab, bool blocked) {
                  if (controller && blocked)
                    controller->OnModalChanged(tab);
                },
                weak_factory_.GetWeakPtr())));
  }
}

void ProjectNavigationController::OnModalChanged(tabs::TabInterface* tab) {
  if (IsInCurrentPageSurface(tab) &&
      (!tab->CanShowModalUI() || tab->IsBlocked())) {
    ExitHub(false);
  }
}

bool ProjectNavigationController::IsInCurrentPageSurface(
    const tabs::TabInterface* tab) const {
  const auto* active = browser_->GetTabStripModel()->GetActiveTab();
  if (!tab || !active)
    return false;
  // 허브가 실제 WebView를 숨기므로 가시성 대신 native 활성 탭과 분할 소속을
  // 따른다.
  return tab == active ||
         (active->GetSplit() && active->GetSplit() == tab->GetSplit());
}

void ProjectNavigationController::ScheduleRefresh() {
  if (refresh_pending_) {
    return;
  }
  refresh_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ProjectNavigationController::Refresh,
                                weak_factory_.GetWeakPtr()));
}

void ProjectNavigationController::Refresh() {
  refresh_pending_ = false;
  if (!service_ || !tabs_)
    return;
  tabs_->PruneClosedTabs();
  if (!service_->FindProject(selected_project_id_)) {
    ExitHub(false);
    selected_project_id_ =
        service_->projects().empty() ? "" : service_->projects().front().id;
  }
  auto* view = browser_view();
  const auto* project = service_->FindProject(selected_project_id_);
  auto make_page = [this](const ProjectPage& page) {
    return MakePageItem(page, *tabs_, *browser_, showing_hub_);
  };
  if (auto* sidebar =
          static_cast<ProjectSidebarView*>(sidebar_tracker_.view())) {
    const auto active_id =
        tabs_->PageId(browser_->GetTabStripModel()->GetActiveTab());
    const auto* active = service_->FindPage(active_id);
    const std::string owner = active ? active->project_id : "";
    if (active_id != revealed_page_id_ || owner != revealed_project_id_) {
      revealed_page_id_ = active_id;
      revealed_project_id_ = owner;
      if (active)
        sidebar->RevealPage(owner);
    }
    std::vector<NavigationProject> projects;
    for (const auto& record : service_->projects()) {
      NavigationProject item{record.id, record.name, {}};
      for (const auto& id : record.page_ids) {
        if (const auto* page = service_->FindPage(id))
          item.pages.push_back(make_page(*page));
      }
      projects.push_back(std::move(item));
    }
    sidebar->SetProjects(projects, showing_hub_ ? selected_project_id_ : "",
                         service_->writable());
    SetSidebarExpandedPresentation(sidebar_expanded_);
  }
  InvalidateNativeTabLayouts();
  if (!showing_hub_ || !view || !project)
    return;
  HubFocus focus;
  std::optional<int> scroll_position;
  if (auto* old = static_cast<ProjectHubView*>(hub_tracker_.view())) {
    if (rendered_hub_project_id_ == project->id) {
      focus = old->GetFocusedAction();
      scroll_position = old->scroll_position();
    }
    old->parent()->RemoveChildViewT(old);
  }
  std::vector<HubPage> pages;
  for (const auto& id : project->page_ids) {
    if (const auto* page = service_->FindPage(id)) {
      pages.push_back(make_page(*page));
    }
  }
  auto* active = browser_->GetTabStripModel()->GetActiveWebContents();
  const auto* attached = service_->FindPage(
      tabs_->PageId(browser_->GetTabStripModel()->GetActiveTab()));
  const bool can_add =
      active &&
      ProjectService::CanStorePageURL(active->GetLastCommittedURL()) &&
      (!attached || attached->project_id != project->id);
  HubActions actions{
      base::BindRepeating(&ProjectNavigationController::AttachCurrentPage,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::RenameProject,
                          weak_factory_.GetWeakPtr(), project->id),
      base::BindRepeating(&ProjectNavigationController::DeleteProject,
                          weak_factory_.GetWeakPtr(), project->id),
      base::BindRepeating(&ProjectNavigationController::OpenPage,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::RemovePage,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::ClosePage,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::ShowPageMenu,
                          weak_factory_.GetWeakPtr())};
  auto* hub = view->AddChildView(std::make_unique<ProjectHubView>(
      browser_->GetProfile(), project->name, project->description, pages,
      service_->writable(), can_add,
      SessionRestore::IsRestoring(browser_->GetProfile()), actions));
  hub->SetID(kHubViewId);
  hub_tracker_.SetView(hub);
  rendered_hub_project_id_ = project->id;
  if (!hub_toolbar_tracker_.view()) {
    auto header = std::make_unique<views::View>();
    header->SetID(kHubHeaderViewId);
    // Omnibox의 별도 합성 레이어까지 같은 순서로 가린다.
    header->SetPaintToLayer();
    header->layer()->SetFillsBoundsOpaquely(false);
    // 실제 Toolbar가 공통 Surface 색과 둥근 클립을 그린다.
    header->SetBackground(nullptr);
    header->SetLayoutManager(std::make_unique<views::FlexLayout>())
        ->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
        .SetInteriorMargin(gfx::Insets::VH(2, 10))
        .SetDefault(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, 8))
        .SetDefault(views::kFlexBehaviorKey,
                    views::FlexSpecification(
                        views::MinimumFlexSizeRule::kScaleToMinimum,
                        views::MaximumFlexSizeRule::kPreferred));
    hub_selector_tracker_.SetView(
        header->AddChildView(std::make_unique<ProjectNavigationView>(
            base::BindRepeating(
                [](base::WeakPtr<ProjectNavigationController> controller,
                   bool pointer) {
                  if (!controller)
                    return;
                  controller->OpenSelectedHub();
                  if (pointer && controller) {
                    ClearHubPointerFocus(
                        controller->hub_toolbar_tracker_.view(),
                        controller->hub_tracker_.view());
                  }
                },
                weak_factory_.GetWeakPtr()),
            base::BindRepeating(&ProjectNavigationController::ShowPicker,
                                weak_factory_.GetWeakPtr()))));
    hub_toolbar_tracker_.SetView(
        view->toolbar()->parent()->AddChildView(std::move(header)));
  }
  if (auto* selector =
          static_cast<ProjectNavigationView*>(hub_selector_tracker_.view()))
    selector->SetProject(project->name, true);
  hub_toolbar_tracker_.view()->SetVisible(true);
  AfterBrowserLayout();
  hub->RestoreFocus(focus);
  if (scroll_position) {
    hub->RestoreScrollPosition(*scroll_position);
  }
  view->InvalidateLayout();
}

void ProjectNavigationController::ShowPicker() {
  auto* const anchor = hub_toolbar_tracker_.view();
  if (!service_ || !tabs_ || !showing_hub_ || !anchor || !anchor->GetWidget()) {
    return;
  }
  if (picker_runner_ && picker_runner_->IsRunning()) {
    return;
  }
  picker_runner_.reset();
  picker_model_ = CreateNavigationMenu();
  picker_runner_ = std::make_unique<views::MenuRunner>(picker_model_.get(), 0);
  picker_runner_->RunMenuAt(
      anchor->GetWidget(), nullptr, anchor->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
}

std::unique_ptr<ProjectPicker>
ProjectNavigationController::CreateNavigationMenu(const ui::MenuModel* parent,
                                                  int container_command) {
  std::vector<ProjectPageItem> pages;
  if (const auto* project = service_->FindProject(selected_project_id_)) {
    for (const auto& id : project->page_ids) {
      if (const auto* page = service_->FindPage(id))
        pages.push_back(MakePageItem(*page, *tabs_, *browser_, showing_hub_));
    }
  }
  return std::make_unique<ProjectPicker>(
      *service_, pages, selected_project_id_, service_->writable(),
      base::BindRepeating(&ProjectNavigationController::ShowHub,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::CreateProject,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&ProjectNavigationController::OpenPage,
                          weak_factory_.GetWeakPtr()),
      parent, container_command);
}

void ProjectNavigationController::OpenSelectedHub() {
  ShowHub(selected_project_id_);
}

void ProjectNavigationController::RenameProject(std::string project_id) {
  auto* view = browser_view();
  const auto* project = service_->FindProject(project_id);
  if (!view || !project || !service_->writable()) {
    return;
  }
  auto dialog = MakeNameDialog(
      IDS_YEE_PROJECT_RENAME, project->name, project->description,
      base::BindOnce(
          [](base::WeakPtr<ProjectNavigationController> controller,
             std::string id, std::u16string name, std::u16string description) {
            if (controller) {
              controller->service_->UpdateProject(id, name, description);
            }
          },
          weak_factory_.GetWeakPtr(), project->id));
  views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), view->GetWidget()->GetNativeWindow(),
      view->GetWidget()->GetNativeView())
      ->Show();
}

void ProjectNavigationController::DeleteProject(std::string project_id) {
  auto* view = browser_view();
  if (!view || !service_->writable() || !service_->FindProject(project_id)) {
    return;
  }
  auto dialog = MakeConfirmDialog(
      IDS_YEE_PROJECT_DELETE, IDS_YEE_PROJECT_DELETE_DESCRIPTION,
      IDS_YEE_PROJECT_DELETE,
      base::BindOnce(
          [](base::WeakPtr<ProjectNavigationController> controller,
             std::string id) {
            if (controller) {
              controller->service_->DeleteProject(id);
            }
          },
          weak_factory_.GetWeakPtr(), project_id));
  views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), view->GetWidget()->GetNativeWindow(),
      view->GetWidget()->GetNativeView())
      ->Show();
}

void ProjectNavigationController::AttachCurrentPage() {
  auto* tab = browser_->GetTabStripModel()->GetActiveTab();
  auto* view = browser_view();
  if (!tab || !view || !service_->writable()) {
    return;
  }
  const std::string page_id = tabs_->PageId(tab);
  const auto* page = service_->FindPage(page_id);
  if (page && page->project_id != selected_project_id_) {
    auto dialog = MakeConfirmDialog(
        IDS_YEE_PROJECT_MOVE, IDS_YEE_PROJECT_MOVE_DESCRIPTION,
        IDS_YEE_PROJECT_MOVE_ACTION,
        base::BindOnce(
            [](base::WeakPtr<ProjectNavigationController> controller,
               base::WeakPtr<tabs::TabInterface> target, std::string project_id,
               std::string page_id, std::string old_owner) {
              if (controller && target) {
                controller->tabs_->Attach(*target, project_id, page_id,
                                          old_owner, true);
              }
            },
            weak_factory_.GetWeakPtr(), tab->GetWeakPtr(), selected_project_id_,
            page_id, page->project_id),
        service_->FindProject(page->project_id)->name + u" → " +
            service_->FindProject(selected_project_id_)->name);
    views::DialogDelegate::CreateDialogWidget(
        std::move(dialog), view->GetWidget()->GetNativeWindow(),
        view->GetWidget()->GetNativeView())
        ->Show();
  } else {
    tabs_->Attach(*tab, selected_project_id_, page_id);
  }
}

void ProjectNavigationController::OpenPage(std::string page_id) {
  const auto* page = service_->FindPage(page_id);
  if (!page || SessionRestore::IsRestoring(browser_->GetProfile())) {
    return;
  }
  if (auto* tab = tabs_->FindLiveTab(page_id, browser_)) {
    ExitHub(false);
    tab->LoadIfNeeded();
    auto* model = browser_->GetTabStripModel();
    const int index = model->GetIndexOfTab(tab);
    if (index >= 0) {
      model->ActivateTabAt(index);
      browser_->GetWindow()->Activate();
      tab->GetContents()->Focus();
    }
    return;
  }
  // 동기 삽입의 반환 WebContents에만 UUID를 붙인다. URL 일치 탭은 찾지 않는다.
  auto release = tabs_->BeginReopenPage(page_id, browser_);
  if (release.is_null())
    return;
  base::ScopedClosureRunner pending_open(std::move(release));
  NavigateParams params(browser_->GetBrowserForMigrationOnly(), page->url,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
  if (auto* created = params.navigated_or_inserted_contents.get()) {
    if (auto* tab = tabs::TabInterface::MaybeGetFromContents(created)) {
      tabs_->BindRestoredPage(*tab, page_id);
    }
  }
  ExitHub(false);
}

void ProjectNavigationController::RemovePage(std::string page_id) {
  service_->RemovePage(page_id);
}

void ProjectNavigationController::ClosePage(std::string page_id) {
  if (auto* tab = tabs_->FindLiveTab(page_id, browser_)) {
    if (auto* target = tab->GetBrowserWindowInterface()) {
      auto* model = target->GetTabStripModel();
      const int index = model->GetIndexOfTab(tab);
      if (index >= 0)
        model->delegate()->CloseTab(tab, CloseTabSource::kFromMouse);
    }
  }
}

void ProjectNavigationController::ShowPageMenu(std::string page_id,
                                               views::View* source,
                                               const gfx::Point& point,
                                               ui::mojom::MenuSourceType type) {
  auto* tab = tabs_->FindLiveTab(page_id, browser_);
  auto* target = tab ? tab->GetBrowserWindowInterface() : nullptr;
  auto* target_view = target ? BrowserView::GetBrowserViewForBrowser(
                                   target->GetBrowserForMigrationOnly())
                             : nullptr;
  if (!target_view || !target_view->tab_strip_view())
    return;
  const int index = target->GetTabStripModel()->GetIndexOfTab(tab);
  auto* native = target_view->tab_strip_view()->GetTabAnchorViewAt(index);
  // legacy 수평 TabStrip은 source가 실제 Tab인지 검사하므로 native view를
  // 전달한다.
  if (native && native->context_menu_controller())
    native->context_menu_controller()->ShowContextMenuForView(native, point,
                                                              type);
}

}  // namespace yee::projects
