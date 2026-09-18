#!/usr/bin/env node

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const header = fs.readFileSync(
    path.join(here, '..', '..', 'browser/ui/agent_bridge_script.h'),
    'utf8');
const match = header.match(/R"JS\(\n?([\s\S]*?)\n?\)JS"/);
assert(match, 'fixed bridge script raw string not found');
const script = match[1];

class FakeElement {
  constructor({tag = 'button', type = '', attrs = {}, text = '', value = '',
                rect = {x: 10, y: 10, width: 80, height: 24}} = {}) {
    this.tagName = tag.toUpperCase();
    this.type = type;
    this.attrs = {...attrs};
    this.innerText = text;
    this.value = value;
    this.rect = rect;
    this.labels = [];
    this.isConnected = true;
    this.disabled = false;
    this.checked = false;
    this.readOnly = false;
    this.clicked = false;
  }

  getAttribute(name) { return this.attrs[name] ?? null; }
  hasAttribute(name) { return Object.hasOwn(this.attrs, name); }

  matches(selector) {
    if (selector.includes(','))
      return selector.split(',').some(part => this.matches(part.trim()));
    if (selector === 'button') return this.tagName === 'BUTTON';
    if (/^h[1-6]$/.test(selector)) return this.tagName === selector.toUpperCase();
    if (selector === 'textarea') return this.tagName === 'TEXTAREA';
    if (selector === 'input') return this.tagName === 'INPUT';
    if (selector === 'input[type=checkbox]') return this.tagName === 'INPUT' && this.type === 'checkbox';
    if (selector === 'input[type=radio]') return this.tagName === 'INPUT' && this.type === 'radio';
    if (selector === 'input[type=checkbox],input[type=radio]')
      return this.tagName === 'INPUT' && (this.type === 'checkbox' || this.type === 'radio');
    if (selector.startsWith('input:not('))
      return this.tagName === 'INPUT' && !['button', 'submit', 'reset'].includes(this.type);
    if (selector === 'button,input[type=button],input[type=submit],input[type=reset]')
      return this.tagName === 'BUTTON' ||
          (this.tagName === 'INPUT' && ['button', 'submit', 'reset'].includes(this.type));
    if (selector === 'a[href]') return this.tagName === 'A' && !!this.getAttribute('href');
    if (selector === 'select') return this.tagName === 'SELECT';
    if (selector === 'h1,h2,h3,h4,h5,h6') return /^H[1-6]$/.test(this.tagName);
    if (selector === '[role]') return this.getAttribute('role') !== null;
    return false;
  }

  getBoundingClientRect() {
    const {x, y, width, height} = this.rect;
    return {x, y, width, height, left: x, top: y,
      right: x + width, bottom: y + height};
  }
  contains(element) { return element === this; }
  click() { this.clicked = true; }
  dispatchEvent() {}
}

class FakeInput extends FakeElement {
  get value() { return this._value ?? ''; }
  set value(value) { this._value = value; }
}
Object.defineProperty(FakeInput.prototype, 'value', {
  configurable: true, enumerable: true,
  get() { return this._value ?? ''; },
  set(value) { this._value = value; },
});
class FakeTextArea extends FakeElement {}

function makeContext(elements) {
  const document = {
    activeElement: elements[0] ?? null,
    hit: elements[0] ?? null,
    querySelectorAll() { return elements; },
    elementFromPoint() { return this.hit; },
  };
  const context = {
    document,
    TextEncoder,
    Date: {now: () => context.wallNow ?? 1000},
    performance: {now: () => context.monoNow ?? 0},
    innerHeight: 800,
    innerWidth: 1000,
    devicePixelRatio: 2,
    getComputedStyle() { return {visibility: 'visible', display: 'block', opacity: '1'}; },
    HTMLInputElement: FakeInput,
    HTMLTextAreaElement: FakeTextArea,
    Event: class Event {},
  };
  context.globalThis = context;
  vm.createContext(context);
  return context;
}

function run(context, request) {
  context.request = JSON.parse(JSON.stringify({expires_unix_ms: 121000, ...request}));
  return vm.runInContext(`(function() { ${script}\n })()`, context);
}

