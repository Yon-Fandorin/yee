#!/usr/bin/env node
// Queue/progress regression tests. This is not a substitute for Blink/app CSS tests.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const source = await fs.readFile(new URL('../../renderer/content_blocking/generic_cosmetic.js', import.meta.url), 'utf8');
function element(classes = [], id = '', children = []) {
  return {nodeType: 1, classList: classes, id, children, isConnected: true};
}
function harness(root, asynchronous = false) {
  const idle = [], batches = [];
  let observer, scans = 0, pending = 0;
  const document = {documentElement: root, createTreeWalker(node) {
    ++scans;
    const descendants = [];
    const visit = node => {for (const child of node.children) {descendants.push(child); visit(child);}};
    visit(node);
    let next = 0;
    return {nextNode: () => descendants[next++] ?? null};
  }};
  const context = vm.createContext({document, TextEncoder, NodeFilter: {SHOW_ELEMENT: 1},
    exceptions: ['.excepted'], requestIdleCallback: callback => {idle.push(callback); assert.ok(idle.length <= 1);},
    MutationObserver: class {constructor(callback) {observer = callback;} observe(target) {assert.equal(target, document);}},
    __yeeApplyGeneric(classes, ids, exceptions) {
      assert.ok(classes.length <= 256 && ids.length <= 256);
      assert.deepEqual(Array.from(exceptions), ['.excepted']);
      for (const name of [...classes, ...ids]) assert.ok(Buffer.byteLength(name) <= 512);
      batches.push({classes: Array.from(classes), ids: Array.from(ids)});
      if (asynchronous) {
        assert.equal(pending, 0, 'Only one worker batch may be pending');
        ++pending;
        return true;
      }
    }});
  vm.runInContext(source, context);
  return {document, batches, scans: () => scans, pending: () => pending,
    complete() {assert.equal(pending, 1); --pending; context.__yeeGenericComplete();},
    mutate: records => observer(records), drain() {
    let remaining = 10000;
    while (idle.length) {assert.ok(remaining-- > 0, 'Queue must make progress'); idle.shift()();}
  }};
}
const root = element();
const h = harness(root);
h.drain();
const added = Array.from({length: 600}, (_, i) => element([`class-${i}`], `id-${i}`));
root.children.push(...added);
h.mutate([{type: 'childList', addedNodes: added}]);
h.drain();
const classes = new Set(h.batches.flatMap(batch => batch.classes));
const ids = new Set(h.batches.flatMap(batch => batch.ids));
for (let i = 0; i < 600; ++i) {
  assert.ok(classes.has(`class-${i}`), `Queue overflow class ${i}`);
  assert.ok(ids.has(`id-${i}`), `Queue overflow id ${i}`);
}
const many = element(Array.from({length: 700}, (_, i) => `many-${i}`));
root.children.push(many);
h.mutate([{type: 'childList', addedNodes: [many]}]);
h.drain();
const all = new Set(h.batches.flatMap(batch => batch.classes));
for (let i = 0; i < 700; ++i) assert.ok(all.has(`many-${i}`), `Large class list ${i}`);
const scansBeforeAttribute = h.scans();
many.classList = ['attribute-ad', '한'.repeat(200), 'valid-ad'];
h.mutate([{type: 'attributes', target: many}]);
h.drain();
assert.equal(h.scans(), scansBeforeAttribute, 'Attribute mutation must not walk the subtree');
const final = new Set(h.batches.flatMap(batch => batch.classes));
assert.ok(final.has('attribute-ad') && final.has('valid-ad'), 'Oversized UTF-8 name does not discard its batch');
const noRoot = harness(null);
noRoot.document.documentElement = element(['late-root']);
noRoot.mutate([{type: 'childList', addedNodes: [noRoot.document.documentElement]}]);
noRoot.drain();
assert.ok(noRoot.batches.some(batch => batch.classes.includes('late-root')));
const asyncRoot = element([], '', [element(Array.from({length: 700}, (_, i) => `async-${i}`))]);
const asynchronous = harness(asyncRoot, true);
asynchronous.drain();
assert.equal(asynchronous.batches.length, 1);
assert.equal(asynchronous.pending(), 1);
const duringReply = Array.from({length: 600}, (_, i) => element([`late-${i}`], `late-id-${i}`));
asyncRoot.children.push(...duringReply);
asynchronous.mutate([{type: 'childList', addedNodes: duringReply}]);
asynchronous.drain();
assert.equal(asynchronous.batches.length, 1, 'Mutations wait for the active native reply');
let replies = 100;
while (asynchronous.pending()) {
  assert.ok(replies-- > 0, 'Replies must drain the queued mutations');
  asynchronous.complete(); asynchronous.drain();
}
const asyncClasses = new Set(asynchronous.batches.flatMap(batch => batch.classes));
const asyncIds = new Set(asynchronous.batches.flatMap(batch => batch.ids));
for (let i = 0; i < 700; ++i) assert.ok(asyncClasses.has(`async-${i}`));
for (let i = 0; i < 600; ++i) {
  assert.ok(asyncClasses.has(`late-${i}`)); assert.ok(asyncIds.has(`late-id-${i}`));
}
console.log('Generic collector: queue bounds, class lists, UTF-8, attributes, late root and one asynchronous worker batch with mutation recovery passed.');
