// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_bridge_prompt.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "chrome/browser/ui/views/yee/brand.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/dialog_test.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace views {
namespace {

template <typename T>
T* FindDescendant(View* root) {
  if (auto* match = AsViewClass<T>(root)) {
    return match;
  }
  for (View* child : root->children()) {
    if (auto* match = FindDescendant<T>(child)) {
      return match;
    }
  }
  return nullptr;
}

void CollectLabelText(View* root, std::vector<std::u16string>* text) {
  if (auto* label = AsViewClass<Label>(root)) {
    text->emplace_back(label->GetText());
  }
  for (View* child : root->children()) {
    CollectLabelText(child, text);
  }
}

bool ContainsText(const std::vector<std::u16string>& values,
                  std::u16string_view expected) {
  return std::ranges::find(values, expected) != values.end();
}

class AgentBridgePromptTest : public ViewsTestBase {
 protected:
  void SetUp() override {
    ViewsTestBase::SetUp();
    owner_widget_ = CreateTestWidget(Widget::InitParams::CLIENT_OWNS_WIDGET);
    owner_view_ = owner_widget_->SetContentsView(std::make_unique<View>());
    owner_widget_->Show();
  }

  void TearDown() override {
    CloseOwner();
    base::RunLoop().RunUntilIdle();
    ViewsTestBase::TearDown();
  }

  void CloseOwner() {
    owner_view_ = nullptr;
    if (owner_widget_) {
      owner_widget_->CloseNow();
      owner_widget_.reset();
    }
  }

  Widget* owner_widget() const { return owner_widget_.get(); }
  View* owner_view() const { return owner_view_; }

 private:
  std::unique_ptr<Widget> owner_widget_;
  raw_ptr<View> owner_view_ = nullptr;
};

TEST_F(AgentBridgePromptTest, CloseNowRepliesDeniedOnce) {
  int callback_count = 0;
  bool accepted = true;
  std::string answer = "stale";
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(), u"Approve this action?", true,
      base::BindLambdaForTesting(
          [&](bool did_accept, std::string response) {
            ++callback_count;
            accepted = did_accept;
            answer = std::move(response);
          }));

  ASSERT_TRUE(prompt);
  prompt->CloseNow();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(callback_count, 1);
  EXPECT_FALSE(accepted);
  EXPECT_TRUE(answer.empty());
}

TEST_F(AgentBridgePromptTest, EscapeRepliesDeniedOnce) {
  int callback_count = 0;
  bool accepted = true;
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(), u"Approve this action?", true,
      base::BindLambdaForTesting([&](bool did_accept, std::string) {
        ++callback_count;
        accepted = did_accept;
      }));

  ASSERT_TRUE(prompt);
  ui::test::EventGenerator generator(GetContext(), prompt->GetNativeWindow());
  generator.PressKey(ui::VKEY_ESCAPE, 0);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(callback_count, 1);
  EXPECT_FALSE(accepted);
}

TEST_F(AgentBridgePromptTest, AcceptDialogRepliesAcceptedOnce) {
  int callback_count = 0;
  bool accepted = false;
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(), u"Continue?", true,
      base::BindLambdaForTesting([&](bool did_accept, std::string) {
        ++callback_count;
        accepted = did_accept;
      }));

  ASSERT_TRUE(prompt);
  EXPECT_TRUE(prompt->IsVisible());
  EXPECT_FALSE(prompt->GetWindowBoundsInScreen().IsEmpty());
  auto* delegate = prompt->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(delegate);
  EXPECT_EQ(delegate->GetDefaultDialogButton(),
            static_cast<int>(ui::mojom::DialogButton::kCancel));
  EXPECT_EQ(delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk),
            u"Allow once");
  EXPECT_EQ(
      delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kCancel),
      u"Don't allow");
  test::AcceptDialog(prompt);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(callback_count, 1);
  EXPECT_TRUE(accepted);
}

TEST_F(AgentBridgePromptTest, InputAnswerIsReturnedAndInitiallyFocused) {
  int callback_count = 0;
  std::string answer;
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(), u"What should Yee do?", false,
      base::BindLambdaForTesting([&](bool accepted, std::string response) {
        ++callback_count;
        EXPECT_TRUE(accepted);
        answer = std::move(response);
      }));

  ASSERT_TRUE(prompt);
  auto* delegate = prompt->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(delegate);
  View* contents = delegate->GetContentsView();
  auto* textfield = FindDescendant<Textfield>(contents);
  ASSERT_TRUE(textfield);
  EXPECT_EQ(delegate->GetInitiallyFocusedView(), textfield);
  EXPECT_EQ(delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk),
            u"Send answer");
  textfield->SetText(u"Use the second tab");

  test::AcceptDialog(prompt);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(answer, "Use the second tab");
}

