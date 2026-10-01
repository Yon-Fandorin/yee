// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
// Independently authored. Data hooks do not mute/seek through an advertisement.
(() => {
  'use strict';
  if (globalThis.__yeeYouTubeInstalled) {
    globalThis.__yeeYouTubeInstalled.refresh?.(); return;
  }
  const NativeURL = URL, NativeResponse = Response, NativeRequest = globalThis.Request;
  const NativeDecoder = globalThis.TextDecoder;
  const documentURL = typeof yeeDocumentUrl === 'string' ? yeeDocumentUrl : location.href;
  const documentHost = new NativeURL(documentURL).hostname;
  const baseURL = () => globalThis.document?.baseURI || documentURL;
  const adKeys = new Set(['adPlacements', 'playerAds', 'adSlots', 'adBreakHeartbeatParams']);
  const parse = JSON.parse, stringify = JSON.stringify, clock = Date.now.bind(Date);
  const own = Function.call.bind(Object.prototype.hasOwnProperty);
  const describe = Object.getOwnPropertyDescriptor, define = Object.defineProperty;
  const playerKeys = ['playerResponse', 'ytInitialPlayerResponse', 'player', 'response'];
  const fields = new WeakMap();
  const guardedObjects = new WeakSet();
  const xhrTexts = new WeakMap();
  const noAd = () => undefined, dropAd = () => {};
  function guardAd(object, key) {
    try {
      const descriptor = describe(object, key);
      if (descriptor?.get === noAd && descriptor?.set === dropAd && !descriptor.configurable) return true;
      if (!descriptor || descriptor.configurable) {
        define(object, key, {configurable: false, enumerable: false,
          get: noAd, set: dropAd});
        return true;
      }
    } catch {} // Frozen/page-defined objects retain their original semantics.
    return false;
  }
  function clean(value, retainGuards = true) {
    if (!value || typeof value !== 'object') return value;
    const pending = [{value}], priority = [], seen = new WeakSet();
    let cursor = 0, budget = 10000;
    while ((priority.length || cursor < pending.length) && budget > 0) {
      const entry = priority.length ? priority.pop() : pending[cursor++];
      const object = entry.value;
      if (!object || typeof object !== 'object') continue;
      if (!entry.enumerate) {
        if (seen.has(object)) continue;
        seen.add(object); --budget;
        try {
          const entries = dataProperty(object, 'entries');
          if (Array.isArray(entries)) {
            for (let index = entries.length - 1; index >= 0 && budget-- > 0; --index)
              if (shortsAd(dataProperty(entries, String(index)))) entries.splice(index, 1);
          }
        } catch {}
        // Installed guards are non-configurable. Reuse that fact while still
        // traversing data: new children and containers must be sanitized.
        if (retainGuards) {
          if (!guardedObjects.has(object)) {
            let guarded = true;
            for (const key of adKeys) if (!guardAd(object, key)) guarded = false;
            if (guarded) guardedObjects.add(object);
          }
        } else {
          for (const key of adKeys) {
            try {if (describe(object, key)?.configurable) delete object[key];} catch {}
          }
        }
        // Known player containers are independent of a wide content sibling.
        let objectFields = fields.get(object);
        for (const key of playerKeys) {
          if (--budget <= 0) break;
          try {
            let tracked = objectFields?.get(key);
            if (!tracked) {
              const descriptor = describe(object, key);
              if (!descriptor && (key !== 'playerResponse' || !retainGuards)) continue;
              if (descriptor && !('value' in descriptor)) continue;
              tracked = {value: descriptor?.value};
              if (retainGuards && (!descriptor || (descriptor.configurable && descriptor.writable))) {
                tracked.get = () => tracked.value;
                define(object, key, {configurable: false, enumerable: descriptor?.enumerable ?? false,
                  get: tracked.get,
                  set: next => {tracked.value = clean(next);}});
                if (!objectFields) fields.set(object, objectFields = new Map());
                // Only non-configurable accessors are stored; ordinary data fields
                // keep their descriptor checks on subsequent reads.
                objectFields.set(key, tracked);
              }
            }
            if (tracked?.value && typeof tracked.value === 'object')
              priority.push({value: tracked.value});
          } catch {}
        }
        // Finish known player containers before enumerating this object's
        // arbitrary siblings, which could consume the entire visit budget.
        entry.enumerate = true;
        pending.push(entry);
        continue;
      }
      try {
        for (const key in object) {
          // Inherited keys and own-property checks also consume the budget.
          if (--budget <= 0) break;
          if (!own(object, key) || playerKeys.includes(key)) continue;
          const descriptor = describe(object, key);
          // Do not invoke arbitrary page getters while walking data.
          const child = descriptor && 'value' in descriptor ? descriptor.value : null;
          if (child && typeof child === 'object' && pending.length - cursor + priority.length < budget)
            pending.push({value: child});
        }
      } catch {} // An arbitrary Proxy trap cannot be made time-bounded here.
    }
    return value;
  }
  function youtubeHost(host) {
    return ['youtube.com', 'youtube-nocookie.com', 'youtubekids.com']
      .some(domain => host === domain || host.endsWith('.' + domain));
  }
  function playerEndpoint(url) {
    try {
      const target = new NativeURL(url, baseURL());
      return youtubeHost(target.hostname) &&
        (/^\/youtubei\/v\d+\/(player|next|get_watch|reel_watch_sequence)(?:\/|$)/.test(target.pathname) ||
         (target.pathname === '/playlist' && target.searchParams.has('list')) ||
         (target.pathname === '/watch' && (target.searchParams.has('v') || target.searchParams.has('t'))));
    } catch { return false; }
  }
  function dataProperty(object, key) {
    const descriptor = object && typeof object === 'object' && describe(object, key);
    return descriptor && 'value' in descriptor ? descriptor.value : undefined;
  }
  function shortsAd(value) {
    try {
      let path = value;
      for (const key of ['command', 'reelWatchEndpoint', 'adClientParams']) path = dataProperty(path, key);
      return !!path && own(path, 'isAd');
    } catch { return false; }
  }
  function pruneShorts(root) {
    const pending = [root], seen = new WeakSet();
    let cursor = 0, budget = 10000;
    while (cursor < pending.length && budget-- > 0) {
      const value = pending[cursor++];
      if (!value || typeof value !== 'object' || seen.has(value)) continue;
      seen.add(value);
      try {
        const entries = dataProperty(value, 'entries');
        if (Array.isArray(entries)) {
          for (let index = entries.length - 1; index >= 0 && budget-- > 0; --index)
            if (shortsAd(dataProperty(entries, String(index)))) entries.splice(index, 1);
        }
        for (const key in value) {
          if (--budget <= 0) break;
          if (!own(value, key)) continue;
          const child = dataProperty(value, key);
          if (child && typeof child === 'object' && pending.length - cursor < budget) pending.push(child);
        }
      } catch {} // Preserve frozen data; arbitrary Proxy traps are not time-bounded.
    }
    return root;
  }
  function cleanText(text, changes = null) {
    if (typeof text !== 'string' || text.length > 8 * 1024 * 1024 ||
        (!changes && !text.includes('ad') && !text.includes('\\'))) return text;
    const prefix = text.match(/^(?:\)\]\}'[, \t]*\r?\n|for\s*\(;;\);|while\s*\(1\);)\s*/)?.[0];
    if (prefix && !changes) return prefix + cleanText(text.slice(prefix.length));
    try {
      // Validate without using the decoded numeric values for output. Edits
      // retain original literals, escapes, duplicate keys and unaffected data.
      parse(text);
      let position = 0, budget = 200000;
      const edits = [];
      const replacements = new Map((changes ?? []).map(change => [stringify(change.path), change.value]));
      function space() {while (/\s/.test(text[position] ?? '') && position < text.length) ++position;}
      function string() {
        const start = position++;
        while (position < text.length) {
          const char = text[position++];
          if (char === '\\') ++position;
          else if (char === '"') return text.slice(start, position);
        }
        throw new Error('Invalid JSON string');
      }
      function removeMembers(members) {
        for (let first = 0; first < members.length; ++first) {
          if (!members[first].ad) continue;
          let last = first;
          while (last + 1 < members.length && members[last + 1].ad) ++last;
          edits.push(last + 1 < members.length
            ? [members[first].start, members[last].comma + 1]
            : [first ? members[first - 1].comma : members[first].start, members[last].end]);
          first = last;
        }
      }
      function value(depth, memberKey = '', path = []) {
        if (--budget <= 0 || depth > 128) throw new Error('JSON work limit');
        space();
        const char = text[position];
        if (char === '"') {string(); return;}
        if (char === '{') {
          ++position; space();
          const members = [];
          while (text[position] !== '}') {
            if (--budget <= 0) throw new Error('JSON work limit');
            const start = position, key = parse(string());
            space(); ++position; space();
            const valueStart = position, childPath = changes ? [...path, key] : [];
            value(depth + 1, key, childPath);
            const valueEnd = position, changePath = changes ? stringify(childPath) : null;
            if (replacements.has(changePath))
              edits.push([valueStart, valueEnd, stringify(replacements.get(changePath))]);
            space();
            const end = position, comma = text[position] === ',' ? position++ : null;
            members.push({start, end, comma, key, ad: !changes && adKeys.has(key)});
            space();
            if (comma === null) break;
          }
          if (changes) {
            const missing = changes.filter(change => change.path.length === path.length + 1 &&
              stringify(change.path.slice(0, -1)) === stringify(path) &&
              !members.some(member => member.key === change.path.at(-1)));
            if (missing.length) edits.push([position, position, (members.length ? ',' : '') +
              missing.map(change => stringify(change.path.at(-1)) + ':' + stringify(change.value)).join(',')]);
          }
          ++position;
          removeMembers(members);
        } else if (char === '[') {
          ++position; space();
          const members = [];
          while (text[position] !== ']') {
            const start = position;
            value(depth + 1, '', changes ? [...path, String(members.length)] : []); space();
            const end = position, comma = text[position] === ',' ? position++ : null;
            members.push({start, end, comma,
              ad: !changes && memberKey === 'entries' && shortsAd(parse(text.slice(start, end)))});
            space();
            if (comma === null) break;
          }
          ++position;
          removeMembers(members);
        } else {
          while (position < text.length && !/[\s,}\]]/.test(text[position])) ++position;
        }
      }
      value(0);
      if (!edits.length) return text;
      edits.sort((a, b) => a[0] - b[0] || b[1] - a[1]);
      let cursor = 0, result = '';
      for (const [start, end, replacement = ''] of edits) {
        if (end < cursor || (end === cursor && start < end)) continue;
        if (start > cursor) result += text.slice(cursor, start);
        result += replacement;
        cursor = end;
      }
      return result + text.slice(cursor);
    } catch {
      // Keep malformed JSON intact. Non-JSON watch/playlist text may contain
      // embedded JSON keys; rename only the targeted property tokens.
      if (changes || /^\s*[\[{]/.test(text)) return text;
      return text.replace(/"(?:adPlacements|playerAds|adSlots|adBreakHeartbeatParams)"(?=\s*:)/g, '"no_ads"');
    }
  }
  function playerRequest(url) {
    try {
      const target = new NativeURL(url, baseURL());
      return youtubeHost(target.hostname) && /^\/youtubei\/v\d+\/player(?:\/|$)/.test(target.pathname);
    } catch { return false; }
  }
  function rewritePlayerRequest(text) {
    if (typeof text !== 'string' || text.length > 1024 * 1024) return text;
    try {
      const data = parse(text), client = data?.context?.client;
      const agent = typeof client?.userAgent === 'string' ? client.userAgent : '';
      const os = agent.match(/^Mozilla\/5\.0 \(([^)]+)\)/)?.[1] || '';
      const tagged = name => new RegExp('(?:^|;\\s*)' + name + '(?:;|$)').test(os);
      const changes = [], set = (path, value) => changes.push({path: path.split('.'), value});
      if (tagged('channel') && client.clientName === 'WEB')
        set('context.client.clientScreen', 'CHANNEL');
      if (tagged('lactmilli')) set('params', '8AUB');
      if (tagged('yahi')) set('params', 'YAHI');
      const playback = data?.playbackContext?.contentPlaybackContext;
      if (playback && typeof playback === 'object') {
        if (tagged('instream')) {
          if (data.playbackContext.adPlaybackContext && typeof data.playbackContext.adPlaybackContext === 'object')
            set('playbackContext.adPlaybackContext.adType', 'AD_TYPE_INSTREAM');
          else set('playbackContext.adPlaybackContext', {adType: 'AD_TYPE_INSTREAM'});
        }
        if (['channel', 'lactmilli', 'instream'].some(tagged))
          set('playbackContext.contentPlaybackContext.lactMilliseconds', String(clock()));
        if (['adunit', 'channel', 'lactmilli', 'instream', 'inline', 'yahi', 'eafg'].some(tagged) && typeof playback.referer === 'string')
          set('playbackContext.contentPlaybackContext.referer', playback.referer.replace(/(?:#reloadxhr)?$/, '#reloadxhr'));
      }
      return changes.length ? cleanText(text, changes) : text;
    } catch { return text; }
  }
  function configureNetworkFlags(config) {
    if (documentHost !== 'www.youtube.com') return config;
    try {
      const flags = config?.data_?.EXPERIMENT_FLAGS;
      if (!flags || typeof flags !== 'object') return config;
      for (const name of ['all_web_enable_network_machine', 'all_web_network_machine_raw_request']) {
        const descriptor = describe(flags, name);
        if (!descriptor || descriptor.configurable)
          define(flags, name, {configurable: false, enumerable: descriptor?.enumerable ?? true,
            get: () => false, set: () => {}});
        else if ('value' in descriptor && descriptor.writable) flags[name] = false;
      }
    } catch {}
    return config;
  }
  const configDescriptor = describe(globalThis, 'ytcfg');
  if (!configDescriptor || ('value' in configDescriptor && configDescriptor.configurable)) {
    let value = configureNetworkFlags(configDescriptor?.value);
    define(globalThis, 'ytcfg', {configurable: false, enumerable: configDescriptor?.enumerable ?? true,
      get: () => configureNetworkFlags(value), set: next => {value = configureNetworkFlags(next);}});
  }
  const originalSplit = String.prototype.split;
  try { define(String.prototype, 'split', {configurable: true, writable: true,
    value: function(...args) {
      const target = typeof this === 'string' && this.includes('H5_async_logging_delay_ms=')
        ? this.replace(/all_web_(enable_network_machine|network_machine_raw_request)=true/g, 'all_web_$1=false') : this;
      return Reflect.apply(originalSplit, target, args);
    }}); } catch {}
  for (const name of ['ytInitialPlayerResponse', 'playerResponse']) {
    const descriptor = describe(globalThis, name);
    if (!descriptor || ('value' in descriptor && descriptor.configurable)) {
      let value = clean(descriptor?.value);
      define(globalThis, name, {
        configurable: false, enumerable: descriptor?.enumerable ?? true,
        get: () => clean(value), set: next => { value = clean(next); }
      });
    }
  }
  const host = documentHost;
  const pruneParsedPlayers = ['m.youtube.com', 'music.youtube.com', 'youtubekids.com', 'youtube-nocookie.com']
    .some(domain => host === domain || host.endsWith('.' + domain));
  try { define(JSON, 'parse', {configurable: true, writable: true,
    value: function(...args) {
      const value = Reflect.apply(parse, this, args);
      if (!pruneParsedPlayers) return pruneShorts(value);
      clean(value, false);
      // This top-level marker is removed only on the mobile/Music/Kids/embed
      // parse path. Normal WWW player JSON and nested content retain it.
      if (value && typeof value === 'object') {
        try {if (describe(value, 'important')?.configurable) delete value.important;} catch {}
      }
      return value;
    }}); } catch {}
  const originalFetch = globalThis.fetch;
  const originalClone = Response.prototype.clone, responseMetadata = new WeakMap();
  function cloneWithMetadata(...args) {
    const result = Reflect.apply(originalClone, this, args);
    const metadata = responseMetadata.get(this);
    return metadata ? stampResponse(result, metadata) : result;
  }
  function stampResponse(response, metadata) {
    for (const key of ['url', 'type', 'redirected', 'ok'])
      define(response, key, {value: metadata[key]});
    responseMetadata.set(response, metadata);
    // A clone can come from another native Response prototype/realm.
    define(response, 'clone', {configurable: true, writable: true, value: cloneWithMetadata});
    return response;
  }
  try { define(Response.prototype, 'clone', {configurable: true, writable: true, value: cloneWithMetadata}); } catch {}
  const requestGetters = new Map();
  const nativeURLString = NativeURL.prototype.toString;
  const nativeURLHref = describe(NativeURL.prototype, 'href')?.get;
  function inputURL(input) {
    if (typeof input === 'string') return input;
    if (input instanceof NativeURL && Object.getPrototypeOf(input) === NativeURL.prototype &&
        !describe(input, 'toString') && !describe(input, Symbol.toPrimitive) &&
        !describe(NativeURL.prototype, Symbol.toPrimitive) &&
        !describe(Object.prototype, Symbol.toPrimitive) &&
        describe(NativeURL.prototype, 'toString')?.value === nativeURLString)
      return Reflect.apply(nativeURLHref, input, []);
    if (typeof NativeRequest === 'function' && input instanceof NativeRequest)
      return requestValue(input, 'url');
    return '';
  }
  if (typeof NativeRequest === 'function')
    for (const name of ['url', 'method', 'body', 'bodyUsed'])
      requestGetters.set(name, describe(NativeRequest.prototype, name)?.get);
  const requestValue = (request, name) => Reflect.apply(requestGetters.get(name), request, []);
  const cloneRequest = NativeRequest?.prototype?.clone;
  function dataInit(init) {
    if (init == null) return true;
    if (typeof init !== 'object') return false;
    const descriptors = Object.getOwnPropertyDescriptors(init);
    if (Object.values(descriptors).some(descriptor => !('value' in descriptor))) return false;
    // Preserve native getter count/order and inherited RequestInit fields by
    // passing accessor/prototype-based inputs straight to native fetch.
    let proto = Object.getPrototypeOf(init), budget = 16;
    const options = ['method', 'headers', 'body', 'referrer', 'referrerPolicy', 'mode',
      'credentials', 'cache', 'redirect', 'integrity', 'keepalive', 'signal',
      'priority', 'duplex', 'window', 'attributionReporting', 'browsingTopics',
      'adAuctionHeaders', 'sharedStorageWritable', 'retryOptions', 'targetAddressSpace'];
    while (proto && budget-- > 0) {
      if (options.some(name => describe(proto, name))) return false;
      proto = Object.getPrototypeOf(proto);
    }
    return !proto;
  }
  async function readText(body, limit) {
    const reader = body.getReader();
    try {
      const decoder = new NativeDecoder('utf-8', {fatal: true, ignoreBOM: true});
      let bytes = 0, text = '';
      for (;;) {
        const part = await reader.read();
        if (part.done) return text + decoder.decode();
        bytes += part.value.byteLength;
        if (bytes > limit) {void reader.cancel().catch(() => {}); return null;}
        text += decoder.decode(part.value, {stream: true});
      }
    } catch (error) {
      void reader.cancel().catch(() => {}); throw error;
    } finally {reader.releaseLock();}
  }
  if (typeof originalFetch === 'function') {
    try { define(globalThis, 'fetch', {configurable: true, writable: true,
      value: async function(...args) {
        // Native Request cloning preserves method, credentials and abort signal.
        // Only tagged player JSON changes; ordinary/Premium requests stay intact.
        try {
          const input = args[0], init = args[1];
          const requestUrl = inputURL(input);
          if (playerRequest(requestUrl) && dataInit(init)) {
            if (typeof init?.body === 'string') {
              const body = rewritePlayerRequest(init.body);
              if (body !== init.body) args[1] = {...init, body};
            } else if (typeof NativeRequest === 'function' && input instanceof NativeRequest && (init?.body === undefined || init.body === null) && !requestValue(input, 'bodyUsed') &&
                       (init?.method === undefined || typeof init.method === 'string') &&
                       !['GET', 'HEAD'].includes((init?.method ?? requestValue(input, 'method')).toUpperCase())) {
              const cloned = Reflect.apply(cloneRequest, input, []);
              const text = await readText(requestValue(cloned, 'body'), 1024 * 1024);
              const body = rewritePlayerRequest(text);
              if (text !== null && body !== text) args[0] = new NativeRequest(input, {body});
            }
          }
        } catch {} // Native fetch retains validation/error semantics for originals.
        const response = await Reflect.apply(originalFetch, this, args);
        if (!playerEndpoint(response.url) || response.type === 'opaque' ||
            response.type === 'opaqueredirect' || !response.body) return response;
        try {
          // Rewrite a clone so fallbacks retain the complete original body.
          // Cancel the tee branch without awaiting its peer's consumption.
          const text = await readText(response.clone().body, 8 * 1024 * 1024);
          if (text === null) return response;
          const result = cleanText(text);
          if (result === text) return response;
          const rewritten = new NativeResponse(result, {status: response.status,
            statusText: response.statusText, headers: response.headers});
          return stampResponse(rewritten, {url: response.url, type: response.type,
            redirected: response.redirected, ok: response.ok});
        } catch { return response; } // Abort, locked body, decoder/construction failure.
      }}); } catch {}
  }
  const originalOpen = XMLHttpRequest.prototype.open, originalSend = XMLHttpRequest.prototype.send;
  const xhrRequests = new WeakMap();
  if (typeof originalOpen === 'function' && typeof originalSend === 'function') {
    try { define(XMLHttpRequest.prototype, 'open', {configurable: true, writable: true,
      value: function(...args) {
        const result = Reflect.apply(originalOpen, this, args);
        if (typeof args[0] === 'string' && typeof args[1] === 'string')
          xhrRequests.set(this, {method: args[0].toUpperCase(), url: new NativeURL(args[1], baseURL()).href});
        else xhrRequests.delete(this);
        return result;
      }});
      define(XMLHttpRequest.prototype, 'send', {configurable: true, writable: true,
        value: function(body) {
          const request = xhrRequests.get(this);
          const payload = request?.method === 'POST' && playerRequest(request.url) ? rewritePlayerRequest(body) : body;
          return Reflect.apply(originalSend, this, [payload]);
        }});
    } catch {}
  }
  for (const method of ['json', 'text']) {
    const original = Response.prototype[method];
    try { Object.defineProperty(Response.prototype, method, {
      configurable: true, writable: true,
      value: async function(...args) {
        const value = await Reflect.apply(original, this, args);
        if (!playerEndpoint(this.url)) return value;
        // Parsed response objects need removal, not permanent absent-field
        // accessors. Those accessors make original pruners report a change and
        // serialize an already-clean body, losing unrelated JSON bytes.
        return method === 'json' ? clean(value, false) : cleanText(value);
      }
    }); } catch {}
  }
  for (const property of ['response', 'responseText']) {
    const descriptor = Object.getOwnPropertyDescriptor(XMLHttpRequest.prototype, property);
    if (!descriptor?.get || !descriptor.configurable) continue;
    try { Object.defineProperty(XMLHttpRequest.prototype, property, {
      ...descriptor,
      get: function() {
        const value = Reflect.apply(descriptor.get, this, []);
        if (!playerEndpoint(this.responseURL)) return value;
        if (typeof value !== 'string') return clean(value, false);
        const cached = xhrTexts.get(this);
        if (cached?.text === value && cached.url === this.responseURL) return cached.result;
        const result = cleanText(value);
        if (value.length <= 8 * 1024 * 1024)
          xhrTexts.set(this, {text: value, result, url: this.responseURL});
        return result;
      }
    }); } catch {}
  }
  function installPlaybackRecovery() {
    const doc = globalThis.document;
    if (!doc || typeof doc.getElementById !== 'function' ||
        documentHost !== 'www.youtube.com') return;
    const wrapped = new WeakMap(), originalAgents = new WeakMap();
    const modes = ['channel', 'lactmilli', 'instream', 'yahi'];
    let retry = null, queued = false, timer = null, disposed = false, suspended = false, modeClient = null;
    function cancelTimer() {if (timer !== null) {clearTimeout(timer); timer = null;}}
    function revisit(delay) {
      cancelTimer();
      if (typeof setTimeout === 'function') timer = setTimeout(() => {timer = null; schedule();}, delay);
    }
    function client() {return globalThis.ytcfg?.data_?.INNERTUBE_CONTEXT?.client;}
    function restoreAgent() {
      const previous = modeClient; modeClient = null;
      try {if (previous) previous.userAgent = originalAgents.get(previous);} catch {}
    }
    function setMode(mode) {
      if (!mode) {restoreAgent(); return true;}
      const context = client();
      if (modeClient && modeClient !== context) restoreAgent();
      if (!context || typeof context.userAgent !== 'string') return false;
      if (modeClient !== context) originalAgents.set(context, context.userAgent);
      const agent = originalAgents.get(context);
      context.userAgent = /Mozilla\/5\.0 \([^)]+/.test(agent)
        ? agent.replace(/(Mozilla\/5\.0 \([^)]+)/, '$1; ' + mode) : agent;
      if (context.userAgent !== agent) modeClient = context;
      return modeClient !== null;
    }
    function rememberPlaylist(url) {
      try {
        const id = url.searchParams.get('list'), manager = doc.querySelector?.('yt-playlist-manager');
        const data = id && manager?.getPlaylistData?.();
        if (data && (!data.playlistId || data.playlistId === id)) retry.playlist = {id, data};
      } catch {}
    }
    function restorePlaylist(url, player) {
      const saved = retry.playlist;
      if (!saved || saved.id !== url.searchParams.get('list') || player.getPlaylistId?.() !== null) return;
      try {
        const manager = doc.querySelector?.('yt-playlist-manager');
        if (typeof manager?.setPlaylistData !== 'function' || typeof manager?.setPlayerPlaybackControlData !== 'function') return;
        manager.setPlaylistData(saved.data);
        manager.setPlayerPlaybackControlData({playlistPanelRenderer: saved.data});
        retry.playlist = null;
      } catch {}
    }
    function check() {
      queued = false;
      if (disposed || suspended) return;
      try {
        const url = new NativeURL(location.href), player = doc.getElementById('movie_player');
        if (doc !== globalThis.document) {cancelTimer(); return;}
        if (!player || url.pathname !== '/watch' || doc.prerendering) {
          cancelTimer(); setMode(''); retry = null; return;
        }
        // Sanitize at the actual player consumption boundary as well as ingress.
        const original = player.getPlayerResponse;
        if (typeof original === 'function' && wrapped.get(player) !== original) {
          const replacement = function(...args) {return clean(Reflect.apply(original, this, args));};
          player.getPlayerResponse = replacement;
          if (player.getPlayerResponse === replacement) wrapped.set(player, replacement);
        }
        const response = player.getPlayerResponse?.(), id = response?.videoDetails?.videoId;
        if (!id || id !== url.searchParams.get('v') || typeof player.loadVideoById !== 'function') {
          cancelTimer(); setMode(''); retry = null; return;
        }
        if (!retry || retry.id !== id) {
          cancelTimer(); setMode(''); retry = {id, attempt: 0, last: null, stalled: null};
        }
        const premium = globalThis.ytInitialData?.topbar?.desktopTopbarRenderer?.logo?.topbarLogoRenderer?.iconImage?.iconType === 'YOUTUBE_PREMIUM_LOGO' ||
          doc.getElementById('masthead')?.getAttribute?.('logo-type') === 'YOUTUBE_PREMIUM_LOGO';
        if (premium) {cancelTimer(); setMode(''); return;}
        const stats = player.getStatsForNerds?.();
        const advertisement = player.classList?.contains('ad-showing') ||
          (typeof stats?.debug_info === 'string' && stats.debug_info.startsWith('SSAP, AD'));
        if (advertisement) {
          // Stop an identified advertisement. Never mute/seek it to completion.
          const video = player.querySelector?.('video');
          if (video && !video.paused && typeof video.pause === 'function') video.pause();
        }
        const progress = player.getProgressState?.();
        if (!advertisement && (response.videoDetails.isLive || (progress?.current > 0 && stats?.resolution && stats.resolution !== '0x0'))) {
          if (retry.attempt > 0) restorePlaylist(url, player);
          cancelTimer(); setMode(''); retry.stalled = null; return;
        }
        const error = response?.playabilityStatus?.errorScreen;
        const captcha = error?.playerErrorMessageRenderer?.playerCaptchaViewModel;
        const reasons = stringify(error?.playerErrorMessageRenderer?.subreason?.runs ||
          error?.playerInterstitialRenderer?.content?.interstitialViewModel?.description?.commandRuns || []);
        const blocked = response?.playabilityStatus?.status === 'UNPLAYABLE' && !captcha &&
          reasons.includes('WEB_PAGE_TYPE_UNKNOWN') && reasons.includes('https://support.google.com/youtube/answer/3037019');
        const buffering = retry.attempt > 0 && player.getPlayerStateObject?.()?.isBuffering &&
          stats?.buffer_health_seconds === '0.00 s' && stats.resolution === '0x0';
        const now = clock();
        if (buffering) {if (retry.stalled === null) retry.stalled = now;}
        else retry.stalled = null;
        if (retry.attempt >= modes.length) {
          // Keep the final mode for its request window. A DOM callback during
          // loadVideoById must not clear it before the player builds its body.
          if (retry.last !== null && now - retry.last < 10000) revisit(10000 - (now - retry.last));
          else {cancelTimer(); setMode('');}
          return;
        }
        if (!advertisement && !blocked && !(buffering && now - retry.stalled >= 8000)) {
          if (buffering) revisit(Math.max(1, 8000 - (now - retry.stalled)));
          else cancelTimer();
          return;
        }
        if (retry.last !== null && now - retry.last < 10000) {
          revisit(10000 - (now - retry.last)); return;
        }
        const start = response.playerConfig?.playbackStartConfig?.startSeconds ?? (advertisement ? 0 : progress?.current ?? 0);
        const position = typeof start === 'number' && Number.isFinite(start) && start >= 0 ? start : 0;
        if (!setMode(modes[retry.attempt])) {cancelTimer(); return;}
        if (retry.attempt === 0) rememberPlaylist(url);
        ++retry.attempt; retry.last = now; retry.stalled = null;
        player.loadVideoById(id, position);
        revisit(10000);
      } catch {} // Page player APIs may disappear during SPA navigation.
    }
    function schedule() {
      if (disposed || queued) return;
      queued = true;
      if (typeof queueMicrotask === 'function') queueMicrotask(check);
      else if (typeof setTimeout === 'function') setTimeout(check, 0);
      else check();
    }
    const documentEvents = ['DOMContentLoaded', 'yt-navigate-finish', 'yt-page-data-updated', 'prerenderingchange'];
    for (const name of documentEvents)
      doc.addEventListener?.(name, schedule);
    const freeze = () => {suspended = true; cancelTimer(); setMode('');};
    const resume = () => {suspended = false; schedule();};
    doc.addEventListener?.('freeze', freeze);
    doc.addEventListener?.('resume', resume);
    doc.defaultView?.addEventListener?.('pageshow', resume);
    let observer;
    if (typeof MutationObserver === 'function')
      (observer = new MutationObserver(schedule)).observe(doc, {childList: true, subtree: true,
        attributes: true, attributeFilter: ['class', 'player-unavailable']});
    schedule();
    return () => {
      disposed = true; cancelTimer(); setMode(''); observer?.disconnect();
      for (const name of documentEvents)
        doc.removeEventListener?.(name, schedule);
      doc.removeEventListener?.('freeze', freeze);
      doc.removeEventListener?.('resume', resume);
      doc.defaultView?.removeEventListener?.('pageshow', resume);
    };
  }
  let lastDocument = Symbol(), disposeRecovery;
  const refresh = () => {
    const doc = globalThis.document;
    if (doc === lastDocument) return;
    disposeRecovery?.(); lastDocument = doc; disposeRecovery = installPlaybackRecovery();
  };
  define(globalThis, '__yeeYouTubeInstalled', {value: Object.freeze({refresh})});
  refresh();
})();