function observe(context) {
  return run(context, {command: 'observe', document: 'doc-1'});
}

test('ordinary observations disclose unread page extent without exposing offscreen rows', () => {
  const context = makeContext([
    new FakeElement({tag:'p',text:'All arrivals delivered'}),
    new FakeElement({tag:'p',text:'Last urgent ticket',rect:{x:10,y:1200,width:80,height:24}}),
  ]);
  context.document.scrollingElement = {scrollHeight:2200};
  context.scrollY = 0;
  const first = observe(context);
  assert.equal(first.truncated, false);
  assert.deepEqual(Array.from(first.nodes, n => n.name), ['All arrivals delivered']);
  assert.equal(first.scroll.max_y, 1400);
  assert.equal(first.scroll.can_scroll_down, true);
  assert.equal(first.scroll.can_scroll_up, false);
  context.scrollY = 1400;
  const last = observe(context);
  assert.equal(last.scroll.can_scroll_down, false);
  assert.equal(last.scroll.can_scroll_up, true);
});

test('visible table caption and column headers precede data cells', () => {
  const elements = [
    new FakeElement({tag:'caption',text:'Delivered product prices'}),
    new FakeElement({tag:'th',text:'Unit price KRW'}),
    new FakeElement({tag:'th',text:'Shipping KRW'}),
    new FakeElement({tag:'td',text:'61000'}),
    new FakeElement({tag:'td',text:'1000'}),
    new FakeElement({tag:'th',text:'Offscreen header',rect:{x:10,y:900,width:80,height:24}}),
  ];
  const context = makeContext(elements);
  // Respect tag selection here: the older all-elements fake concealed the
  // production omission of TH/CAPTION from querySelectorAll.
  context.document.querySelectorAll = selector => elements.filter(
    e => selector.split(',').includes(e.tagName.toLowerCase()));
  const result = observe(context);
  assert.deepEqual(Array.from(result.nodes, n => n.name),
    ['Delivered product prices','Unit price KRW','Shipping KRW','61000','1000']);
  assert.equal(result.truncated, false);
});

function batchFixture() {
  const field = new FakeInput({tag:'input',type:'text',attrs:{'aria-label':'Name'}});
  const button = new FakeElement({text:'Save locally',rect:{x:100,y:10,width:80,height:24}});
  const context = makeContext([field,button]);
  context.document.elementFromPoint = x => x < 100 ? field : button;
  const snapshot = observe(context);
  const actions = [{command:'fill',ref:snapshot.nodes[0].ref,value:'Cedar'},
                   {command:'click',ref:snapshot.nodes[1].ref}];
  return {field,button,context,actions};
}

test('batch returns one final observation and completed count', () => {
  const {field,button,context,actions} = batchFixture();
  const result = run(context,{command:'batch',document:'doc-1',actions});
  assert.equal(result.error,undefined);
  assert.equal(result.completed,2);
  assert.equal(field.value,'Cedar');
  assert.equal(button.clicked,true);
  assert.equal(result.nodes[0].value,'Cedar');
});

test('invalid or stale later batch target prevents every mutation', () => {
  for (const change of [
    f => f.button.disabled = true,
    f => f.button.innerText = 'Changed',
    f => f.button.isConnected = false,
    f => f.actions[1].ref = 'other_1',
    f => f.actions.reverse(),
    f => f.actions.push({...f.actions[0]}),
    f => f.actions[1].command = 'navigate',
    f => f.field.type = 'password',
  ]) {
    const f = batchFixture();
    change(f);
    const result = run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    assert.ok(result.error);
    assert.equal(result.completed,0);
    assert.equal(f.field.value,'');
    assert.equal(f.button.clicked,false);
  }
});

test('batch rechecks original later targets after input handlers, reports partial result', () => {
  for (const change of [b => b.innerText = 'Changed', b => b.disabled = true,
                        b => b.isConnected = false]) {
    const f = batchFixture();
    f.field.dispatchEvent = () => change(f.button);
    const result = run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    assert.ok(result.error);
    assert.equal(result.completed,1);
    assert.equal(f.field.value,'Cedar');
    assert.equal(f.button.clicked,false);
  }
});

