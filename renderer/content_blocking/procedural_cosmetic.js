// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
// Consumes adblock-rust's ProceduralOrActionFilter JSON in Yee's isolated world.
// Parsing stays on the engine worker. DOM access stays on the renderer, with
// resumable batches and no periodic polling when the document is unchanged.
const nonce = Array.from(crypto.getRandomValues(new Uint8Array(16)),
  byte => byte.toString(16).padStart(2, '0')).join('');
const prefix = 'data-yee-cb-' + nonce + '-';
const rules = [];
function unquote(value) {
  value = value.trim();
  if (value.length < 2 || !['"', "'"].includes(value[0]) || value.at(-1) !== value[0])
    return value;
  return value.slice(1, -1).replace(/\\([\da-f]{1,6}\s?|[^\r\n\f])/gi, (_, escape) => {
    if (/^[\da-f]/i.test(escape)) {
      const point = parseInt(escape.trim(), 16);
      return point > 0 && point <= 0x10ffff ? String.fromCodePoint(point) : '\ufffd';
    }
    return escape;
  });
}
function matcher(argument, exact = false) {
  const value = unquote(argument);
  if (value.startsWith('/')) {
    const end = value.lastIndexOf('/');
    if (end > 0 && /^[imsu]*$/.test(value.slice(end + 1))) {
      const expression = new RegExp(value.slice(1, end), value.slice(end + 1));
      return input => expression.test(input);
    }
  }
  return exact ? input => input === value : input => input.includes(value);
}
function pair(argument, separator) {
  // Separators inside quoted strings and /regular expressions/ belong to the
  // operand, e.g. :matches-attr(/data=ad/="yes").
  let quote = '', regex = false, escaped = false;
  for (let index = 0; index < argument.length; ++index) {
    const char = argument[index];
    if (escaped) { escaped = false; continue; }
    if (char === '\\') { escaped = true; continue; }
    if (quote) { if (char === quote) quote = ''; continue; }
    if (regex) { if (char === '/') regex = false; continue; }
    if (char === '"' || char === "'") { quote = char; continue; }
    if (char === '/' && !argument.slice(0, index).trim()) { regex = true; continue; }
    if (char === separator)
      return [argument.slice(0, index), argument.slice(index + 1)];
  }
  return [argument, null];
}
function compile(operator, marker) {
  if (!operator || typeof operator.arg !== 'string') throw Error('Invalid operator');
  const arg = operator.arg;
  switch (operator.type) {
    case 'css-selector':
      // A selector after a procedural step can continue into a descendant or
      // sibling; an attached suffix instead filters the current element.
      return node => {
        if (/^\s|^[>+~]/.test(arg)) {
          const relative = arg.trimStart();
          if (/^[+~]/.test(relative)) {
            const parent = node.parentElement;
            if (!parent) return [];
            let position = 1;
            for (let previous = node.previousElementSibling; previous;
                 previous = previous.previousElementSibling) ++position;
            return parent.querySelectorAll(`:scope > :nth-child(${position})${arg}`);
          }
          return node.querySelectorAll(':scope ' + arg);
        }
        return node.matches(arg) ? [node] : [];
      };
    case 'has-text': {
      const test = matcher(arg);
      // textContent avoids forcing layout on every text condition.
      return node => test(node.textContent ?? '') ? [node] : [];
    }
    case 'matches-attr': {
      const [key, value] = pair(arg, '=');
      const keyTest = matcher(key, true), valueTest = value === null ? null : matcher(value, true);
      return node => {
        for (const attribute of node.attributes)
          if (keyTest(attribute.name) && (!valueTest || valueTest(attribute.value))) return [node];
        return [];
      };
    }
    case 'matches-css':
    case 'matches-css-before':
    case 'matches-css-after': {
      const [property, value] = pair(arg, ':');
      if (value === null) throw Error('Missing CSS value');
      const name = unquote(property), test = matcher(value, true);
      const pseudo = operator.type === 'matches-css' ? null :
        operator.type === 'matches-css-before' ? '::before' : '::after';
      return node => {
        // Evaluate page CSS without this rule's own previous contribution.
        // Otherwise display:block -> hide -> display:none would oscillate on
        // every mutation. Ancestors may carry this rule's :upward() result.
        const marked = [];
        for (let ancestor = node; ancestor; ancestor = ancestor.parentElement) {
          if (ancestor.hasAttribute(marker)) {
            marked.push(ancestor);
            ancestor.removeAttribute(marker);
          }
        }
        try {
          const value = getComputedStyle(node, pseudo).getPropertyValue(name);
          return test(name === 'content' ? unquote(value) : value) ? [node] : [];
        } finally {
          for (const ancestor of marked) ancestor.setAttribute(marker, '');
        }
      };
    }
    case 'matches-path': {
      const test = matcher(arg);
      return node => test(location.pathname + location.search) ? [node] : [];
    }
    case 'min-text-length': {
      const length = Number(unquote(arg));
      if (!Number.isInteger(length) || length < 0) throw Error('Invalid text length');
      return node => (node.textContent?.length ?? 0) >= length ? [node] : [];
    }
    case 'upward': {
      const value = unquote(arg);
      if (/^\d+$/.test(value)) {
        const levels = Number(value);
        if (levels < 1 || levels >= 256) throw Error('Invalid ancestor depth');
        return node => {
          for (let index = 0; node && index < levels; ++index) node = node.parentElement;
          return node ? [node] : [];
        };
      }
      return node => {
        const parent = node.parentElement?.closest(value);
        return parent ? [parent] : [];
      };
    }
    case 'xpath':
      return node => {
        const snapshot = document.evaluate(unquote(arg), node, null,
          XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
        const result = [];
        for (let index = 0; index < snapshot.snapshotLength; ++index) {
          const match = snapshot.snapshotItem(index);
          if (match?.nodeType === Node.ELEMENT_NODE) result.push(match);
        }
        return result;
      };
    default: throw Error('Unsupported operator');
  }
}
function declarations(text) {
  const sheet = new CSSStyleSheet();
  sheet.replaceSync(`:root{${text}}`);
  if (sheet.cssRules.length !== 1 || sheet.cssRules[0].selectorText !== ':root' ||
      !sheet.cssRules[0].style?.length) throw Error('Invalid action style');
  return sheet.cssRules[0].style.cssText;
}
for (const serialized of proceduralActions) {
  try {
    const rule = JSON.parse(serialized);
    if (!Array.isArray(rule.selector) || !rule.selector.length) continue;
    const marker = prefix + rules.length;
    const operators = rule.selector.map(operator => compile(operator, marker));
    const action = rule.action;
    if (action && !['style', 'remove', 'remove-attr', 'remove-class'].includes(action.type)) continue;
    if (action && typeof action.arg !== 'string' && action.type !== 'remove') continue;
    let css = null;
    if (!action || action.type === 'style') {
      css = declarations(action?.arg ?? 'display: none !important');
      if (__yeeInsertProceduralStyle(marker, css) !== true) continue;
    }
    rules.push({selector: rule.selector, operators, action, marker, css, targets: new Set()});
  } catch { /* Skip one invalid rule without preventing following rules. */ }
}
let scheduled = false, dirty = true, iterator = null;
function schedule() {
  if (scheduled || !rules.length) return;
  scheduled = true;
  if (typeof requestIdleCallback === 'function') requestIdleCallback(flush, {timeout: 200});
  else setTimeout(flush, 25);
}
function* applyRule(rule) {
  let nodes;
  let start = 0;
  if (rule.selector[0].type === 'css-selector') {
    nodes = document.querySelectorAll(rule.selector[0].arg);
    start = 1;
  } else nodes = document.querySelectorAll('*');
  for (let index = start; index < rule.operators.length; ++index) {
    const result = new Set();
    for (const node of nodes) {
      if (node.isConnected) for (const match of rule.operators[index](node)) {
        result.add(match);
        yield;
      }
      yield;
    }
    nodes = result;
  }
  const targets = new Set();
  for (const node of nodes) {
    if (node.isConnected) targets.add(node);
    yield;
  }
  // Only change DOM after evaluating the whole selector; an invalid later
  // operator must never apply a partial match and hide unrelated content.
  const previous = rule.targets;
  rule.targets = targets;
  for (const node of previous) {
    if (rule.css && !targets.has(node)) node.removeAttribute(rule.marker);
    yield;
  }
  for (const node of targets) {
    if (!node.isConnected) continue;
    const action = rule.action;
    if (rule.css) {
      if (!node.hasAttribute(rule.marker)) node.setAttribute(rule.marker, '');
    } else if (action.type === 'remove') node.remove();
    else if (action.type === 'remove-attr') node.removeAttribute(unquote(action.arg));
    else if (action.type === 'remove-class' && node.classList.contains(unquote(action.arg)))
      node.classList.remove(unquote(action.arg));
    yield;
  }
  // Destructive actions need no retained nodes; removed subtrees can be freed.
  if (!rule.css) rule.targets.clear();
}
function* applyAll() {
  for (const rule of rules) {
    try { yield* applyRule(rule); }
    catch { /* A malformed browser selector leaves this rule unapplied. */ }
    yield;
  }
}
function flush() {
  scheduled = false;
  if (__yeeProceduralEnabled() !== true) {
    observer.disconnect();
    for (const rule of rules) for (const node of rule.targets) node.removeAttribute(rule.marker);
    iterator = null;
    return;
  }
  if (!iterator) { dirty = false; iterator = applyAll(); }
  const end = performance.now() + 4;
  for (let budget = 512; budget > 0 && performance.now() < end; --budget) {
    if (iterator.next().done) { iterator = null; break; }
  }
  if (iterator || dirty) schedule();
}
const observer = new MutationObserver(records => {
  for (const record of records) {
    if (record.type === 'attributes' && record.attributeName.startsWith(prefix)) {
      // Ignore our own marks, but repair a mark removed by page JavaScript.
      const rule = rules.find(rule => rule.marker === record.attributeName);
      if (!rule?.targets.has(record.target) || record.target.hasAttribute(rule.marker)) continue;
    }
    dirty = true;
    schedule();
    break;
  }
});
if (rules.length) {
  observer.observe(document, {subtree: true, childList: true, characterData: true, attributes: true});
  addEventListener('popstate', () => { dirty = true; schedule(); });
  addEventListener('hashchange', () => { dirty = true; schedule(); });
  // Runs after a pushState even when it produced no DOM mutation. Navigation
  // API events avoid wrapping the page's history functions in its main world.
  globalThis.navigation?.addEventListener('navigatesuccess', () => { dirty = true; schedule(); });
  schedule();
}
