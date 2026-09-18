#!/usr/bin/env node
// Native Yee with real tabs and an owned HTTP server. Requires all Yee browser
// processes to be gracefully shut down first, as required by AGENTS.md.
import assert from 'node:assert/strict';
import {execFile, spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {promisify} from 'node:util';
import {closeOwnedBrowser, connectCDP, pause, requireShutdown} from './content_blocking_test_runtime.mjs';

const root = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const executable = process.argv[2];
if (!executable) throw new Error('Usage: node tools/dev/test-content-blocking.mjs /absolute/path/to/Yee');
assert.equal(process.platform, 'darwin', 'This native bundle fixture currently supports macOS');
const fixture = await fs.readFile(path.join(root, 'tests/fixtures/content-blocking/fixture.html'));
let requests = [];
const server = http.createServer((request, response) => {
  requests.push({host: request.headers.host, method: request.method, url: request.url});
  response.setHeader('Access-Control-Allow-Origin', '*');
  response.setHeader('Access-Control-Expose-Headers', 'X-Yee-Fixture');
  if (request.url === '/redirect-block' || request.url === '/redirect-frame') {
    response.writeHead(302, {Location: `http://yee-block.test:${server.address().port}/redirect-target`});
    response.end();
  } else if (request.url === '/worker.js') {
    response.setHeader('Content-Type', 'application/javascript');
    response.end(`fetch('http://yee-block.test:${server.address().port}/worker-fetch').then(() => postMessage(false), () => postMessage(true));`);
  } else if (request.url === '/content') {
    response.writeHead(201, {'Content-Type': 'text/plain', 'X-Yee-Fixture': 'preserved'});
    response.end('allowed response');
  } else if (request.url === '/fixture') {
    response.setHeader('Content-Type', 'text/html'); response.end(fixture);
  } else if (request.url === '/csp-frame') {
    response.setHeader('Content-Type', 'text/html');
    response.setHeader('Content-Security-Policy', "script-src 'none'; style-src 'none'");
    response.end('<!doctype html><div id="csp-ad" class="yee-generic-ad">CSP advertisement</div><img src="/csp-image">');
  } else response.end('unfiltered fixture response');
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));

async function requireYeeShutdown() {
  const {stdout} = await promisify(execFile)('python3',
    [path.join(root, 'tools/dev/browser_bundle_executables.py'), '--running']);
  requireShutdown(JSON.parse(stdout));
}
async function prepareShutdownHelper() {
  const source = path.join(root, 'tools/dev/gracefully-quit-yee.swift');
  const directory = path.join(root, '.local-build/yee-shutdown-helper');
  const binary = path.join(directory, 'gracefully-quit-yee');
  const cache = path.join(directory, 'module-cache');
  await fs.mkdir(cache, {recursive: true});
  let current = false;
  try {current = (await fs.stat(binary)).mtimeMs >= (await fs.stat(source)).mtimeMs;} catch {}
  if (!current) await promisify(execFile)('xcrun',
    ['swiftc', '-module-cache-path', cache, source, '-o', binary], {timeout: 60000});
  return binary;
}

const evidence = [];
let passed = false, failure;
try {
  await requireYeeShutdown();
  const shutdownHelper = await prepareShutdownHelper();
  for (const mode of ['off', 'on', 'site-exception']) {
    await requireYeeShutdown();
    requests = [];
    const profile = await fs.mkdtemp(path.join(os.tmpdir(), 'yee-blocking-'));
    const log = await fs.open(path.join(profile, 'browser.log'), 'w');
    const extra = mode === 'off' ? ['--disable-features=YeeContentBlocking'] :
      mode === 'site-exception' ? ['--yee-content-blocking-disabled-sites=yee-fixture.test'] : [];
    const browser = spawn(executable, [`--user-data-dir=${profile}`, '--no-first-run',
      '--no-default-browser-check', '--remote-debugging-port=0', '--no-proxy-server',
      '--disable-background-networking', '--disable-component-update',
      '--yee-content-blocking-test-rules',
      '--host-resolver-rules=MAP *.test 127.0.0.1', ...extra,
      `http://yee-fixture.test:${server.address().port}/fixture`], {stdio: ['ignore', log.fd, log.fd]});
    let browserCDP;
    let tabCDP;
    try {
      await new Promise((resolve, reject) => {browser.once('spawn', resolve); browser.once('error', reject);});
      let port;
      for (let attempt = 0; attempt < 150; ++attempt) {
        if (browser.exitCode !== null || browser.signalCode !== null)
          throw new Error(`Yee exited: ${browser.exitCode ?? browser.signalCode}; ${profile}/browser.log`);
        try {port = Number((await fs.readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]); break;}
        catch {await pause(100);}
      }
      assert.ok(port, `DevTools startup: ${profile}`);
      const version = await (await fetch(`http://127.0.0.1:${port}/json/version`, {signal: AbortSignal.timeout(15000)})).json();
      browserCDP = await connectCDP(version.webSocketDebuggerUrl);
      let tab;
      for (let attempt = 0; attempt < 150; ++attempt) {
        const tabs = await (await fetch(`http://127.0.0.1:${port}/json/list`, {signal: AbortSignal.timeout(15000)})).json();
        tab = tabs.find(tab => tab.type === 'page' && tab.url.includes('yee-fixture.test'));
        if (tab) break;
        await pause(100);
      }
      assert.ok(tab, 'Actual fixture tab');
      tabCDP = await connectCDP(tab.webSocketDebuggerUrl);
      const evaluation = {expression: `(async () => {
        for(let i=0;i<100;i++){if(window.results?.done)return window.results;await new Promise(r=>setTimeout(r,100));}
        throw new Error('Fixture timeout: '+JSON.stringify(window.results));
      })()`, awaitPromise: true, returnByValue: true};
      let evaluated;
      for (let attempt = 0; attempt < 5; ++attempt) {
        try {evaluated = await tabCDP.call('Runtime.evaluate', evaluation); break;}
        catch (error) {
          if (!error.message.includes('Execution context was destroyed') || attempt === 4)
            throw error;
          await pause(200);
        }
      }
      assert.ok(!evaluated.exceptionDetails, JSON.stringify(evaluated.exceptionDetails));
      const result = evaluated.result.value;
      const protectedMode = mode === 'on';
      assert.equal(result.beforeFirstInline, protectedMode, 'Native scriptlet before first inline script');
      assert.equal(result.initialBlankBeforeReturn, protectedMode, 'Initial blank realm installed before append returns');
      assert.equal(result.replacementBody, protectedMode ? '/* Yee empty replacement. */' : 'unfiltered fixture response');
      assert.equal(result.cleanedURL, `http://yee-query.test:${server.address().port}/resource?${protectedMode ? '' : 'tracking=1&'}keep=2`);
      assert.equal(result.cleanedRedirected, protectedMode);
      assert.equal(result.cleanedBody, 'unfiltered fixture response');
      assert.equal(result.fetchBlocked, protectedMode, 'Document fetch');
      assert.equal(result.redirectBlocked, protectedMode, 'Redirect');
      assert.equal(result.workerBlocked, protectedMode, 'Dedicated worker fetch');
      assert.equal(result.allowedStatus, 201); assert.equal(result.allowedHeader, 'preserved');
      assert.equal(result.allowedBody, 'allowed response');
      assert.equal(result.pingFetchStatus, 200, 'Ping-only rule preserves fetch');
      assert.equal(result.iframeFetchStatus, 200, 'Subdocument-only rule preserves fetch');
      for (const key of ['inheritedAd', 'blankAd', 'cspAd'])
        assert.equal(result[key], protectedMode ? 'none' : 'block', key);
      for (const id of ['site-ad', 'dynamic', 'late', 'overflow']) assert.equal(result[id], protectedMode ? 'none' : 'block', id);
      assert.notEqual(result.exception, 'none', 'Generic cosmetic exception');
      const blockedHits = requests.filter(request => request.host.startsWith('yee-block.test:'));
      const replacementHits = requests.filter(request => request.host.startsWith('yee-redirect.test:'));
      assert.equal(replacementHits.length === 0, protectedMode, 'Local redirect resource prevents an advertising network request');
      const queryHits = requests.filter(request => request.host.startsWith('yee-query.test:'));
      assert.equal(queryHits.length, 1);
      assert.equal(queryHits[0].url, `/resource?${protectedMode ? '' : 'tracking=1&'}keep=2`, 'Only the effective query reaches Network Service');
      assert.equal(blockedHits.length === 0, protectedMode, 'Server received no blocked requests');
      const pingHits = requests.filter(request => request.host.startsWith('yee-ping.test:') && request.url === '/typed-beacon');
      assert.equal(pingHits.length === 0, protectedMode, 'Ping-only rule prevents the beacon reaching the server');
      const frameHits = requests.filter(request => request.host.startsWith('yee-frame-type.test:') && request.url === '/iframe-type');
      assert.equal(frameHits.length === 0, protectedMode, 'Navigation factory enforces subdocument rules');
      const cspImageHits = requests.filter(request => request.url === '/csp-image');
      assert.equal(cspImageHits.length === 0, protectedMode, 'Filter CSP is enforced alongside the original server CSP');
      // The current publisher exception does not disable protection for a new
      // top-level destination. Page.navigate reports Chromium's blocked page.
      const mainNavigation = await tabCDP.call('Page.navigate', {
        url: `http://yee-block.test:${server.address().port}/main-navigation`
      });
      assert.equal(mainNavigation.errorText?.includes('ERR_BLOCKED_BY_CLIENT') ?? false,
        mode !== 'off', 'Top-level navigation protection uses the destination site');
      const mainHits = requests.filter(request => request.url === '/main-navigation');
      assert.equal(mainHits.length === 0, mode !== 'off', 'Blocked top-level request never reaches the server');
      evidence.push({mode, profile, results: result, mainNavigation,
        blockedServerRequests: blockedHits, pingServerRequests: pingHits, frameServerRequests: frameHits,
        mainServerRequests: mainHits, cspImageServerRequests: cspImageHits});
      console.log(JSON.stringify(evidence.at(-1)));
    } finally {
      tabCDP?.close();
      try {
        await closeOwnedBrowser(browser, browserCDP, async pid => {
          await promisify(execFile)(shutdownHelper, [executable, `--pid=${pid}`], {timeout: 20000});
        });
      } finally {await log.close();}
    }
  }
  passed = true;
} catch (error) {
  failure = error.message;
  throw error;
} finally {
  await new Promise(resolve => server.close(resolve));
  const output = path.join(root, '.local-build/yee-content-blocking-browser-evidence.json');
  await fs.writeFile(output, JSON.stringify({schema: 'yee.content-blocking-browser.v2', passed,
    failure, evidence}, null, 2) + '\n');
}
