#!/usr/bin/env node
// Independent protocol and playback controls; no live ad availability needed.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const script = await fs.readFile(new URL('../../renderer/content_blocking/youtube.js', import.meta.url), 'utf8');
const playerURL = 'https://www.youtube.com/youtubei/v1/player';
const raw = '{"keep":9007199254740993,"adPlacements":[1],"tiny":1.2300e-99,"escaped":"\\u0041","streamingData":{"formats":[{"url":"content"}]}}';
const expected = '{"keep":9007199254740993,"tiny":1.2300e-99,"escaped":"\\u0041","streamingData":{"formats":[{"url":"content"}]}}';
let assertions = 0;
function equal(actual, wanted, reason) {++assertions; assert.equal(actual, wanted, reason);}
function make({href = 'https://www.youtube.com/watch?v=content', body = raw,
               finalURL = playerURL, type = 'basic', fetchImpl, extras = {}} = {}) {
  class LocalResponse extends Response {}
  class LocalXHR {
    open(...args) {this.openArgs = args;}
    send(body) {this.sent = body;}
    get response() {return this.value;}
    get responseText() {return this.value;}
  }
  const calls = [];
  const context = vm.createContext({Response: LocalResponse, Request, URL, TextDecoder,
    XMLHttpRequest: LocalXHR, location: {href},
    fetch: fetchImpl ?? (async (...args) => {
      calls.push(args);
      const response = new LocalResponse(body, {status: 202, statusText: 'Accepted', headers: {'X-Fixture': 'kept'}});
      for (const [key, value] of Object.entries({url: finalURL, type, redirected: true}))
        Object.defineProperty(response, key, {value});
      return response;
    }), ...extras});
  vm.runInContext(script, context);
  return {context, calls, LocalXHR};
}

for (const api of ['text', 'arrayBuffer', 'blob', 'reader', 'clone', 'json']) {
  const {context} = make();
  const response = await context.fetch(playerURL);
  equal(response.status, 202); equal(response.statusText, 'Accepted');
  equal(response.url, playerURL); equal(response.redirected, true);
  equal(response.headers.get('X-Fixture'), 'kept');
  let text;
  if (api === 'reader') {
    const reader = response.body.getReader(); const pieces = [];
    for (;;) {const part = await reader.read(); if (part.done) break; pieces.push(part.value);}
    text = Buffer.concat(pieces).toString(); reader.releaseLock();
  } else if (api === 'arrayBuffer') text = Buffer.from(await response.arrayBuffer()).toString();
  else if (api === 'blob') text = await (await response.blob()).text();
  else if (api === 'clone') {
    const copied = response.clone(); equal(copied.url, playerURL); equal(copied.type, 'basic'); equal(copied.redirected, true);
    const nested = copied.clone(); equal(nested.url, playerURL); equal(await nested.text(), expected);
    text = await copied.text(); equal(await response.text(), expected);
  }
  else if (api === 'json') {
    const data = await response.json(); equal(data.adPlacements, undefined);
    equal(data.streamingData.formats[0].url, 'content');
  } else text = await response.text();
  if (text !== undefined) equal(text, expected, `${api} sees the same lossless body`);
  equal(response.bodyUsed, true);
  await assert.rejects(response.text.bind(response)); ++assertions;
}
for (const endpoint of ['/youtubei/v1/get_watch', '/youtubei/v1/reel_watch_sequence', '/playlist?list=fixture', '/watch?v=content']) {
  const {context} = make({finalURL: 'https://www.youtube.com' + endpoint});
  equal(Buffer.from(await (await context.fetch(playerURL)).arrayBuffer()).toString(), expected, endpoint);
}
for (const domain of ['m.youtube.com', 'music.youtube.com', 'www.youtubekids.com', 'www.youtube-nocookie.com']) {
  const {context} = make({href: `https://${domain}/embed/content`, finalURL: `https://${domain}/youtubei/v1/player`});
  equal(await (await context.fetch(playerURL)).text(), expected, domain);
  equal(vm.runInContext('JSON.parse(\'{"playerAds":[1],"content":2}\').playerAds', context), undefined);
  equal(vm.runInContext('JSON.parse(\'{"important":1,"normal":{"important":2}}\').important', context), undefined);
  equal(vm.runInContext('JSON.parse(\'{"important":1,"normal":{"important":2}}\').normal.important', context), 2);
}
for (const finalURL of ['https://external.test/youtubei/v1/player', 'https://www.youtube.com/youtubei/v1/browse']) {
  const {context} = make({finalURL});
  equal(await (await context.fetch(playerURL)).text(), raw, 'Unrelated final URL stays intact');
}
for (const type of ['opaque', 'opaqueredirect']) {
  const {context} = make({type});
  // Read the untouched body directly; native opaque responses have no body.
  equal(Buffer.from(await (await context.fetch(playerURL)).arrayBuffer()).toString(), raw);
}
for (const prefix of [")]}'\n", 'for(;;);', 'while(1);']) {
  const {context} = make({body: prefix + raw});
  equal(await (await context.fetch(playerURL)).text(), prefix + expected, 'XSSI envelope preserved');
}
const shorts = '{"entries":[{"keep":9007199254740993},{"command":{"reelWatchEndpoint":{"adClientParams":{"isAd":true}}}},{"keep":"\\u0041"}],"normal":{"adClientParams":{"foo":1}}}';
{
  const {context} = make({body: shorts});
  const text = await (await context.fetch(playerURL)).text();
  equal(text, '{"entries":[{"keep":9007199254740993},{"keep":"\\u0041"}],"normal":{"adClientParams":{"foo":1}}}');
  context.fixture = shorts;
  equal(vm.runInContext('JSON.parse(fixture).entries.length', context), 2);
  equal(vm.runInContext('JSON.parse(\'{"playerAds":[1]}\').playerAds.length', context), 1, 'Brave Chromium does not prune generic WWW JSON.parse player objects');
}
{
  const {context} = make();
  context.playerResponse = {child: {content: 1}};
  const child = context.playerResponse.child;
  equal('player' in child, false, 'Absent generic player aliases do not alter normal object shape');
  equal('response' in child, false, 'Absent generic response aliases do not alter normal object shape');
  child.adSlots = [1]; equal(child.adSlots, undefined, 'New retained ad field stays blocked');
  child.playerResponse = {playerAds: [1]}; equal(child.playerResponse.playerAds, undefined);
  assert.throws(() => Object.defineProperty(child, 'adSlots', {value: [1]})); ++assertions;
  Object.defineProperty(child, 'normal', {value: 3, configurable: true});
  equal(child.normal, 3); equal(delete child.normal, true, 'Normal descriptors preserved');
}