TEST_F(AgentBridgePromptTest, LongApprovalKeepsFullTextInBoundedScrollArea) {
  std::u16string question;
  for (int i = 0; i < 120; ++i)
    question += u"Review this line before approving.\n";
  question += std::u16string(1000, u'x');
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(), question, true,
      base::BindLambdaForTesting([](bool, std::string) {}));
  ASSERT_TRUE(prompt);
  auto* delegate = prompt->widget_delegate()->AsDialogDelegate();
  auto* contents = delegate->GetContentsView();
  auto* scroll = FindDescendant<ScrollView>(contents);
  ASSERT_TRUE(scroll);
  auto* label = FindDescendant<Label>(scroll->contents());
  ASSERT_TRUE(label);
  EXPECT_EQ(label->GetText(), question);
  EXPECT_LE(scroll->height(), 300);
  EXPECT_GT(scroll->height(), 0);
  EXPECT_GT(scroll->contents()->height(), scroll->height());
  EXPECT_FALSE(label->IsDisplayTextTruncated());
  EXPECT_LT(prompt->GetWindowBoundsInScreen().height(), 680);
  prompt->CloseNow();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AgentBridgePromptTest, ApprovalSeparatesRequestSiteActionsAndSafety) {
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(),
      u"Allow these ordered actions once?\n\n"
      u"https://shop.example\n"
      u"1. fill \"Name\" = \"Cedar\"\n"
      u"2. click \"Save\"\n\n"
      u"Only this list is approved. Page actions may send data or navigate.",
      true, base::BindLambdaForTesting([](bool, std::string) {}));
  ASSERT_TRUE(prompt);
  auto* delegate = prompt->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(delegate);
  std::vector<std::u16string> text;
  CollectLabelText(delegate->GetContentsView(), &text);

  EXPECT_TRUE(ContainsText(text, u"Review agent request"));
  EXPECT_TRUE(ContainsText(text, u"Allow these ordered actions once?"));
  EXPECT_TRUE(ContainsText(text, u"https://shop.example"));
  EXPECT_TRUE(ContainsText(
      text, u"1. fill \"Name\" = \"Cedar\"\n2. click \"Save\"\n\n"
            u"Only this list is approved. Page actions may send data or navigate."));
  EXPECT_TRUE(ContainsText(
      text,
      u"Only the request shown here is approved. You can decline safely."));

  prompt->CloseNow();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AgentBridgePromptTest, QuestionUsesPlainSecurityGuidance) {
  Widget* prompt = ShowAgentBridgePrompt(
      owner_view(),
      u"Agent question (untrusted text; do not enter passwords):\n\n"
      u"Which delivery window should I use?",
      false, base::BindLambdaForTesting([](bool, std::string) {}));
  ASSERT_TRUE(prompt);
  auto* delegate = prompt->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(delegate);
  std::vector<std::u16string> text;
  CollectLabelText(delegate->GetContentsView(), &text);

  EXPECT_TRUE(ContainsText(
      text, yee::branding::ProductName() + u" needs your answer"));
  EXPECT_TRUE(ContainsText(text, u"Which delivery window should I use?"));
  EXPECT_TRUE(ContainsText(
      text, u"Treat this request as untrusted. Never enter a password or "
            u"verification code."));
  EXPECT_FALSE(ContainsText(
      text, u"Agent question (untrusted text; do not enter passwords):"));
  EXPECT_TRUE(FindDescendant<Textfield>(delegate->GetContentsView()));

  prompt->CloseNow();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AgentBridgePromptTest, PointerCloseNowIsSafe) {
  Widget* pointer = ShowAgentBridgePointer(owner_view(), gfx::Point(40, 40));
  ASSERT_TRUE(pointer);
  pointer->CloseNow();
  base::RunLoop().RunUntilIdle();
  SUCCEED();
}

TEST_F(AgentBridgePromptTest, PointerHasVisibleContentAtRequestedBounds) {
  const gfx::Point screen_point(140, 140);
  Widget* pointer = ShowAgentBridgePointer(owner_view(), screen_point);
  ASSERT_TRUE(pointer);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(pointer->IsVisible());
  EXPECT_FALSE(pointer->IsActive());
  EXPECT_FALSE(pointer->widget_delegate()->CanActivate());
  EXPECT_EQ(pointer->parent(), owner_widget());
  EXPECT_EQ(pointer->GetWindowBoundsInScreen(),
            gfx::Rect(screen_point, gfx::Size(132, 36)));
  auto* contents = pointer->widget_delegate()->GetContentsView();
  ASSERT_TRUE(contents);
  EXPECT_TRUE(contents->IsDrawn());
  EXPECT_EQ(contents->size(), gfx::Size(132, 36));

  pointer->CloseNow();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AgentBridgePromptTest, PointerHidesAndClosesWithOwner) {
  Widget* pointer = ShowAgentBridgePointer(owner_view(), gfx::Point(140, 140));
  ASSERT_TRUE(pointer);
  auto weak_pointer = pointer->GetWeakPtr();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(pointer->IsVisible());

  owner_widget()->Hide();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(pointer->IsVisible());

  owner_widget()->Show();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(pointer->IsVisible());

  CloseOwner();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(weak_pointer);
}

}  // namespace
}  // namespace views