test('fill completion requires the exact value after synchronous handlers', () => {
  for (const change of [
    f => { f.field.dispatchEvent = () => { f.field.value = 'normalized'; }; },
    f => { f.field.dispatchEvent = () => { f.field.isConnected = false; }; },
  ]) {
    const f = batchFixture();
    change(f);
    const result = run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    assert.equal(result.error,'field_value_not_applied');
    assert.equal(result.completed,0);
    assert.equal(result.partial_effect_possible,true);
    assert.equal(f.button.clicked,false);
  }
});

test('batch exceptions do not claim rollback or execute the next action', () => {
  const f = batchFixture();
  f.field.dispatchEvent = () => { throw Error('handler'); };
  const result = run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
  assert.equal(result.error,'batch_action_exception');
  assert.equal(result.completed,0);
  assert.equal(result.partial_effect_possible,true);
  assert.equal(f.field.value,'Cedar');
  assert.equal(f.button.clicked,false);
});

test('observation assigns stable refs and redacts secret fields', () => {
  const button = new FakeElement({text: 'Continue'});
  const password = new FakeInput({tag: 'input', type: 'password', attrs: {name: 'password'}, value: 'hunter2'});
  const context = makeContext([button, password]);
  const first = observe(context);
  const second = observe(context);

  assert.equal(first.nodes[0].ref, second.nodes[0].ref);
  assert.equal(first.nodes[1].ref, second.nodes[1].ref);
  assert.equal(first.nodes[1].secret, true);
  assert.equal(first.nodes[1].value, '');
  assert.equal(first.viewport.width, 1000);
  assert.equal(first.viewport.height, 800);
  assert.equal(first.viewport.deviceScaleFactor, 2);
});

test('action snapshots report the current renderer viewport after resize', () => {
  const input = new FakeInput({tag: 'input', type: 'text', attrs: {'aria-label': 'Name'}});
  const context = makeContext([input]);
  const first = observe(context);
  context.innerWidth = 698;
  context.innerHeight = 900;
  context.devicePixelRatio = 1.5;
  const filled = run(context, {command: 'fill', document: 'doc-1',
    ref: first.nodes[0].ref, value: 'Cedar'});
  assert.equal(filled.error, undefined);
  assert.equal(filled.viewport.width, 698);
  assert.equal(filled.viewport.height, 900);
  assert.equal(filled.viewport.deviceScaleFactor, 1.5);
  assert.equal(input.value, 'Cedar');
});

test('read rejects unknown, stale, hidden, and obscured targets', () => {
  const button = new FakeElement({text: 'Continue'});
  const other = new FakeElement({text: 'Other'});
  const context = makeContext([button, other]);
  const snapshot = observe(context);
  const ref = snapshot.nodes[0].ref;

  assert.equal(run(context, {command: 'read', document: 'doc-1', ref: 'missing'}).error,
               'unknown_reference');
  button.innerText = 'Changed';
  assert.equal(run(context, {command: 'read', document: 'doc-1', ref}).error,
               'stale_target');
  button.innerText = 'Continue';
  button.rect = {x: 0, y: 0, width: 0, height: 0};
  assert.equal(run(context, {command: 'read', document: 'doc-1', ref}).error,
               'not_visible');
  button.rect = {x: 10, y: 10, width: 80, height: 24};
  context.document.hit = other;
  assert.equal(run(context, {command: 'click', document: 'doc-1', ref}).error,
               'target_obscured');
});

test('read returns current long field value and does not execute JSON data', () => {
  const field = new FakeInput({tag: 'input', type: 'text', attrs: {name: 'query'}, value: 'initial'});
  const context = makeContext([field]);
  const ref = observe(context).nodes[0].ref;
  field.value = 'x'.repeat(16001);
  const long = run(context, {command: 'read', document: 'doc-1', ref});
  assert.equal(long.text.length, 16000);
  assert.equal(long.field_truncated, true);

  const injected = '"};globalThis.pwned=true;//';
  const result = run(context, {command: 'fill', document: 'doc-1', ref, value: injected});
  assert.equal(result.nodes[0].value, injected);
  assert.equal(field.value, injected);
  assert.equal(context.pwned, undefined);
});

