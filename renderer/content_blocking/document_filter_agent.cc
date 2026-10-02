// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "chrome/renderer/yee_content_blocking/document_filter_agent.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/renderer/yee_content_blocking/scripts.h"
#include "components/content_settings/renderer/content_settings_agent_impl.h"
#include "components/yee_content_blocking/document_engine.h"
#include "components/yee_content_blocking/settings.h"
#include "content/public/renderer/render_frame.h"
#include "gin/converter.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/web_isolated_world_info.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "v8/include/v8.h"

namespace yee::content_blocking {
namespace {
void InitializeIsolatedWorld() {
  static const bool configured = [] {
    blink::WebIsolatedWorldInfo info;
    info.security_origin =
        blink::WebSecurityOrigin::Create(GURL("chrome://yee-content-blocking"));
    info.content_security_policy = blink::WebString::FromUtf8("");
    blink::SetIsolatedWorldInfo(ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING, info);
    return true;
  }();
  (void)configured;
}
std::optional<std::vector<std::string>> ReadStrings(
    v8::Local<v8::Context> context,
    v8::Local<v8::Value> value,
    uint32_t limit) {
  std::vector<std::string> result;
  if (!value->IsArray())
    return std::nullopt;
  auto array = value.As<v8::Array>();
  if (array->Length() > limit)
    return std::nullopt;
  for (uint32_t i = 0; i < array->Length(); ++i) {
    v8::Local<v8::Value> item;
    if (!array->Get(context, i).ToLocal(&item) || !item->IsString())
      return std::nullopt;
    std::string text = gin::V8ToString(v8::Isolate::GetCurrent(), item);
    if (text.size() > 512)
      return std::nullopt;
    result.push_back(std::move(text));
  }
  return result;
}

bool EnabledForFrame(blink::WebLocalFrame* frame) {
  if (!frame)
    return false;
  const GURL site = url::Origin(frame->Top()->GetSecurityOrigin()).GetURL();
  if (!EnabledForSite(site))
    return false;
  content::RenderFrame* render_frame =
      content::RenderFrame::FromWebFrame(frame);
  auto* settings_agent =
      content_settings::ContentSettingsAgentImpl::Get(render_frame);
  if (!settings_agent)
    return true;
  RendererContentSettingRules* rules =
      settings_agent->GetRendererContentSettingRules();
  if (!rules || rules->yee_content_blocking_rules.empty())
    return true;
  for (const ContentSettingPatternSource& rule :
       rules->yee_content_blocking_rules) {
    if (rule.secondary_pattern.Matches(site))
      return rule.GetContentSetting() != CONTENT_SETTING_BLOCK;
  }
  return true;
}
}  // namespace
void DocumentFilterAgent::ApplyGeneric(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  args.GetReturnValue().Set(false);
  if (args.Length() != 3)
    return;
  auto context = args.GetIsolate()->GetCurrentContext();
  // Derive the live frame from the executing context. No retained frame pointer
  // can survive document/frame destruction through a JavaScript closure.
  auto* frame = blink::WebLocalFrame::FrameForContext(context);
  if (!EnabledForFrame(frame))
    return;
  auto classes = ReadStrings(context, args[0], 256);
  auto ids = ReadStrings(context, args[1], 256);
  auto exceptions = ReadStrings(context, args[2], 8192);
  // An incomplete exception list could turn an allow rule into overblocking.
  // Skip this batch if any of the bounded inputs cannot be read in full.
  if (!classes || !ids || !exceptions)
    return;
  auto* agent = Get(content::RenderFrame::FromWebFrame(frame));
  if (agent && agent->applied_) {
    RendererDocumentEngine().GenericSelectors(
        std::move(*classes), std::move(*ids), std::move(*exceptions),
        base::BindOnce(&DocumentFilterAgent::InsertGenericSelectors,
                       agent->weak_factory_.GetWeakPtr()));
    args.GetReturnValue().Set(true);
  }
}

void DocumentFilterAgent::ProceduralEnabled(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  auto* frame = blink::WebLocalFrame::FrameForContext(
      args.GetIsolate()->GetCurrentContext());
  args.GetReturnValue().Set(EnabledForFrame(frame));
}

void DocumentFilterAgent::InsertProceduralStyle(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  args.GetReturnValue().Set(false);
  if (args.Length() != 2 || !args[0]->IsString() || !args[1]->IsString())
    return;
  auto* frame = blink::WebLocalFrame::FrameForContext(
      args.GetIsolate()->GetCurrentContext());
  if (!EnabledForFrame(frame))
    return;
  auto* agent = Get(content::RenderFrame::FromWebFrame(frame));
  if (!agent || !agent->applied_)
    return;
  const std::string marker = gin::V8ToString(args.GetIsolate(), args[0]);
  const std::string declarations = gin::V8ToString(args.GetIsolate(), args[1]);
  // The private isolated-world runtime validates a single CSS declaration
  // block, then tags matching nodes. Native user-origin CSS keeps page styles
  // from overriding the result. Bound its per-document sheet storage.
  if (!base::StartsWith(marker, "data-yee-cb-") || marker.size() > 96 ||
      !base::ContainsOnlyChars(std::string_view(marker).substr(12),
                              "0123456789abcdef-") ||
      declarations.empty() || declarations.size() > 32 * 1024 ||
      agent->procedural_style_keys_.size() >= 1024 ||
      agent->procedural_style_bytes_ + declarations.size() > 256 * 1024)
    return;
  const std::string css = "[" + marker + "]{" + declarations + "}";
  agent->procedural_style_keys_.push_back(frame->GetDocument().InsertStyleSheet(
      blink::WebString::FromUtf8(css), nullptr, blink::WebCssOrigin::kUser));
  agent->procedural_style_bytes_ += declarations.size();
  args.GetReturnValue().Set(true);
}

void DocumentFilterAgent::InsertGenericSelectors(
    std::vector<std::string> selectors) {
  // Document replacement cancels the reply. Recheck site settings as they
  // may have changed while this batch was on the worker.
  auto alive = weak_factory_.GetWeakPtr();
  if (applied_ && EnabledForFrame(render_frame()->GetWebFrame()))
    InsertSelectors(selectors);
  if (!alive)
    return;
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING,
      blink::WebScriptSource(blink::WebString::FromUtf8(
          "globalThis.__yeeGenericComplete?.()")),
      blink::BackForwardCacheAware::kAllow);
}

