// Verify the shared-state contract used by Brave/uBO scriptlets.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const runtime = await fs.readFile(new URL('../../components/content_blocking/scriptlet_runtime.js', import.meta.url), 'utf8');
const context = vm.createContext({});
const program = `
(() => {
  'use strict';
  scriptletGlobals.set('first', 7);
  scriptletGlobals.second = 9;
  const get = scriptletGlobals.get, set = scriptletGlobals.set;
  set('third', 11);
  globalThis.observed = [scriptletGlobals.first, get('second'),
    scriptletGlobals.has('third'), scriptletGlobals.third,
    scriptletGlobals.canDebug, deAmpEnabled];
})();
`;
const script = runtime.replace('/* YEE_SCRIPTLET_PROGRAM */', program);
vm.runInContext(script, context);
assert.deepEqual(Array.from(context.observed), [7, 9, true, 11, undefined, false]);
assert.equal(vm.runInContext('typeof scriptletGlobals', context), 'undefined');
vm.runInContext(runtime.replace('/* YEE_SCRIPTLET_PROGRAM */',
  "globalThis.fresh = scriptletGlobals.get('first');"), context);
assert.equal(context.fresh, undefined);
console.log('Scriptlet shared-state compatibility and independent scope passed');
