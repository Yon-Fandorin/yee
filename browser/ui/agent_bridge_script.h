#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_SCRIPT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_SCRIPT_H_
namespace yee {
// Fixed isolated-world code. The caller injects JSON data as `request`, never
// executable client text. Main-document DOM semantics are a prototype AX
// fallback.
inline constexpr char kAgentBridgeScript[] = R"JS(
// The absolute client deadline includes mailbox and approval time. Use both
// epoch and monotonic elapsed time once this invocation starts. A queued script
// must reject expired work before touching the document; every batch action
// checks again after page event handlers from the preceding action return.
const startedWall = Date.now();
const startedMono = performance.now();
const budget = request.expires_unix_ms - startedWall;
const expired = () => !Number.isFinite(budget) || budget <= 0 ||
  Date.now() >= request.expires_unix_ms || performance.now() - startedMono >= budget;
if (expired()) return {error:'request_expired',completed:0};
const key = '__yeeAgentBridgePrototypeV1';
let state = globalThis[key];
if (!state || state.document !== request.document) {
  state = globalThis[key] = {document:request.document, ids:new WeakMap(),
    next:0, targets:new Map()};
}
const visible = e => {
  const r = e.getBoundingClientRect();
  const s = getComputedStyle(e);
  return e.isConnected && r.width > 0 && r.height > 0 &&
    r.bottom > 0 && r.right > 0 && r.top < innerHeight && r.left < innerWidth &&
    s.visibility !== 'hidden' && s.display !== 'none' && s.opacity !== '0';
};
const name = e => (e.getAttribute('aria-label') ||
  (e.labels ? [...e.labels].map(x => x.innerText).join(' ') : '') ||
  e.getAttribute('placeholder') || e.innerText || e.getAttribute('title') || '')
  .trim().slice(0,16384);
const secret = e => /password|token|secret|credit|card|cvv|cvc|otp|pin|ssn/i.test(
  [e.type,e.name,e.id,e.autocomplete,e.getAttribute('aria-label')].join(' '));
const fingerprint = e => [e.tagName,e.type,name(e),e.getAttribute('href'),
  e.getAttribute('formaction')].join('|');
const role = e => {
  if (e.matches('input[type=checkbox],input[type=radio]')) return 'checkbox';
  if (e.matches('input:not([type=button]):not([type=submit]):not([type=reset]),textarea')) return 'field';
  if (e.matches('button,input[type=button],input[type=submit],input[type=reset]')) return 'button';
  if (e.matches('a[href]')) return 'link';
  if (e.matches('select')) return 'combobox';
  if (e.matches('h1,h2,h3,h4,h5,h6')) return 'heading';
  return ({button:'button',link:'link',textbox:'field',checkbox:'checkbox',
    dialog:'dialog',alert:'alert',heading:'heading'})[e.getAttribute('role')] || 'text';
};
let scroll;
if (request.command === 'scroll') {
  if (!['up','down'].includes(request.direction) || !Number.isInteger(request.pages) || request.pages < 1 || request.pages > 3)
    return {error:'invalid_scroll'};
  if (expired()) return {error:'request_expired'};
  const before = scrollY;
  globalThis.scrollBy({top:(request.direction === 'down' ? 1 : -1)*innerHeight*0.8*request.pages,behavior:'instant'});
  scroll = {y:scrollY,max_y:Math.max(0,(document.scrollingElement?.scrollHeight || innerHeight)-innerHeight),moved:scrollY!==before};
}
let point = null;
if (request.command === 'read') {
  const target = state.targets.get(request.ref);
  if (!target) return {error:'unknown_reference'};
  const e = target.element;
  if (!visible(e)) return {error:'not_visible'};
  if (fingerprint(e) !== target.fingerprint) return {error:'stale_target'};
  if (secret(e)) return {error:'secret_field_forbidden'};
  const text = role(e) === 'field' || role(e) === 'combobox' ? String(e.value || '') : String(e.innerText || name(e));
  return {text:text.slice(0,16000),field_truncated:text.length >= 16000};
}
const act = (action, validateOnly = false) => {
  if (expired()) return {error:'request_expired'};
  const target = state.targets.get(action.ref);
  if (!target) return {error:'unknown_reference'};
  const e = target.element;
  if (!visible(e)) return {error:'not_visible'};
  if (fingerprint(e) !== target.fingerprint) return {error:'stale_target'};
  if (e.disabled || e.getAttribute('aria-disabled') === 'true') return {error:'disabled'};
  if (secret(e)) return {error:'secret_field_forbidden'};
  const r = e.getBoundingClientRect();
  const x = Math.max(0,Math.min(innerWidth-1,r.x+r.width/2));
  const y = Math.max(0,Math.min(innerHeight-1,r.y+r.height/2));
  const hit = document.elementFromPoint(x,y);
  if (!hit || !(hit === e || e.contains(hit))) return {error:'target_obscured'};
  if (action.command === 'check') {
    if (!(e instanceof HTMLInputElement) || e.type !== 'checkbox' ||
        typeof action.checked !== 'boolean') return {error:'unsupported_checkbox'};
    if (validateOnly || e.checked === action.checked) return;
    if (expired()) return {error:'request_expired'};
    point = {x,y};
    e.click();
    // A cancelled click or synchronous replacement is not a completed check.
    // It may already have invoked page handlers: never replay or continue.
    if (!e.isConnected || fingerprint(e) !== target.fingerprint ||
        e.checked !== action.checked)
      return {error:'checkbox_state_not_applied',partial_effect_possible:true};
  } else if (action.command === 'click') {
    if (!['button','link','checkbox'].includes(role(e))) return {error:'unsupported_target'};
    if (validateOnly) return;
    if (expired()) return {error:'request_expired'};
    point = {x,y};
    e.click();
  } else {
    if (!(e instanceof HTMLTextAreaElement) &&
        !(e instanceof HTMLInputElement && /^(text|search|email|url)$/.test(e.type)))
      return {error:'unsupported_field'};
    if (e.readOnly) return {error:'readonly'};
    if (validateOnly) return;
    if (expired()) return {error:'request_expired'};
    point = {x,y};
    const proto = e instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    Object.getOwnPropertyDescriptor(proto,'value').set.call(e,action.value);
    e.dispatchEvent(new Event('input',{bubbles:true}));
    e.dispatchEvent(new Event('change',{bubbles:true}));
    // Synchronous handlers may replace or normalize the field. Count a fill as
    // completed only when the exact requested value still exists afterwards.
    if (!e.isConnected || fingerprint(e) !== target.fingerprint ||
        String(e.value) !== action.value)
      return {error:'field_value_not_applied',partial_effect_possible:true};
  }
};
let completed;
if (request.command === 'batch') {
  const actions = request.actions;
  if (!Array.isArray(actions) || !actions.length || actions.length > 8)
    return {error:'invalid_batch',completed:0};
  const refs = new Set();
  for (let i=0; i<actions.length; i++) {
    const a = actions[i];
    if (!a || typeof a !== 'object' || typeof a.ref !== 'string' || refs.has(a.ref) ||
        !a.ref.startsWith(request.document + '_') ||
        !((a.command === 'fill' && typeof a.value === 'string' && new TextEncoder().encode(a.value).length <= 4000 && Object.keys(a).length === 3) ||
          (a.command === 'check' && typeof a.checked === 'boolean' && Object.keys(a).length === 3) ||
          (a.command === 'click' && i === actions.length-1 && Object.keys(a).length === 2)))
      return {error:'invalid_batch',completed:0};
    refs.add(a.ref);
    const t = state.targets.get(a.ref);
    if (!t || role(t.element) !== (a.command === 'fill' ? 'field' : a.command === 'check' ? 'checkbox' : 'button'))
      return {error:'invalid_batch_target',completed:0};
    const invalid = act(a, true);
    if (invalid) return {...invalid,completed:0};
  }
  if (request.preflight === true) return {preflight_valid:true};
  completed = 0;
  // Keep the original target map/fingerprints through every step. Input/change
  // handlers may mutate or replace a later target; recheck immediately before it.
  for (const action of actions) {
    try {
      const invalid = act(action);
      if (invalid) return {...invalid,completed};
      completed++;
    } catch (_) {
      return {error:'batch_action_exception',completed,partial_effect_possible:true};
    }
  }
} else if (request.command === 'click' || request.command === 'fill') {
  const invalid = act(request, request.preflight === true);
  if (invalid) return invalid;
  if (request.preflight === true) return {preflight_valid:true};
}
const nodes = [];
const targets = new Map();
const elements = document.querySelectorAll('button,a[href],input,textarea,select,h1,h2,h3,h4,h5,h6,p,label,[role],output,li,caption,th,td');
let truncated = elements.length > 3000;
for (let i=0; i<Math.min(elements.length,3000); i++) {
  const e = elements[i];
  if (!visible(e) || e.type === 'hidden') continue;
  const n = name(e), r = role(e);
  if (r === 'text' && !n) continue;
  if (nodes.length >= 400) { truncated = true; break; }
  let id = state.ids.get(e);
  if (!id) { id = request.document + '_' + (++state.next); state.ids.set(e,id); }
  const isSecret = secret(e);
  nodes.push({ref:id,role:r,name:isSecret && r === 'text' ? '<redacted>' : n,secret:isSecret,
    value:isSecret ? '' : (r === 'text' ? String(e.innerText || '').trim() : String(e.value || '')).slice(0,16384),
    ...(r === 'link' && !isSecret && e.hasAttribute('href') ?
      {href:String(e.href || e.getAttribute('href') || '').slice(0,16384)} : {}),
    enabled:!e.disabled && e.getAttribute('aria-disabled') !== 'true',
    checked:!!e.checked,focused:e === document.activeElement});
  targets.set(id,{element:e,fingerprint:fingerprint(e)});
}
state.targets = targets;
// A full snapshot still covers only the viewport. Always expose document
// extent so a completed update above the fold cannot imply all rows were read.
const y = globalThis.scrollY || 0;
const max_y = Math.max(0,(document.scrollingElement?.scrollHeight || innerHeight)-innerHeight);
scroll = {...(scroll || {}),y,max_y,can_scroll_up:y>0,can_scroll_down:y<max_y};
return {nodes,truncated,point,scroll,...(completed === undefined ? {} : {completed}),viewport:{width:innerWidth,height:innerHeight,deviceScaleFactor:devicePixelRatio}};
)JS";
}  // namespace yee
#endif
