#!/usr/bin/env python3
"""Real Chromium Web API fixture in an isolated Chrome or visible Yee tab."""
from pathlib import Path
import argparse
import json
import os
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--yee', action='store_true', help='Use the newly built, visible Yee app')
parser.add_argument('--response-only', action='store_true', help='Focus on body readers, clone and error semantics')
options = parser.parse_args()
yee_executable = ROOT / '.local-build/chromium/src/out/YeePilot/Yee.app/Contents/MacOS/Yee'
if options.yee:
    subprocess.run(['zsh', '-c', 'set -e; source "$1"; require_integrated_yee_app_current; gracefully_quit_yee',
                    'yee-web-api-fixture', str(ROOT / 'tools/dev/common.zsh')], check=True)
scripts = {
    "youtube": (ROOT / "renderer/content_blocking/youtube.js").read_text(),
    "validation": (ROOT / "renderer/content_blocking/selector_validation.js").read_text(),
    "video": next(resource['content'] for resource in json.loads(
        (ROOT / 'components/content_blocking/data/resources.json').read_text())
        if resource['name'] == 'yee-blank.mp4'),
    "actualYee": options.yee,
    "responseOnly": options.response_only,
}
probe = r"""
(async () => {
  const results = {actualYee: config.actualYee, actualChromiumWebAPIs: true,
    installation: 'fixture source in actual browser tab', assertions: []};
  const check = (condition, name) => {if (!condition) throw Error(name); results.assertions.push(name);};
  const nativeFetch = window.fetch;
  const nativeClone = Response.prototype.clone;
  const raw = '{"keep":9007199254740993,"adSlots":[1],"content":"\\u0041"}';
  const wanted = '{"keep":9007199254740993,"content":"\\u0041"}';
  const endpoint = 'https://www.youtube.com/youtubei/v1/player';
  window.fetch = async () => {
    const response = new Response(raw, {status: 201, headers: {'X-Fixture': 'retained'}});
    Object.defineProperty(response, 'url', {value: endpoint}); return response;
  };
  window.fixtureScans = 0;
  const countedSource = config.youtube.replace('function cleanText(text, changes = null, inspection = null) {',
    'function cleanText(text, changes = null, inspection = null) { globalThis.fixtureScans++;');
  check(countedSource !== config.youtube, 'diagnostic scanner counter attaches');
  (new Function('yeeDocumentUrl', countedSource))('https://www.youtube.com/');
  for (const api of ['text', 'arrayBuffer', 'blob', 'reader', 'clone', 'json']) {
    const response = await fetch(endpoint); let text;
    if (api === 'arrayBuffer') text = new TextDecoder().decode(await response.arrayBuffer());
    else if (api === 'blob') text = await (await response.blob()).text();
    else if (api === 'reader') {
      const reader = response.body.getReader(); const decoder = new TextDecoder(); text = '';
      for (;;) {const part = await reader.read(); if (part.done) break; text += decoder.decode(part.value, {stream: true});}
      text += decoder.decode(); reader.releaseLock();
    } else if (api === 'clone') {const copy = response.clone(); check(copy.url === endpoint && copy.clone().url === endpoint, 'nested clone keeps response URL'); text = await copy.text(); check(await response.text() === wanted, 'clone leaves original clean');}
    else if (api === 'json') check((await response.json()).adSlots === undefined, 'json strips ad data');
    else text = await response.text();
    if (text !== undefined) check(text === wanted, api + ' preserves non-ad bytes');
    check(response.status === 201 && response.headers.get('X-Fixture') === 'retained', api + ' preserves response metadata');
    check(response.bodyUsed, api + ' consumes body once');
  }
  check(fixtureScans === 6, 'six fetch inspections cover all native body readers and nested clones');
  const response = await fetch(endpoint); await response.text();
  try {await response.text(); check(false, 'consumed text rejects');}
  catch (error) {check(error instanceof TypeError, 'consumed text retains native error');}
  try {response.clone(); check(false, 'consumed clone rejects');}
  catch (error) {check(error instanceof TypeError, 'consumed clone retains native error');}
  const child = document.createElement('iframe'); document.body.appendChild(child);
  const realm = child.contentWindow;
  realm.fetch = async () => {
    const response = new Response(wanted);
    Object.defineProperty(response, 'url', {value: endpoint}); return response;
  };
  // A native method borrowed from the parent realm returns its prototype.
  Object.defineProperty(realm.Response.prototype, 'clone', {configurable: true,
    writable: true, value: nativeClone});
  realm.fixtureScans = 0;
  (new realm.Function('yeeDocumentUrl', countedSource))('https://www.youtube.com/');
  const checked = await realm.fetch(endpoint);
  const copy = realm.Response.prototype.clone.call(checked), nested = copy.clone();
  check(Object.getPrototypeOf(copy) !== realm.Response.prototype, 'native clone can return another realm prototype');
  for (const response of [checked, copy, nested]) {
    if (!response.url) Object.defineProperty(response, 'url', {value: endpoint});
    check(await realm.Response.prototype.text.call(response) === wanted, 'clean cross-realm body keeps bytes');
  }
  check(realm.fixtureScans === 1, 'clean cross-realm nested clones reuse completed inspection');
  const borrowed = new realm.Response(raw); Object.defineProperty(borrowed, 'url', {value: endpoint});
  check((await borrowed.json()).adSlots === undefined, 'independently installed initial blank realm cleans borrowed native Response');
  if (config.responseOnly) {
    document.getElementById('result').textContent = JSON.stringify(results); return;
  }
  const selectors = ['.broken[', '.safe-ad', 'div:has(.nested)', '.safe-ad-two'];
  // Exact return parentheses used by the native C++ embedder: leading source
  // comments must not trigger automatic semicolon insertion after return.
  const valid = new Function('selectors', 'return (\n' + config.validation + '\n);')(selectors);
  check(!valid.includes('.broken[') && valid.includes('.safe-ad'), 'malformed CSS is rejected individually');
  const style = document.createElement('style');
  style.textContent = valid.map(selector => selector + '{display:none!important;}\n').join('');
  document.head.appendChild(style);
  for (const name of ['safe-ad', 'safe-ad-two']) {
    const element = document.createElement('div'); element.className = name; document.body.appendChild(element);
    check(getComputedStyle(element).display === 'none', 'valid following CSS applies: ' + name);
  }
  window.fetch = nativeFetch;
  const video = document.createElement('video'); video.preload = 'metadata';
  const loaded = new Promise((resolve, reject) => {video.onloadedmetadata = resolve; video.onerror = () => reject(Error('Blank replacement MP4 cannot decode'));});
  video.src = 'data:video/mp4;base64,' + config.video; document.body.appendChild(video); await loaded;
  check(video.duration === 1 && video.videoWidth === 32 && video.videoHeight === 32, 'generated replacement MP4 loads in Chromium');
  video.muted = true;
  await new Promise((resolve, reject) => {
    const finish = error => {clearTimeout(timer); error ? reject(error) : resolve();};
    const timer = setTimeout(() => finish(Error('Replacement MP4 playback timeout')), 8000);
    video.onended = () => finish();
    video.onerror = () => finish(Error('Replacement MP4 decode error: ' + video.error?.code));
    video.play().catch(finish);
  });
  check(video.ended && video.getVideoPlaybackQuality().totalVideoFrames > 0,
    'replacement MP4 decodes frames and reaches its end');
  document.getElementById('result').textContent = JSON.stringify(results);
})().catch(error => {document.getElementById('result').textContent = JSON.stringify({error: error.stack});});
"""
with tempfile.TemporaryDirectory(prefix="yee-blocking-fixture-") as directory:
    path = Path(directory) / "fixture.html"
    path.write_text('<!doctype html><meta charset="utf-8"><body><pre id="result">pending</pre><script>const config=' +
                    json.dumps(scripts).replace('<', '\\u003c') + ';</script><script>' +
                    probe.replace('</script', '<\\/script') + '</script>')
    command = [str(yee_executable) if options.yee else '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
               *([] if options.yee else ['--headless=new', '--disable-gpu', '--no-sandbox']), '--no-first-run',
               '--no-default-browser-check', '--disable-background-networking',
               '--disable-component-update', '--disable-sync', '--disable-extensions',
               '--disable-features=OptimizationHints', '--user-data-dir=' + directory + '/profile',
               '--remote-debugging-port=0', path.as_uri()]
    diagnostics = tempfile.TemporaryFile(mode='w+')
    process = subprocess.Popen(command, stdout=diagnostics, stderr=diagnostics,
                               text=True, start_new_session=True)
    bounded_shutdown = False
    try:
        probe_result = subprocess.run(
            ['node', str(ROOT / 'tools/dev/read-content-blocking-fixture.mjs'),
             directory + '/profile/DevToolsActivePort', path.as_uri()],
            capture_output=True, text=True, timeout=40)
        if probe_result.returncode:
            raise RuntimeError(probe_result.stderr)
        result = json.loads(probe_result.stdout)
    finally:
        bounded_shutdown = True
        if options.yee and process.poll() is None:
            subprocess.run(['zsh', '-c', 'source "$1"; gracefully_quit_yee', 'yee-web-api-fixture',
                            str(ROOT / 'tools/dev/common.zsh')], check=True, timeout=30)
            process.wait(timeout=5)
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
        diagnostics.close()
    assert 'error' not in result, result
    assert len(result['assertions']) == (30 if options.response_only else 35), result
    result['scope'] = 'response' if options.response_only else 'response-css-media'
    result['boundedShutdown'] = bounded_shutdown
    output = ROOT / ('.local-build/content-blocking-yee-web-api-fixture.json' if options.yee else
                     '.local-build/content-blocking-browser-fixture.json')
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
