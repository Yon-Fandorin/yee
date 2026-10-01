// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
// Runs in Yee's own isolated world. Native CSS uses user origin.
const seenClasses = new Set(), seenIds = new Set();
const encoder = new TextEncoder();
const work = [];
let scheduled = false, rescan = false;
function bounded(name) {
  return name.length <= 512 && encoder.encode(name).length <= 512;
}
function enqueue(root, subtree = true) {
  if (root?.nodeType !== 1) return;
  if (work.length < 256) work.push({root, subtree, started: false,
    walker: null, element: null, index: 0});
  else rescan = true;
  schedule();
}
let inFlight = false;
globalThis.__yeeGenericComplete = () => {
  inFlight = false;
  if (work.length || rescan) schedule();
};
function schedule() {
  if (scheduled || inFlight) return;
  scheduled = true;
  if (typeof requestIdleCallback === 'function') requestIdleCallback(flush, {timeout: 100});
  else setTimeout(flush, 25);
}
function flush() {
  scheduled = false;
  const classes = [], ids = [];
  let budget = 512;
  while (work.length && budget-- > 0 && classes.length < 256 && ids.length < 256) {
    const item = work[0];
    if (!item.element) {
      if (!item.started) {
        item.started = true;
        item.element = item.root;
        if (item.subtree) item.walker = document.createTreeWalker(item.root, NodeFilter.SHOW_ELEMENT);
      } else item.element = item.walker?.nextNode();
      if (!item.element) { work.shift(); continue; }
      item.index = 0;
      const id = item.element.id;
      if (id && bounded(id) && !seenIds.has(id)) { seenIds.add(id); ids.push(id); }
    }
    const element = item.element;
    if (element.isConnected && item.index < element.classList.length) {
      // Retain progress through an element with more than one batch of classes.
      const name = element.classList[item.index++];
      if (bounded(name) && !seenClasses.has(name)) { seenClasses.add(name); classes.push(name); }
    } else item.element = null;
  }
  // One worker batch per document. Mutations stay in the bounded queue while
  // it is pending, then resume after the native reply applies its selectors.
  if (classes.length || ids.length)
    inFlight = __yeeApplyGeneric(classes, ids, exceptions) === true;
  if (seenClasses.size > 10000) seenClasses.clear();
  if (seenIds.size > 10000) seenIds.clear();
  if (!work.length && rescan) {
    rescan = false;
    // Queue overflow coalesces into one incremental scan instead of discarding
    // the last mutations. It uses the same per-callback budget as other work.
    enqueue(document.documentElement);
  }
  if (work.length) schedule();
}
new MutationObserver(records => {
  for (const record of records) {
    if (record.type === 'attributes') enqueue(record.target, false);
    else for (const node of record.addedNodes) enqueue(node);
  }
}).observe(document, {subtree: true, childList: true,
  attributes: true, attributeFilter: ['class', 'id']});
enqueue(document.documentElement);