void DocumentFilterAgent::InsertSelectors(
    const std::vector<std::string>& selectors) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.InsertSelectors");
  base::ListValue candidates;
  size_t bytes = 0;
  for (const auto& selector : selectors) {
    if (selector.empty() || styles_.Contains(selector) ||
        selector.size() + 25 > SelectorStyles::kChunkBytes)
      continue;
    if (bytes + selector.size() > SelectorStyles::kMaxBytes ||
        candidates.size() >= SelectorStyles::kMaxSelectors)
      break;
    candidates.Append(selector);
    bytes += selector.size();
  }
  if (candidates.empty())
    return;
  InitializeIsolatedWorld();
  auto* frame = render_frame()->GetWebFrame();
  auto* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  auto alive = weak_factory_.GetWeakPtr();
  const std::string script = "(() => { const selectors = " +
      base::WriteJson(candidates).value_or("[]") + "; return (\n" +
      kSelectorValidationScript + "\n); })()";
  auto result = frame->ExecuteScriptInIsolatedWorldAndReturnValue(
      ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING,
      blink::WebScriptSource(blink::WebString::FromUtf8(script)),
      blink::BackForwardCacheAware::kAllow);
  if (!alive || result.IsEmpty())
    return;
  auto context = frame->GetScriptContextFromWorldId(
      isolate, ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING);
  if (context.IsEmpty())
    return;
  v8::Context::Scope scope(context);
  std::vector<std::string> valid;
  if (!gin::ConvertFromV8(isolate, result, &valid))
    return;
  auto document = frame->GetDocument();
  for (const auto& update : styles_.Add(valid)) {
    if (update.index < style_keys_.size()) {
      document.RemoveInsertedStyleSheet(style_keys_[update.index],
                                        blink::WebCssOrigin::kUser);
      style_keys_[update.index] =
          document.InsertStyleSheet(blink::WebString::FromUtf8(update.css),
                                    nullptr, blink::WebCssOrigin::kUser);
    } else {
      style_keys_.push_back(
          document.InsertStyleSheet(blink::WebString::FromUtf8(update.css),
                                    nullptr, blink::WebCssOrigin::kUser));
    }
  }
}

DocumentFilterAgent::DocumentFilterAgent(content::RenderFrame* frame)
    : RenderFrameObserver(frame), RenderFrameObserverTracker(frame) {}