for (const mode of ['channel', 'lactmilli', 'instream', 'yahi', 'ordinary']) {
  const body = '{"keep":9007199254740993,"context":{"client":{"clientName":"WEB","userAgent":"Mozilla/5.0 (X11; ' + mode + ')"}},"playbackContext":{"contentPlaybackContext":{"referer":"https://www.youtube.com/watch?v=content","keep":1.2300e-99}},"escaped":"\\u0041"}';
  const {context, calls, LocalXHR} = make();
  const init = {method: 'POST', body, credentials: 'include'};
  await context.fetch(playerURL, init);
  equal(init.body, body, 'Caller init is not mutated');
  const result = calls[0][1].body;
  equal(result.includes('9007199254740993'), true);
  equal(result.includes('1.2300e-99'), true);
  equal(result.includes('"\\u0041"'), true);
  const parsed = JSON.parse(result);
  if (mode === 'ordinary') equal(result, body);
  else equal(parsed.playbackContext.contentPlaybackContext.referer.endsWith('#reloadxhr'), true);
  if (mode === 'channel') equal(parsed.context.client.clientScreen, 'CHANNEL');
  if (mode === 'lactmilli') equal(parsed.params, '8AUB');
  if (mode === 'yahi') equal(parsed.params, 'YAHI');
  if (mode === 'instream') equal(parsed.playbackContext.adPlaybackContext.adType, 'AD_TYPE_INSTREAM');
  const xhr = new LocalXHR(); xhr.open('POST', playerURL); xhr.send(body);
  const xhrData = JSON.parse(xhr.sent);
  equal(xhrData.context.client.clientScreen, parsed.context.client.clientScreen);
  const controller = new AbortController();
  const request = new Request(playerURL, {method: 'POST', body, credentials: 'include', signal: controller.signal});
  await context.fetch(request);
  const actual = calls[1][0];
  equal(actual.credentials, 'include'); equal(actual.method, 'POST');
  controller.abort(); equal(actual.signal.aborted, true);
  equal(request.bodyUsed, false, 'Cloned original remains unconsumed');
  await actual.arrayBuffer(); if (actual !== request) await request.arrayBuffer();
}
{
  const {context} = make({fetchImpl: async () => {throw new DOMException('aborted', 'AbortError');}});
  await assert.rejects(context.fetch(playerURL), error => error.name === 'AbortError'); ++assertions;
}
{
  const body = '{"context":{"client":{"clientName":"WEB","userAgent":"Mozilla/5.0 (X11; channel)"}}}';
  let reads = 0;
  const init = {method: 'POST', get body() {++reads; return body;}};
  const {context} = make({fetchImpl: async (_, actual) => {
    equal(actual, init, 'Accessor RequestInit passes through unchanged');
    equal(actual.body, body); return new Response('normal');
  }});
  await context.fetch(playerURL, init); equal(reads, 1, 'Only native fetch reads the getter');
}
{
  const body = '{"context":{"client":{"clientName":"WEB","userAgent":"Mozilla/5.0 (X11; channel)"}}}';
  const {context, calls} = make();
  const request = new Request(playerURL, {method: 'POST', body});
  Object.defineProperty(request, 'url', {get() {throw Error('shadow Request URL getter');}});
  await context.fetch(request);
  const actual = calls[0][0];
  equal(JSON.parse(await actual.text()).context.client.clientScreen, 'CHANNEL', 'Native Request getters ignore page shadows');
  await request.arrayBuffer();
}
{
  const body = '{"context":{"client":{"clientName":"WEB","userAgent":"Mozilla/5.0 (X11) channel-product"}}}';
  const {context, calls} = make();
  await context.fetch(playerURL, {method: 'POST', body});
  equal(calls[0][1].body, body, 'Only explicit recovery tags change player requests');
}
{
  const body = '{"context":{"client":{"clientName":"WEB","userAgent":"Mozilla/5.0 (X11; channel)"}}}';
  const {context, calls} = make();
  await context.fetch(new URL(playerURL), {method: 'POST', body});
  equal(JSON.parse(calls[0][1].body).context.client.clientScreen, 'CHANNEL', 'Plain native URL input supports body repair');
  let reads = 0;
  const input = new URL(playerURL);
  Object.defineProperty(input, 'toString', {value() {++reads; return playerURL;}});
  const init = {method: 'POST', body};
  const passthrough = make({fetchImpl: async (actual, actualInit) => {
    equal(String(actual), playerURL); equal(actualInit, init); return new Response('normal');
  }});
  await passthrough.context.fetch(input, init);
  equal(reads, 1, 'Custom URL conversion is invoked only by native fetch');
}
{
  const body = ' '.repeat(8 * 1024 * 1024) + raw;
  const {context} = make({body});
  equal(Buffer.from(await (await context.fetch(playerURL)).arrayBuffer()).toString(), body, 'Oversize body falls back intact');
}
{
  const {context} = make({extras: {yeeDocumentUrl: 'https://www.youtube.com/', location: {href: 'about:blank'}}});
  equal(await (await context.fetch(playerURL)).text(), expected, 'Native inherited document origin supplies host policy');
}

