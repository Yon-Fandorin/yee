#!/usr/bin/env node
// Verify the adapter against Web APIs and JSON fixtures independently of ad
// availability. Live YouTube playback remains a separate native-app gate.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const script = await fs.readFile(new URL('../../renderer/content_blocking/youtube.js', import.meta.url), 'utf8');
const clone = value => structuredClone(value);
const data = {playabilityStatus: {status: 'OK'}, videoDetails: {videoId: 'content'},
  streamingData: {formats: [{url: 'content-stream'}]}, captions: {tracks: ['retained']},
  adPlacements: [{ad: true}], playerAds: [1], nested: {adSlots: [2], content: 'retained'}};
class FixtureResponse extends Response {}
class FixtureXHR {
  constructor(value, type = '') {this.value = value; this.responseType = type; this.responseURL = 'https://www.youtube.com/youtubei/v1/player';}
  get response() {return this.value;}
  get responseText() {if (this.responseType === 'json') throw new DOMException('InvalidStateError'); return this.value;}
}
const context = vm.createContext({Response: FixtureResponse, XMLHttpRequest: FixtureXHR,
  URL, location: {href: 'https://www.youtube.com/watch?v=content'}});
vm.runInContext(script, context, {filename: fileURLToPath(new URL('../../renderer/content_blocking/youtube.js', import.meta.url))});
context.ytInitialPlayerResponse = clone(data);
assert.equal(context.ytInitialPlayerResponse.adPlacements, undefined);
assert.equal(context.ytInitialPlayerResponse.nested.adSlots, undefined);
assert.deepEqual(context.ytInitialPlayerResponse.streamingData, data.streamingData);
assert.deepEqual(context.ytInitialPlayerResponse.captions, data.captions);
let reads = 0;
const wide = {};
for (let i = 0; i < 15000; ++i)
  Object.defineProperty(wide, `item${i}`, {enumerable: true, get() {++reads; return {};}});
context.ytInitialPlayerResponse = {...clone(data), wide};
assert.ok(reads < 10000, 'Traversal bounds property visits, including one wide object');
assert.equal(context.ytInitialPlayerResponse.adPlacements, undefined);
const throwing = {...clone(data)};
Object.defineProperty(throwing, 'pageGetter', {enumerable: true, get() {throw new Error('page getter');}});
assert.doesNotThrow(() => {context.ytInitialPlayerResponse = throwing;}, 'Page getters must not break player assignment');
assert.deepEqual(context.ytInitialPlayerResponse.videoDetails, data.videoDetails);
const nested = {playerResponse: clone(data), wide: Array.from({length: 5000}, () => ({}))};
context.ytInitialPlayerResponse = nested;
assert.equal(nested.playerResponse.adPlacements, undefined, 'Known player containers precede wide siblings');
const wideRoot = {playerResponse: clone(data),
  ...Object.fromEntries(Array.from({length: 15000}, (_, i) => [`sibling${i}`, {}]))};
