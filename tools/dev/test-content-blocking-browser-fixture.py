#!/usr/bin/env python3
"""Real Chromium Web API fixture in a temporary Chrome profile, without Yee."""
from pathlib import Path
import json
import os
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
scripts = {
    "youtube": (ROOT / "renderer/content_blocking/youtube.js").read_text(),
    "validation": (ROOT / "renderer/content_blocking/selector_validation.js").read_text(),
    "video": next(resource['content'] for resource in json.loads(
        (ROOT / 'components/content_blocking/data/resources.json').read_text())
        if resource['name'] == 'yee-blank.mp4'),
}
probe = r"""
(async () => {
  const results = {actualYee: false, actualChromiumWebAPIs: true, assertions: []};
  const check = (condition, name) => {if (!condition) throw Error(name); results.assertions.push(name);};
  const nativeFetch = window.fetch;
  const raw = '{"keep":9007199254740993,"adSlots":[1],"content":"\\u0041"}';
  const wanted = '{"keep":9007199254740993,"content":"\\u0041"}';
  const endpoint = 'https://www.youtube.com/youtubei/v1/player';
  window.fetch = async () => {
    const response = new Response(raw, {status: 201, headers: {'X-Fixture': 'retained'}});
    Object.defineProperty(response, 'url', {value: endpoint}); return response;
  };
  (new Function('yeeDocumentUrl', config.youtube))('https://www.youtube.com/');
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
  const child = document.createElement('iframe'); document.body.appendChild(child);
  const realm = child.contentWindow;
  (new realm.Function('yeeDocumentUrl', config.youtube))('https://www.youtube.com/');
  const borrowed = new realm.Response(raw); Object.defineProperty(borrowed, 'url', {value: endpoint});
  check((await borrowed.json()).adSlots === undefined, 'independently installed initial blank realm cleans borrowed native Response');
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
  document.getElementById('result').textContent = JSON.stringify(results);
})().catch(error => {document.getElementById('result').textContent = JSON.stringify({error: error.stack});});
"""
with tempfile.TemporaryDirectory(prefix="yee-blocking-fixture-") as directory:
    path = Path(directory) / "fixture.html"
    path.write_text('<!doctype html><meta charset="utf-8"><body><pre id="result">pending</pre><script>const config=' +
                    json.dumps(scripts).replace('<', '\\u003c') + ';</script><script>' +
                    probe.replace('</script', '<\\/script') + '</script>')
    command = ['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
               '--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
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
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
        diagnostics.close()
    assert 'error' not in result, result
    assert len(result['assertions']) == 25, result
    result['boundedShutdown'] = bounded_shutdown
    output = ROOT / '.local-build/content-blocking-browser-fixture.json'
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