DocumentFilterAgent::~DocumentFilterAgent() = default;
void DocumentFilterAgent::PrepareEngine() {
  if (base::FeatureList::IsEnabled(kYeeContentBlocking))
    (void)RendererDocumentEngine();
}
void DocumentFilterAgent::DidCreateNewDocument() {
  weak_factory_.InvalidateWeakPtrs();
  applied_ = false;
  styles_.Reset();
  style_keys_.clear();
  procedural_style_keys_.clear();
  procedural_style_bytes_ = 0;
  InitializeIsolatedWorld();
}
void DocumentFilterAgent::OnDestruct() {
  delete this;
}
bool DocumentFilterAgent::ApplyAtDocumentStart(content::RenderFrame* frame,
                                               bool initial_empty_document) {
  if (auto* agent = Get(frame)) {
    auto alive = initial_empty_document ? agent->lifetime_factory_.GetWeakPtr()
                                        : agent->weak_factory_.GetWeakPtr();
    agent->Apply();
    return !!alive;
  }
  return true;
}
void DocumentFilterAgent::Apply() {
  if (applied_)
    return;
  applied_ = true;
  auto* frame = render_frame()->GetWebFrame();
  GURL url(frame->GetDocument().Url());
  if (url.IsAboutBlank() || url.IsAboutSrcdoc())
    url = url::Origin(frame->GetSecurityOrigin()).GetURL();
  if (!url.SchemeIsHTTPOrHTTPS() || !EnabledForFrame(frame))
    return;
  TRACE_EVENT0("loading", "Yee.ContentBlocking.ApplyDocumentRules");
  InitializeIsolatedWorld();
  auto* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  auto rules = RendererDocumentEngine().RulesForPage(url.spec());
  auto alive = weak_factory_.GetWeakPtr();
  InsertSelectors(rules.selectors);
  if (!alive)
    return;
  // Install the lossless ingress adapter before external scriptlets. Original
  // fetch pruners then see an already-clean body and avoid needless JSON
  // serialization that would round unrelated integers or change escapes.
  if (url.DomainIs("youtube.com") || url.DomainIs("youtube-nocookie.com") ||
      url.DomainIs("youtubekids.com")) {
    const std::string youtube = "(() => { const yeeDocumentUrl = " +
        base::WriteJson(base::Value(url.spec())).value_or("\"\"") + ";" + kYouTubeScript + "})();";
    frame->ExecuteScript(
        blink::WebScriptSource(blink::WebString::FromUtf8(youtube)));
    if (!alive)
      return;
  }
  if (!rules.script.empty()) {
    frame->ExecuteScript(
        blink::WebScriptSource(blink::WebString::FromUtf8(rules.script)));
    if (!alive)
      return;
  }
  if (!rules.generic_hide && rules.procedural_actions.empty())
    return;
  frame->ExecuteScriptInIsolatedWorld(
      ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING,
      blink::WebScriptSource(blink::WebString::FromUtf8("void 0")),
      blink::BackForwardCacheAware::kAllow);
  if (!alive)
    return;
  auto context = frame->GetScriptContextFromWorldId(
      isolate, ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING);
  if (context.IsEmpty())
    return;
  {
    v8::MicrotasksScope microtasks_scope(
        isolate, context->GetMicrotaskQueue(),
        v8::MicrotasksScope::kDoNotRunMicrotasks);
    v8::Context::Scope scope(context);
    for (const auto& binding : {
             std::pair{"__yeeApplyGeneric", ApplyGeneric},
             std::pair{"__yeeInsertProceduralStyle", InsertProceduralStyle},
             std::pair{"__yeeProceduralEnabled", ProceduralEnabled}}) {
      v8::Local<v8::Function> function;
      if (!v8::Function::New(context, binding.second).ToLocal(&function) ||
          !context->Global()->Set(context, gin::StringToV8(isolate, binding.first),
                                 function).FromMaybe(false))
        return;
    }
  }
  if (!rules.procedural_actions.empty()) {
    base::ListValue actions;
    for (const auto& action : rules.procedural_actions)
      actions.Append(action);
    const std::string script = "(() => { const proceduralActions = " +
        base::WriteJson(actions).value_or("[]") + ";" +
        kProceduralCosmeticScript + "})();";
    frame->ExecuteScriptInIsolatedWorld(
        ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING,
        blink::WebScriptSource(blink::WebString::FromUtf8(script)),
        blink::BackForwardCacheAware::kAllow);
    if (!alive)
      return;
  }
  if (!rules.generic_hide)
    return;
  base::ListValue exceptions;
  for (const auto& selector : rules.exceptions)
    exceptions.Append(selector);
  const std::string script = "(() => { const exceptions = " +
                             base::WriteJson(exceptions).value_or("[]") + ";" +
                             kGenericCosmeticScript + "})();";
  frame->ExecuteScriptInIsolatedWorld(
      ISOLATED_WORLD_ID_YEE_CONTENT_BLOCKING,
      blink::WebScriptSource(blink::WebString::FromUtf8(script)),
      blink::BackForwardCacheAware::kAllow);
}
}  // namespace yee::content_blocking
