#!/usr/bin/env node
import assert from 'node:assert/strict';
import {classifyProfileScript, summarizeCPUWindow} from './performance_cpu.mjs';

const frame = (functionName, scriptId, url = '') =>
  ({functionName, scriptId, url, lineNumber: 0, columnNumber: 0});
const profile = {startTime: 1000000, endTime: 1014000, nodes: [
  {id: 1, callFrame: frame('(root)', '0'), children: [2, 5]},
  {id: 2, callFrame: frame('page', 'page', 'https://page.test/main.js'), children: [3]},
  {id: 3, callFrame: frame('clean', 'yee'), children: [4]},
  {id: 4, callFrame: frame('callback', 'page', 'https://page.test/main.js')},
  {id: 5, callFrame: frame('(garbage collector)', '0')},
], samples: [2, 3, 4, 5], timeDeltas: [1000, 2000, 3000, 4000]};
const scripts = new Map([['yee', {owner: 'yee-youtube'}]]);
const clipped = summarizeCPUWindow(profile, scripts, 1001500, 1007000);
assert.equal(clipped.sampledMs, 5.5, 'Both boundary intervals are clipped');
assert.deepEqual(clipped.selfByOwner, {page: 2.5, 'yee-youtube': 3});
assert.equal(clipped.injectedStackMs, 4, 'Nested page callbacks count once in injected stacks');
assert.equal(clipped.topInclusive.find(row => row.function === 'clean').inclusiveMs, 4);
const brave = summarizeCPUWindow(profile, new Map([['yee', {owner: 'brave-scriptlets'}]]),
  1001500, 1007000);
assert.equal(brave.injectedStackMs, clipped.injectedStackMs,
  'Verified Brave injected sources use the same stack accounting');
assert.equal(brave.injectedSelf[0].owner, 'brave-scriptlets');
assert.equal(summarizeCPUWindow(profile, scripts, 1000000, 1014000).sampledMs, 12);
assert.equal(summarizeCPUWindow(profile, scripts, 2000000, 2001000).sampledMs, 0);
assert.throws(() => summarizeCPUWindow({...profile, timeDeltas: []}, scripts, 0, 1));
const reordered = summarizeCPUWindow({...profile, samples: [2, 4, 3, 5],
  timeDeltas: [1000, 5000, -3000, 7000]}, scripts, 1001500, 1007000);
assert.deepEqual(reordered.selfByOwner, clipped.selfByOwner, 'Out-of-order deltas preserve paired samples');
assert.equal(reordered.reorderedSamples, 2);
const named = structuredClone(profile);named.nodes[1].callFrame.functionName='constructor';
assert.equal(summarizeCPUWindow(named,scripts,1000000,1003000).topSelf[0].owner,'page',
  'JavaScript function names cannot collide with runtime classification keys');
const owned = {'yee-youtube': 'exact owned source'};
assert.equal(classifyProfileScript('wrapper exact owned source suffix', '', owned), 'yee-youtube');
assert.equal(classifyProfileScript('function clean() {}', '', owned), 'unresolved', 'Names alone do not prove ownership');
assert.equal(classifyProfileScript('window.__yeeTransition={}', '', owned), 'collector');
assert.equal(classifyProfileScript('page code', 'https://page.test/main.js', owned), 'page');
console.log('CPU source identity, window clipping and exclusive/inclusive attribution checks passed');