test('observation bounds the returned node list', () => {
  const elements = Array.from({length: 450}, (_, i) =>
      new FakeElement({text: `Button ${i}`}));
  const result = observe(makeContext(elements));
  assert.equal(result.nodes.length, 400);
  assert.equal(result.truncated, true);
});

test('labeled prose retains its body and body-only changes', () => {
  const paragraph = new FakeElement({tag: 'p', attrs: {'aria-label': 'Article'},
    text: 'Long text. '.repeat(30) + 'TAIL_MARKER'});
  const context = makeContext([paragraph]);
  const first = observe(context).nodes[0];
  assert.equal(first.name, 'Article');
  assert.equal(first.value, paragraph.innerText);
  paragraph.innerText += ' changed';
  const second = observe(context).nodes[0];
  assert.equal(second.ref, first.ref);
  assert.notEqual(second.value, first.value);
});

test('static secret bodies are omitted and input whitespace is preserved', () => {
  const secret = new FakeElement({tag: 'p', attrs: {'aria-label': 'Secret'}, text: 'private body'});
  const field = new FakeInput({tag: 'input', type: 'text', value: '  query  '});
  const result = observe(makeContext([secret, field]));
  assert.equal(result.nodes[0].value, '');
  assert.equal(result.nodes[0].name, '<redacted>');
  assert.equal(result.nodes[1].value, '  query  ');
});

test('replaced DOM nodes do not inherit old numeric references', () => {
  const oldButton = new FakeElement({text: 'Save'});
  const elements = [oldButton];
  const context = makeContext(elements);
  const oldRef = observe(context).nodes[0].ref;
  const replacement = new FakeElement({text: 'Save'});
  oldButton.isConnected = false;
  elements[0] = replacement;
  context.document.hit = replacement;
  const newRef = observe(context).nodes[0].ref;
  assert.notEqual(oldRef, newRef);
  assert.equal(run(context, {command: 'click', document: 'doc-1', ref: oldRef}).error,
               'unknown_reference');
  assert.equal(replacement.clicked, false);
});

test('new document invalidates old refs even when numeric suffix is reused', () => {
  const button = new FakeElement({text: 'Save'});
  const context = makeContext([button]);
  const oldRef = observe(context).nodes[0].ref;
  const newRef = run(context, {command: 'observe', document: 'doc-2'}).nodes[0].ref;
  assert.equal(oldRef.split('_').at(-1), newRef.split('_').at(-1));
  assert.notEqual(oldRef, newRef);
  assert.equal(run(context, {command: 'click', document: 'doc-2', ref: oldRef}).error,
               'unknown_reference');
  assert.equal(button.clicked, false);
});


test('queued expired and missing-deadline actions do not touch DOM', () => {
  for (const expires_unix_ms of [999, 1000, null]) {
    const {context, field, button, actions} = batchFixture();
    const result = run(context, {command:'batch', document:'doc-1', actions, expires_unix_ms});
    assert.equal(result.error, 'request_expired');
    assert.equal(result.completed, 0);
    assert.equal(field.value, '');
    assert.equal(button.clicked, false);
  }
});

test('batch stops after event handler consumes deadline, including wall rollback', () => {
  for (const rollback of [false, true]) {
    const {context, field, button, actions} = batchFixture();
    field.dispatchEvent = () => {
      context.wallNow = rollback ? 500 : 121000;
      context.monoNow = 120000;
    };
    const result = run(context, {command:'batch', document:'doc-1', actions});
    assert.equal(result.error, 'request_expired');
    assert.equal(result.completed, 1);
    assert.equal(field.value, 'Cedar');
    assert.equal(button.clicked, false);
  }
});