function playback(kind) {
  let now = 0, timerId = 0;
  const timers = new Map(), events = new Map(), windowEvents = new Map(), microtasks = [];
  const calls = [], agent = 'Mozilla/5.0 (X11; Linux x86_64)';
  const response = {videoDetails: {videoId: 'content'}, playabilityStatus: {status: 'OK'}};
  const player = {getPlayerResponse: () => response, getProgressState: () => ({current: 12}),
    getStatsForNerds: () => ({resolution: '1920x1080'}), loadVideoById: (...args) => calls.push(args),
    classList: {contains: () => ['ad', 'premium-ad'].includes(kind)}, querySelector: () => ({paused: false, pause: () => ++pauses}),
    getPlayerStateObject: () => ({isBuffering: kind === 'buffer'})};
  let pauses = 0;
  const doc = {baseURI: 'https://www.youtube.com/watch?v=content',
    getElementById: id => id === 'movie_player' ? player : null,
    addEventListener: (name, fn) => {if (!events.has(name)) events.set(name, new Set()); events.get(name).add(fn);},
    removeEventListener: (name, fn) => events.get(name)?.delete(fn),
    defaultView: {
      addEventListener: (name, fn) => {if (!windowEvents.has(name)) windowEvents.set(name, new Set()); windowEvents.get(name).add(fn);},
      removeEventListener: (name, fn) => windowEvents.get(name)?.delete(fn)}};
  const cfg = {data_: {INNERTUBE_CONTEXT: {client: {userAgent: agent}}, EXPERIMENT_FLAGS: {all_web_enable_network_machine: true, normal: true}}};
  if (['blocked', 'captcha', 'login', 'buffer', 'prerender'].includes(kind)) {
    response.playabilityStatus = {status: kind === 'login' ? 'LOGIN_REQUIRED' : 'UNPLAYABLE', errorScreen: {
      playerErrorMessageRenderer: {subreason: {runs: [{navigationEndpoint: {urlEndpoint: {
        url: 'https://support.google.com/youtube/answer/3037019', target: 'WEB_PAGE_TYPE_UNKNOWN'}}}]}}}};
    if (kind === 'captcha') response.playabilityStatus.errorScreen.playerErrorMessageRenderer.playerCaptchaViewModel = {};
    player.getStatsForNerds = () => ({resolution: '0x0', buffer_health_seconds: '0.00 s'});
    player.getProgressState = () => ({current: 0});
  }
  if (kind === 'live') response.videoDetails.isLive = true;
  if (kind === 'prerender') doc.prerendering = true;
  if (kind === 'ssai') player.getStatsForNerds = () => ({resolution: '1920x1080', debug_info: 'SSAP, AD'});
  const {context} = make({extras: {document: doc, ytcfg: cfg,
    ...(kind.startsWith('premium') ? {ytInitialData: {topbar: {desktopTopbarRenderer: {logo: {topbarLogoRenderer: {iconImage: {iconType: 'YOUTUBE_PREMIUM_LOGO'}}}}}}} : {}),
    Date: class extends Date {static now() {return now;}},
    queueMicrotask: fn => microtasks.push(fn), setTimeout: (fn, delay) => {const id = ++timerId; timers.set(id, {fn, at: now + delay}); return id;},
    clearTimeout: id => timers.delete(id)}});
  function flush() {while (microtasks.length) microtasks.shift()();}
  function advance(ms) {const end = now + ms; for (let count = 0; count < 100; ++count) {
    const next = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
    if (!next || next[1].at > end) break;
    now = next[1].at; timers.delete(next[0]); next[1].fn(); flush();
  } now = end; flush();}
  flush();
  return {context, response, player, calls, cfg, agent, timers, windowEvents, documentEvents: events, advance,
    fire: (name = 'yt-page-data-updated') => {for (const listener of events.get(name) ?? []) listener(); flush();}, pauses: () => pauses};
}
{
  const test = playback('blocked');
  const previous = test.cfg.data_.INNERTUBE_CONTEXT.client;
  test.context.ytcfg = {data_: {INNERTUBE_CONTEXT: {client: {userAgent: 'Mozilla/5.0 (Macintosh)'}}}};
  test.context.location.href = 'https://www.youtube.com/'; test.fire();
  equal(previous.userAgent, test.agent, 'Replacing ytcfg restores the previously tagged client');
  equal(test.context.ytcfg.data_.INNERTUBE_CONTEXT.client.userAgent, 'Mozilla/5.0 (Macintosh)', 'A new normal client is not overwritten');
}
{
  const test = playback('blocked');
  const originalFetch = test.context.fetch;
  for (let index = 0; index < 5; ++index) {
    test.context.document = {...test.context.document};
    vm.runInContext(script, test.context); test.advance(0);
    equal(test.timers.size, 1, 'Old document timer is disposed before remount');
    equal(test.documentEvents.get('freeze').size, 1, 'Document remount does not retain freeze listeners');
    equal(test.documentEvents.get('resume').size, 1, 'Document remount does not retain resume listeners');
    equal(test.windowEvents.get('pageshow').size, 1, 'Document remount does not retain pageshow listeners');
  }
  equal(test.context.fetch, originalFetch, 'Document remount preserves a single fetch wrapper');
}
{
  const test = playback('blocked');
  const previous = test.cfg.data_.INNERTUBE_CONTEXT.client;
  test.context.ytcfg = {get data_() {throw Error('old document configuration disappeared');}};
  test.context.location.href = 'https://www.youtube.com/'; test.fire();
  equal(previous.userAgent, test.agent, 'Restore does not read a disappearing new configuration');
  equal(test.timers.size, 0);
}
for (const kind of ['normal', 'live', 'premium', 'premium-ad', 'captcha', 'login']) {
  const test = playback(kind); test.advance(60000);
  equal(test.calls.length, 0, `${kind} does not reload`);
  equal(test.pauses(), 0, `${kind} is not paused`);
  equal(test.cfg.data_.INNERTUBE_CONTEXT.client.userAgent, test.agent);
  equal(test.cfg.data_.EXPERIMENT_FLAGS.normal, true);
  equal(test.cfg.data_.EXPERIMENT_FLAGS.all_web_enable_network_machine, false);
}
{
  const test = playback('blocked'); equal(test.calls.length, 1);
  test.fire(); equal(test.calls.length, 1, 'Repeated events respect cooldown from time zero');
  test.context.Date.now = () => -1;
  test.advance(9999); equal(test.calls.length, 1);
  test.advance(1); equal(test.calls.length, 2);
  test.advance(20000); equal(test.calls.length, 4, 'Retry is bounded to four modes');
  test.fire(); equal(test.cfg.data_.INNERTUBE_CONTEXT.client.userAgent.includes('; yahi'), true, 'Final mode survives immediate DOM callbacks while its request is being built');
  test.advance(10000); equal(test.cfg.data_.INNERTUBE_CONTEXT.client.userAgent, test.agent, 'Final request window closes before restoring the baseline');
  equal(test.timers.size, 0); equal(test.cfg.data_.INNERTUBE_CONTEXT.client.userAgent, test.agent);
  equal(test.calls[0][0], 'content'); equal(test.calls[0][1], 0);
}
{
  const test = playback('buffer'); equal(test.calls.length, 1);
  test.response.playabilityStatus.status = 'OK';
  test.fire(); test.advance(10000); equal(test.calls.length, 2, 'Explicitly retried zero-buffer stall gets a timed recheck');
  test.player.getStatsForNerds = () => ({resolution: '1920x1080'});
  test.player.getProgressState = () => ({current: 20}); test.fire();
  equal(test.timers.size, 0); equal(test.cfg.data_.INNERTUBE_CONTEXT.client.userAgent, test.agent);
}
{
  const test = playback('ad'); equal(test.pauses(), 1); equal(test.calls.length, 1);
  equal(test.calls[0][1], 0, 'Ad progress must not become a main-content start position');
  test.context.location.href = 'https://www.youtube.com/'; test.fire();
  equal(test.timers.size, 0); equal(test.cfg.data_.INNERTUBE_CONTEXT.client.userAgent, test.agent, 'SPA exit restores protocol agent');
}
{
  const test = playback('ssai'); equal(test.pauses(), 1); equal(test.calls.length, 1, 'Identified SSAI cannot take the normal-video progress path');
  equal(test.calls[0][1], 0);
}
{
  const test = playback('blocked');
  const data = Object.freeze({playlistId: 'PL-fixture', videos: ['content', 'next']});
  const restores = [];
  test.context.document.querySelector = () => ({getPlaylistData: () => data,
    setPlaylistData: value => restores.push(value), setPlayerPlaybackControlData: value => restores.push(value.playlistPanelRenderer)});
  test.context.location.href = 'https://www.youtube.com/watch?v=other&list=PL-fixture';
  test.response.videoDetails.videoId = 'other'; test.response.playerConfig = {playbackStartConfig: {startSeconds: 9}};
  test.player.getPlaylistId = () => null; test.fire();
  equal(test.calls.at(-1)[1], 9, 'Original playback start is retained');
  test.response.playabilityStatus.status = 'OK';
  test.player.getStatsForNerds = () => ({resolution: '1920x1080'}); test.player.getProgressState = () => ({current: 11});
  test.fire(); equal(restores.length, 2); equal(restores[0], data); equal(restores[1], data);
  test.fire(); equal(restores.length, 2, 'Saved playlist restores once');
}
{
  const test = playback('prerender'); equal(test.calls.length, 0);
  test.context.document.prerendering = false; test.fire('prerenderingchange');
  equal(test.calls.length, 1, 'Activation rechecks without requiring a DOM mutation');
}
{
  const test = playback('blocked');
  test.fire('freeze');
  test.fire(); test.advance(30000); equal(test.calls.length, 1, 'Hidden document callbacks cannot restart recovery');
  equal(test.timers.size, 0);
  const client = test.cfg.data_.INNERTUBE_CONTEXT.client;
  client.userAgent = 'Mozilla/5.0 (new baseline)';
  test.windowEvents.get('pageshow').values().next().value(); test.advance(0);
  equal(test.calls.length, 2, 'Restored document resumes bounded recovery');
  test.fire('freeze');
  equal(client.userAgent, 'Mozilla/5.0 (new baseline)', 'A fresh recovery stage preserves same-object UA updates');
}
{
  const test = playback('blocked');
  test.fire('freeze'); test.advance(10000);
  test.fire('resume'); test.advance(0);
  equal(test.calls.length, 2, 'An ordinary frozen tab resumes recovery without pageshow');
}
console.log(`Extended YouTube adapter passed ${assertions} protocol/playback assertions.`);
