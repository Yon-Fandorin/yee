"""실제 창 없이 내부 페이지 JS 대기의 이벤트 등록·해제와 shadow root 수명을 검사한다."""

import json
from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / 'browser/ui/internal_page_test_support.h'


@unittest.skipUnless(shutil.which('node'), 'Node.js가 필요하다')
class InternalPageWaitTest(unittest.TestCase):
    def test_shadow_roots_resource_events_and_cleanup_without_browser(self):
        prefix, suffix = re.findall(r'R"JS\((.*?)\)JS"', HELPER.read_text(), re.S)
        result = subprocess.run(['node', '--input-type=module'], input=r'''
import assert from 'node:assert/strict';
import vm from 'node:vm';

function harness(check = state => state.ready) {
  const observers = new Set(), targets = [], timers = new Map();
  const state = {ready:false, reads:0, timerCalls:0};
  class Target {
    constructor() { this.listeners = new Map(); targets.push(this); }
    addEventListener(event, callback) {
      if (!this.listeners.has(event)) this.listeners.set(event, new Set());
      this.listeners.get(event).add(callback);
    }
    removeEventListener(event, callback) { this.listeners.get(event)?.delete(callback); }
    emit(event) { for (const callback of this.listeners.get(event) || []) callback(); }
  }
  class Root extends Target {
    constructor() { super(); this.children = []; this.parent = null; }
    querySelectorAll() {
      return this.children.flatMap(child => [child, ...child.querySelectorAll('*')]);
    }
    append(child) { child.parent = this; this.children.push(child); this.mutate(); }
    mutate() {
      for (let node = this; node; node = node.parent)
        for (const observer of observers)
          if (observer.roots.has(node)) observer.callback();
    }
  }
  class Element extends Root {
    attachShadow() { this.shadowRoot = new Root(); return this.shadowRoot; }
  }
  class MutationObserver {
    constructor(callback) { this.callback = callback; this.roots = new Set(); observers.add(this); }
    observe(root) { this.roots.add(root); }
    disconnect() { this.roots.clear(); }
  }
  const document = new Root(), window = new Target();
  const native = new Target();
  native.addListener = callback => native.addEventListener('native', callback);
  native.removeListener = callback => native.removeEventListener('native', callback);
  window.navigation = new Target();
  const originalAttach = Element.prototype.attachShadow;
  const context = vm.createContext({document, window, Element, MutationObserver, queueMicrotask,
    chrome:{developerPrivate:{onItemStateChanged:native}},
    check:() => { ++state.reads; return check(state); },
    setTimeout:(callback, ms) => {
      assert.equal(ms, 5000); ++state.timerCalls; timers.set(1, callback); return 1;
    },
    clearTimeout:id => timers.delete(id),
  });
  return {document, window, native, state, Element, timers,
    start:(condition = 'check()') => vm.runInContext(PREFIX + condition + SUFFIX, context),
    observed:root => [...observers].some(observer => observer.roots.has(root)),
    clean:() => {
      assert.equal(Element.prototype.attachShadow, originalAttach);
      assert.equal(timers.size, 0);
      assert.equal(state.timerCalls, 1);
      assert([...observers].every(observer => observer.roots.size === 0));
      assert(targets.every(target => [...target.listeners.values()].every(set => set.size === 0)));
    },
  };
}
// JS Promise와 MutationObserver의 전달을 fixture에서 배달한다. 완료 조건을 반복 조회하지 않는다.
async function flush() { for (let i = 0; i < 12; ++i) await Promise.resolve(); }

// 기존의 중첩 root 내부 변경과 DOM mutation이 없는 이미지 load 완료.
{
  const h = harness();
  const outer = new h.Element(), inner = new h.Element();
  h.document.append(outer);
  outer.attachShadow().append(inner);
  const root = inner.attachShadow();
  const promise = h.start(); await flush();
  assert(h.observed(root));
  const reads = h.state.reads; await flush(); assert.equal(h.state.reads, reads);
  root.append(new h.Element()); await flush(); assert(h.state.reads > reads);
  h.state.ready = true; root.emit('load');
  assert.equal(await promise, true); h.clean();
}
// 초기 inspect 뒤 삽입된 host가 이미 가진 root도 다시 찾아 관측한다.
{
  const h = harness(), promise = h.start(); await flush();
  const host = new h.Element(), root = host.attachShadow();
  h.document.append(host); await flush(); assert(h.observed(root));
  h.state.ready = true; root.mutate();
  assert.equal(await promise, true); h.clean();
}
// 기존 host에 attachShadow만 호출해도 등록되며, error 이벤트도 root에서 받는다.
{
  const h = harness(), host = new h.Element(); h.document.append(host);
  const promise = h.start(); await flush();
  const root = host.attachShadow(); await flush(); assert(h.observed(root));
  h.state.ready = true; root.emit('error');
  assert.equal(await promise, true); h.clean();
}
// light DOM의 이미지 load와 native 완료 이벤트도 계속 동작한다.
for (const event of ['load', 'native', 'currententrychange']) {
  const h = harness(), promise = h.start(); await flush();
  h.state.ready = true;
  (event === 'native' ? h.native : event === 'load' ? h.document : h.window.navigation).emit(event);
  assert.equal(await promise, true); h.clean();
}
// await 표현식이 pending인 동안 온 변경은 완료 뒤 한 번 다시 읽는다.
{
  let release, calls = 0;
  const h = harness(state => ++calls === 1 ? new Promise(resolve => { release = resolve; }) : state.ready);
  const promise = h.start('(await check())');
  h.state.ready = true; h.document.mutate(); release(false);
  assert.equal(await promise, true); assert.equal(calls, 2); h.clean();
}
// 실패 deadline·predicate 예외·즉시 성공 경로에서도 모두 해제한다.
{
  const h = harness(), promise = h.start(); await flush(); h.timers.get(1)();
  assert.equal(await promise, false); h.clean();
}
{
  const h = harness(() => { throw new Error('predicate failure'); });
  await assert.rejects(h.start('(await check())'), /predicate failure/); h.clean();
}
{
  const h = harness(() => true); assert.equal(await h.start(), true); h.clean();
}
'''.replace('PREFIX', json.dumps(prefix)).replace('SUFFIX', json.dumps(suffix)),
                                text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