test('bounded scroll reveals lower content and reports document end', () => {
  const element=new FakeElement({tag:'p',text:'Tail exception',rect:{x:10,y:900,width:80,height:24}});
  const context=makeContext([element]);context.scrollY=0;context.document.scrollingElement={scrollHeight:1600};
  context.scrollBy=({top,behavior})=>{assert.equal(behavior,'instant');const before=context.scrollY;context.scrollY=Math.max(0,Math.min(800,before+top));element.rect.y-=context.scrollY-before;};
  assert.equal(observe(context).nodes.length,0);
  const response=run(context,{command:'scroll',document:'doc-1',direction:'down',pages:1});
  assert.equal(response.nodes[0].name,'Tail exception');assert.equal(response.scroll.y,640);assert.equal(response.scroll.moved,true);
  run(context,{command:'scroll',document:'doc-1',direction:'down',pages:1});
  const end=run(context,{command:'scroll',document:'doc-1',direction:'down',pages:1});
  assert.equal(end.scroll.y,800);assert.equal(end.scroll.max_y,800);assert.equal(end.scroll.moved,false);
});

test('invalid or expired scroll never changes viewport', () => {
  const context=makeContext([]);let calls=0;context.scrollY=0;context.scrollBy=()=>calls++;
  for(const args of [{direction:'left',pages:1},{direction:'down',pages:0},{direction:'down',pages:4},{direction:'down',pages:true},{direction:'down',pages:1,expires_unix_ms:999}]){
    const result=run(context,{command:'scroll',document:'doc-1',...args});assert.ok(result.error);
  }
  assert.equal(calls,0);
});

test('approval preflight preserves targets and emits no input or click', () => {
  for (const command of ['fill', 'click', 'batch']) {
    const {field,button,context,actions} = batchFixture();
    let events = 0;
    field.dispatchEvent = () => { events++; };
    const targets = context.__yeeAgentBridgePrototypeV1.targets;
    const request = command === 'batch' ? {command, actions} : actions[command === 'fill' ? 0 : 1];
    const result = run(context,{...request,document:'doc-1',preflight:true});
    assert.equal(result.preflight_valid,true);
    assert.equal(result.nodes,undefined);
    assert.equal(context.__yeeAgentBridgePrototypeV1.targets,targets);
    assert.equal(field.value,'');
    assert.equal(button.clicked,false);
    assert.equal(events,0);
  }
});

test('approval preflight rejects invalid targets, including the last batch target', () => {
  for (const command of ['click','batch']) {
    for (const [invalidate,error] of [
      [b => { b.isConnected = false; },'not_visible'],
      [b => { b.innerText = 'Different action'; },'stale_target'],
      [b => { b.disabled = true; },'disabled'],
      [(b,c) => { c.document.elementFromPoint = () => null; },'target_obscured'],
    ]) {
      const {field,button,context,actions} = batchFixture();
      invalidate(button,context);
      const request = command === 'batch' ? {command,actions} : actions[1];
      assert.equal(run(context,{...request,document:'doc-1',preflight:true}).error,error);
      assert.equal(field.value,'');
      assert.equal(button.clicked,false);
    }
  }
});

test('successful preflight does not authorize a target changed during approval', () => {
  const {field,button,context,actions} = batchFixture();
  const request = {command:'batch',document:'doc-1',actions};
  assert.equal(run(context,{...request,preflight:true}).preflight_valid,true);
  button.isConnected = false;
  const result = run(context,request);
  assert.equal(result.error,'not_visible');
  assert.equal(result.completed,0);
  assert.equal(field.value,'');
  assert.equal(button.clicked,false);
});

function checkFixture() {
  const field = new FakeInput({tag:'input',type:'text',attrs:{'aria-label':'Search'}});
  const checkbox = new FakeInput({tag:'input',type:'checkbox',attrs:{'aria-label':'In stock'},rect:{x:100,y:10,width:80,height:24}});
  const button = new FakeElement({text:'Apply',rect:{x:200,y:10,width:80,height:24}});
  const context = makeContext([field,checkbox,button]);
  context.document.elementFromPoint = x => x < 100 ? field : x < 200 ? checkbox : button;
  let clicks = 0;
  checkbox.click = () => {clicks++; checkbox.checked = !checkbox.checked;};
  const snapshot = observe(context);
  const actions = [{command:'fill',ref:snapshot.nodes[0].ref,value:'SSD'},
    {command:'check',ref:snapshot.nodes[1].ref,checked:true},
    {command:'click',ref:snapshot.nodes[2].ref}];
  return {field,checkbox,button,context,actions,clicks:()=>clicks};
}

