// Copyright 2026 The Yee Authors
// SPDX-License-Identifier: BSD-3-Clause
// Public, generic execution scope for the separately supplied program.
(() => {
  const scriptletGlobals = (() => {
    const state = new Map();
    const get = state.get.bind(state);
    const set = state.set.bind(state);
    const has = state.has.bind(state);
    return new Proxy(state, {
      get(_target, key) {
        if (key === 'get') return get;
        if (key === 'set') return set;
        if (key === 'has') return has;
        return get(key);
      },
      set(_target, key, value) {
        if (key !== 'get' && key !== 'set' && key !== 'has') set(key, value);
        return true;
      },
    });
  })();
  // Yee has no DeAMP preference. Keep its optional scriptlet contract disabled.
  const deAmpEnabled = false;
  /* YEE_SCRIPTLET_PROGRAM */
})();