context.ytInitialPlayerResponse = wideRoot;
assert.equal(wideRoot.playerResponse.adPlacements, undefined, 'Player containers precede wide root enumeration');
const identity = nested.playerResponse;
identity.adPlacements = [1];
assert.equal(identity.adPlacements, undefined, 'Retained references cannot reintroduce an ad field');
assert.equal(nested.playerResponse, identity, 'Player object identity is preserved');
nested.playerResponse = clone(data);
assert.equal(nested.playerResponse.playerAds, undefined, 'Replacing a nested player cleans the replacement');
nested.playerResponse.adSlots = [1];
assert.equal(nested.playerResponse.adSlots, undefined);
const cycle = clone(data); cycle.self = cycle;
assert.doesNotThrow(() => {context.ytInitialPlayerResponse = cycle;});
const frozen = Object.freeze({adPlacements: [1], content: 'retained'});
assert.doesNotThrow(() => {context.ytInitialPlayerResponse = frozen;});
assert.equal(context.ytInitialPlayerResponse, frozen, 'Frozen data is preserved without throwing');
let ownChecks = 0;
const inherited = Object.fromEntries(Array.from({length: 20000}, (_, i) => [`inherited${i}`, {}]));
const inheritedProxy = new Proxy(Object.create(inherited), {
  getOwnPropertyDescriptor(target, key) {++ownChecks; return Reflect.getOwnPropertyDescriptor(target, key);}
});
context.ytInitialPlayerResponse = {playerResponse: clone(data), inheritedProxy};
assert.ok(ownChecks < 21000, 'Inherited properties consume traversal budget');
assert.equal(context.ytInitialPlayerResponse.playerResponse.playerAds, undefined);
const response = new FixtureResponse(JSON.stringify(data), {status: 201, headers: {'X-Fixture': 'retained'}});
Object.defineProperty(response, 'url', {value: 'https://www.youtube.com/youtubei/v1/player'});
const parsed = await response.json();
assert.equal(parsed.playerAds, undefined); assert.deepEqual(parsed.videoDetails, data.videoDetails);
assert.equal(response.status, 201); assert.equal(response.headers.get('X-Fixture'), 'retained');
assert.equal(response.bodyUsed, true); await assert.rejects(() => response.json());
const external = new FixtureResponse(JSON.stringify(data));
Object.defineProperty(external, 'url', {value: 'https://other.test/youtubei/v1/player'});
assert.deepEqual(await external.json(), data, 'External endpoint is preserved');
const textResponse = new FixtureResponse(JSON.stringify(data));
Object.defineProperty(textResponse, 'url', {value: 'https://www.youtube.com/youtubei/v1/next?key=fixture'});
const textData = JSON.parse(await textResponse.text());
assert.equal(textData.playerAds, undefined);
assert.deepEqual(textData.streamingData, data.streamingData);
await assert.rejects(() => textResponse.text(), 'Text body remains single-use');
const malformed = '{"adPlacements": [invalid JSON';
const malformedResponse = new FixtureResponse(malformed);
Object.defineProperty(malformedResponse, 'url', {value: 'https://www.youtube.com/youtubei/v1/player'});
assert.equal(await malformedResponse.text(), malformed, 'Malformed text is preserved');
const unrelated = new FixtureResponse(JSON.stringify(data));
Object.defineProperty(unrelated, 'url', {value: 'https://www.youtube.com/youtubei/v1/browse'});
assert.deepEqual(await unrelated.json(), data, 'Unrelated YouTube endpoint is preserved');
const xhr = new FixtureXHR(JSON.stringify(data));
assert.equal(JSON.parse(xhr.responseText).adPlacements, undefined);
const jsonXHR = new FixtureXHR(clone(data), 'json');
assert.equal(jsonXHR.response.playerAds, undefined);
assert.throws(() => jsonXHR.responseText, 'Native getter errors are preserved');
const removeAds = value => {
  if (!value || typeof value !== 'object') return value;
  if (Array.isArray(value)) return value.map(removeAds);
  return Object.fromEntries(Object.entries(value)
    .filter(([key]) => !['adPlacements', 'playerAds', 'adSlots', 'adBreakHeartbeatParams'].includes(key))
    .map(([key, child]) => [key, removeAds(child)]));
};
const textCases = [
  '{"keep":9007199254740993,"adPlacements":[1],"tiny":1.2300e-99,"escaped":"\\u0041"}',
  '{"adPlacements":{"adSlots":[1]},"keep":1}',
  '{"keep":1,"adPlacements":[1],"playerAds":[2]}',
  '{"adPlacements":[1],"playerAds":[2]}',
  '{"\\u0061dPlacements":[1],"keep":"comma, quote\\\" and braces{}"}',
  '[{"adPlacements":[1]}, {"keep":true,"adSlots":[]}, null, "adSlots"]',
  '{"keep":1,"keep":2,"adSlots":[],"child":{"adSlots":[],"keep":3}}',
  ...Array.from({length: 64}, (_, mask) => JSON.stringify(Object.fromEntries(
    Array.from({length: 6}, (_, i) => [mask & (1 << i) ?
      ['adPlacements', 'playerAds', 'adSlots', 'adBreakHeartbeatParams'][i % 4] : `keep${i}`,
      {child: [{adSlots: [i], keep: i}]}]))))
];
for (const text of textCases) {
  const response = new FixtureResponse(text);
  Object.defineProperty(response, 'url', {value: 'https://www.youtube.com/youtubei/v1/player'});
  const result = await response.text();
  assert.deepEqual(JSON.parse(result), removeAds(JSON.parse(text)), `Member removal preserves valid JSON: ${text}`);
  const xhr = new FixtureXHR(text);
  assert.equal(xhr.responseText, result);
  assert.equal(xhr.responseText, result, 'Repeated XHR reads are stable');
}
const lossless = new FixtureXHR(textCases[0]).responseText;
assert.ok(lossless.includes('9007199254740993'));
assert.ok(lossless.includes('1.2300e-99'));
assert.ok(lossless.includes('"\\u0041"'));
const changingXHR = new FixtureXHR('{"adSlots":[],"keep":1}');
assert.equal(JSON.parse(changingXHR.responseText).keep, 1);
changingXHR.value = '{"adSlots":[],"keep":2}';
assert.equal(JSON.parse(changingXHR.responseText).keep, 2, 'XHR cache invalidates when content changes');
changingXHR.responseURL = 'https://other.test/youtubei/v1/player';
assert.equal(changingXHR.responseText, changingXHR.value, 'XHR cache respects endpoint changes');
const deep = '{"adPlacements":[],"child":' + '['.repeat(130) + '0' + ']'.repeat(130) + '}';
assert.equal(new FixtureXHR(deep).responseText, deep, 'Work-limit fallback preserves the complete original text');
class FrozenResponse extends Response {}
Object.defineProperty(FrozenResponse.prototype, 'json', {value: Response.prototype.json, configurable: false});
class FrozenXHR extends FixtureXHR {}
Object.freeze(FrozenXHR.prototype);
assert.doesNotThrow(() => vm.runInContext(script, vm.createContext({
  Response: FrozenResponse, XMLHttpRequest: FrozenXHR, URL, location: context.location
})), 'Frozen Web API descriptors do not interrupt installation');
const original = context.Response.prototype.json;
vm.runInContext(script, context);
assert.equal(context.Response.prototype.json, original, 'Repeated SPA installation is idempotent');
console.log('YouTube adapter passed: player mutation/identity, traversal budgets, lossless JSON member removal (71 cases), XHR cache, endpoint scope, body lifecycle, frozen APIs and repeated installation.');
await import('./test-youtube-content-blocking-extended.mjs');