test('explicit checkbox state batches with fills and submit, including no-op/uncheck', () => {
  for (const initial of [false,true]) for (const desired of [false,true]) {
    const f = checkFixture();f.checkbox.checked=initial;f.actions[1].checked=desired;
    const result=run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    assert.equal(result.error,undefined);assert.equal(result.completed,3);
    assert.equal(f.field.value,'SSD');assert.equal(f.checkbox.checked,desired);
    assert.equal(f.clicks(),initial===desired?0:1);assert.equal(f.button.clicked,true);
  }
});

test('unsupported checkbox plans preflight every target without side effects', () => {
  for (const change of [f=>f.actions[1].checked='true', f=>f.actions[1].checked=1,
      f=>f.actions[1].extra=true, f=>f.checkbox.type='radio',
      f=>f.checkbox.disabled=true, f=>f.checkbox.isConnected=false,
      f=>{f.checkbox.type='text';f.checkbox.attrs.role='checkbox';},
      f=>{f.actions[1]={command:'click',ref:f.actions[1].ref};}]) {
    const f=checkFixture();change(f);
    const result=run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    assert.ok(result.error);assert.equal(result.completed,0);
    assert.equal(f.field.value,'');assert.equal(f.clicks(),0);assert.equal(f.button.clicked,false);
  }
});

test('checkbox handlers cannot cancel/replace then continue to submit', () => {
  for (const change of [f=>{f.checkbox.click=()=>{};},
      f=>{f.checkbox.click=()=>{f.checkbox.checked=true;f.checkbox.isConnected=false;};},
      f=>{f.checkbox.click=()=>{f.checkbox.checked=true;f.checkbox.type='radio';};}]) {
    const f=checkFixture();change(f);
    const result=run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    assert.equal(result.error,'checkbox_state_not_applied');assert.equal(result.completed,1);
    assert.equal(result.partial_effect_possible,true);assert.equal(f.button.clicked,false);
  }
  const f=checkFixture();f.checkbox.click=()=>{f.checkbox.checked=true;f.button.isConnected=false;};
  const result=run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
  assert.equal(result.error,'not_visible');assert.equal(result.completed,2);assert.equal(f.button.clicked,false);
});

test('observed links include resolved destinations without leaking secret targets', () => {
  const link=new FakeElement({tag:'a',attrs:{href:'../policy?q=refund'},text:'Policy'});
  link.href='https://example.test/policy?q=refund';
  const hidden=new FakeElement({tag:'a',attrs:{href:'/hidden'},text:'Hidden',rect:{x:10,y:900,width:80,height:24}});
  const privateLink=new FakeElement({tag:'a',attrs:{href:'/secret', 'aria-label':'Token'},text:'Access'});
  const roleLink=new FakeElement({attrs:{role:'link'},text:'Script controlled'});
  const context=makeContext([link,hidden,privateLink,roleLink]);
  const first=observe(context);
  assert.equal(first.nodes[0].href,link.href);
  assert.equal(first.nodes.some(n=>n.name==='Hidden'),false);
  assert.equal(first.nodes[1].secret,true);assert.equal('href' in first.nodes[1],false);
  assert.equal('href' in first.nodes[2],false);
  const oldRef=first.nodes[0].ref;
  link.attrs.href='/new-policy';link.href='https://example.test/new-policy';
  const stale=run(context,{command:'click',document:'doc-1',ref:oldRef});
  assert.equal(stale.error,'stale_target');assert.equal(link.clicked,false);
  assert.equal(observe(context).nodes[0].href,link.href);
});

test('batch fill limit is 4000 UTF-8 bytes without content truncation', () => {
  for (const value of ['x'.repeat(4000),'가'.repeat(1333)+'x','😀'.repeat(1000),
                       'x'.repeat(4001),'가'.repeat(1334),'😀'.repeat(1001)]) {
    const f=batchFixture();f.actions[0].value=value;
    const result=run(f.context,{command:'batch',document:'doc-1',actions:f.actions});
    const valid=new TextEncoder().encode(value).length<=4000;
    assert.equal(result.error,valid?undefined:'invalid_batch');
    assert.equal(f.field.value,valid?value:'');assert.equal(f.button.clicked,valid);
  }
});
