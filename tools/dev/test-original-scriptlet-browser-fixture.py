#!/usr/bin/env python3
"""Execute actual Rust-engine scriptlet output in isolated Chrome fixtures."""
from pathlib import Path
import json
import os
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
scripts = json.loads(Path(sys.argv[1]).read_text())
config = {"original": scripts,
          "yee": (ROOT / "renderer/content_blocking/youtube.js").read_text()}
probe = r"""
(async () => {
  const results = {actualYee: false, actualChromiumWebAPIs: true,
                   engineGeneratedOriginalScriptlets: true, assertions: []};
  const check = (condition, name) => {if (!condition) throw Error(name); results.assertions.push(name);};
  function make(host, withYee = false) {
    const child = document.createElement('iframe'); document.body.append(child);
    const realm = child.contentWindow;
    realm.eval(`class FixtureXHR {
      open(method, url) {this.method = method; this.url = url;}
      send(body) {this.sent = body;}
      get response() {return this.value;}
      get responseText() {return this.value;}
    }; window.XMLHttpRequest = FixtureXHR;`);
    realm.errors = [];
    realm.addEventListener('error', event => realm.errors.push(event.message));
    realm.fetch = realm.Function(`return async function(url) {
      const response = new Response(window.raw, {status: 201, headers: {'X-Fixture':'kept'}});
      Object.defineProperty(response, 'url', {value: window.endpoint}); return response;
    }`)();
    if (host === 'www.youtube.com') {
      const masthead = realm.document.createElement('div'); masthead.id = 'masthead';
      masthead.setAttribute('logo-type', 'NORMAL'); realm.document.body.appendChild(masthead);
      realm.ytcfg = {data_:{INNERTUBE_CONTEXT:{client:{userAgent:'Mozilla/5.0 (ordinary)',clientName:'WEB'}}}};
      const node = realm.document.createElement('script'); node.type = 'text/plain';
      node.id = 'contract-fixture';
      node.textContent = '(function serverContract(){window.contractFixture = 7;})();';
      realm.document.body.appendChild(node);
    }
    if (withYee) realm.Function('yeeDocumentUrl', config.yee)(`https://${host}/watch?v=content`);
    realm.eval(config.original[host]);
    return realm;
  }
  const generic = make('fixture.test');
  check(generic.runtimeContract === true && make('fixture.test').runtimeContract === true,
        'engine-generated shared-state contract executes with independent frame scopes');
  check(generic.fixtureFlag === true, 'original set-constant and dependency graph execute');
  check(generic.JSON.parse('{"adSlots":[1],"content":2}').adSlots === undefined,
        'original json-prune modifies parsed advertising fields');
  const edited = new generic.XMLHttpRequest(); edited.open('POST', 'https://fixture.test/player');
  edited.send('{"context":{"client":{"clientName":"WEB"}},"keep":3}');
  check(JSON.parse(edited.sent).context.client.clientScreen === 'CHANNEL',
        'original trusted JSONPath request edit executes');
  const unedited = new generic.XMLHttpRequest(); unedited.open('POST', 'https://fixture.test/browse');
  const untouched = '{"context":{"client":{"clientName":"WEB"}},"keep":3}'; unedited.send(untouched);
  check(unedited.sent === untouched, 'original trusted request edit preserves unmatched endpoints');
  // Run the original observer while a real srcdoc parser is still loading;
  // the completed about:blank fixtures above only cover the initial DOM scan.
  for (const withYee of [false, true]) {
    const child = document.createElement('iframe');
    const loaded = new Promise(resolve => child.onload = resolve);
    const source = '(function serverContract(){window.loadingContract = 17;})();';
    const encoded = value => JSON.stringify(value).replaceAll('<', '\\u003c');
    child.srcdoc = '<!doctype html><body><div id="masthead" logo-type="NORMAL"></div><script>' +
      'window.errors=[]; addEventListener("error", e=>errors.push(e.message));' +
      'window.startedWhileLoading=document.readyState==="loading";' +
      'window.ytcfg={data_:{INNERTUBE_CONTEXT:{client:{userAgent:"Mozilla/5.0 (ordinary)",clientName:"WEB"}}}};' +
      (withYee ? 'Function("yeeDocumentUrl",' + encoded(config.yee) + ')("https://www.youtube.com/watch?v=normal");' : '') +
      'eval(' + encoded(config.original['www.youtube.com']) + ');' +
      'document.addEventListener("DOMContentLoaded",()=>{const node=document.getElementById("loading-contract");' +
      'window.observedSource=node.textContent;const execute=document.createElement("script");' +
      'execute.textContent=node.textContent;document.body.appendChild(execute);});' +
      '</script><script type="text/plain" id="loading-contract">' + source + '</script>';
    document.body.appendChild(child); await loaded;
    const realm = child.contentWindow, label = 'loading parser original' + (withYee ? ' + Yee' : '');
    check(realm.startedWhileLoading, label + ' observer installed before interactive');
    check(realm.observedSource !== source && realm.observedSource.includes('onAbnormalityDetected'),
          label + ' observer rewrites parser-added serverContract node');
    check(realm.loadingContract === 17, label + ' dynamic rewrite retains original statement');
    const late = realm.document.createElement('script'); late.type = 'text/plain'; late.textContent = source;
    realm.document.body.appendChild(late); await Promise.resolve();
    check(late.textContent === source, label + ' observer stops after interactive');
    check(realm.errors.length === 0, label + ' no asynchronous script errors');
  }
  const hosts = ['www.youtube.com', 'm.youtube.com', 'music.youtube.com', 'tv.youtube.com',
                 'www.youtube-nocookie.com', 'www.youtubekids.com'];
  for (const host of hosts) {
    for (const withYee of [false, true]) {
      const realm = make(host, withYee), label = host + (withYee ? ' original + Yee' : ' original');
      realm.ytInitialPlayerResponse = realm.JSON.parse('{"videoDetails":{"videoId":"content"},"adSlots":[1],"playerAds":[1],"adPlacements":[1]}');
      check(realm.ytInitialPlayerResponse.adSlots === undefined &&
            realm.ytInitialPlayerResponse.playerAds === undefined &&
            realm.ytInitialPlayerResponse.adPlacements === undefined, label + ' initial player advertising removed');
      check(realm.ytInitialPlayerResponse.videoDetails.videoId === 'content', label + ' initial content retained');
      realm.endpoint = `https://${host}/youtubei/v1/player?key=fixture`;
      realm.raw = '{"keep":9007199254740993,"adSlots":[1],"adPlacements":[1],"content":"\\u0041"}';
      const response = await realm.fetch(realm.endpoint), text = await response.text();
      const parsed = realm.JSON.parse(text);
      if (!withYee && host === 'tv.youtube.com') {
        check(text === realm.raw, label + ' TV response has no original body-prune rule');
      } else {
        check(parsed.adSlots === undefined && parsed.adPlacements === undefined,
              label + ' player response advertising removed');
      }
      check(parsed.content === 'A', label + ' normal response content retained');
      check(response.status === 201 && response.headers.get('X-Fixture') === 'kept', label + ' response metadata retained');
      if (withYee) check(text.includes('9007199254740993') && text.includes('\\u0041'),
                         label + ' precise integer and escaped bytes retained: ' + text);
      realm.endpoint = `https://${host}/youtubei/v1/browse`;
      check(await (await realm.fetch(realm.endpoint)).text() === realm.raw, label + ' unrelated response unchanged');
      if (host === 'www.youtube.com') {
        const xhr = new realm.XMLHttpRequest(); xhr.open('POST', 'https://www.youtube.com/youtubei/v1/player?key=fixture');
        xhr.send('{"context":{"client":{"clientName":"WEB","userAgent":"Mozilla/5.0 (channel)"}},"playbackContext":{"contentPlaybackContext":{"referer":"https://www.youtube.com/watch?v=content"}},"keep":2}');
        check(JSON.parse(xhr.sent).context.client.clientScreen === 'CHANNEL', label + ' original channel request contract executes');
        const node = realm.document.getElementById('contract-fixture');
        const source = '(function serverContract(){window.contractFixture = 7;})();';
        check(node.textContent !== source && node.textContent.includes('onAbnormalityDetected'),
              label + ' original serverContract DOM rewrite executes');
        const execute = realm.document.createElement('script');
        execute.textContent = node.textContent;
        realm.document.body.appendChild(execute);
        check(realm.contractFixture === 7, label + ' rewritten serverContract retains original statement');
      }
      check(realm.errors.length === 0, label + ' no asynchronous script errors');
    }
  }
  // Native Body readers must observe the same cleaned bytes even after the
  // original fetch/JSON wrappers are installed. Node mocks cannot prove this.
  for (const host of hosts) {
    const realm = make(host, true);
    realm.raw = '{"videoDetails":{"videoId":"normal"},"keep":9007199254740993,"content":"\\u0041π","adSlots":[1],"adPlacements":[1],"entries":[{"command":{"reelWatchEndpoint":{"adClientParams":{"isAd":true}}}},{"videoId":"normal-short"}]}';
    for (const path of ['/youtubei/v1/player', '/youtubei/v1/next', '/youtubei/v1/get_watch',
                        '/youtubei/v1/reel_watch_sequence', '/playlist?list=normal', '/watch?v=normal']) {
      realm.endpoint = `https://${host}${path}`;
      for (const method of ['text', 'json', 'arrayBuffer', 'blob', 'stream', 'clone']) {
        const label = `${host} ${path} ${method} original + Yee`;
        const response = await realm.fetch(realm.endpoint);
        check(!response.bodyUsed && response.url === realm.endpoint && response.status === 201 &&
              response.headers.get('X-Fixture') === 'kept', label + ' metadata before reading');
        let text, parsed;
        if (method === 'json') parsed = await response.json();
        else if (method === 'text') text = await response.text();
        else if (method === 'arrayBuffer') text = new TextDecoder().decode(await response.arrayBuffer());
        else if (method === 'blob') text = await (await response.blob()).text();
        else if (method === 'stream') {
          const reader = response.body.getReader(), decoder = new TextDecoder(); text = '';
          for (;;) {const {value, done} = await reader.read(); if (done) break; text += decoder.decode(value, {stream:true});}
          text += decoder.decode(); reader.releaseLock();
        } else {
          const cloned = response.clone();
          check(cloned.url === response.url && cloned.status === response.status &&
                cloned.type === response.type && cloned.redirected === response.redirected,
                label + ' clone metadata retained');
          text = await cloned.text();
          check(text === await response.text(), label + ' clone and original agree');
        }
        parsed ??= JSON.parse(text);
        check(parsed.adSlots === undefined && parsed.adPlacements === undefined &&
              parsed.videoDetails.videoId === 'normal' && parsed.content === 'Aπ' &&
              parsed.entries.length === 1 && parsed.entries[0].videoId === 'normal-short',
              label + ' advertising removed and normal content kept');
        if (text !== undefined) check(text.includes('9007199254740993') && text.includes('\\u0041'),
                                     label + ' original numeric and escaped bytes kept');
        check(response.bodyUsed, label + ' body consumption recorded');
        let rejected = false; try {await response.text();} catch {rejected = true;}
        check(rejected, label + ' second read rejects');
      }
    }
    realm.endpoint = `https://${host}/youtubei/v1/browse`;
    const unrelated = await realm.fetch(realm.endpoint);
    check(new TextDecoder().decode(await unrelated.arrayBuffer()) === realm.raw,
          host + ' unrelated native binary body untouched');
    check(realm.errors.length === 0, host + ' body matrix has no asynchronous script errors');
  }
  document.getElementById('result').textContent = JSON.stringify(results);
})().catch(error => {document.getElementById('result').textContent = JSON.stringify({error:error.stack});});
"""
with tempfile.TemporaryDirectory(prefix="yee-original-scriptlets-") as directory:
    path = Path(directory) / "fixture.html"
    path.write_text('<!doctype html><meta charset="utf-8"><body><pre id="result">pending</pre><script>const config=' +
                    json.dumps(config).replace('<', '\\u003c') + ';</script><script>' +
                    probe.replace('</script', '<\\/script') + '</script>')
    # File-backed diagnostics cannot fill a pipe and stall Chrome, and helpers
    # inheriting a pipe cannot mask the fixture result during shutdown.
    diagnostics = tempfile.TemporaryFile(mode='w+')
    process = subprocess.Popen([
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--no-default-browser-check', '--disable-background-networking',
        '--disable-component-update', '--disable-sync', '--disable-extensions',
        '--user-data-dir=' + directory + '/profile', '--remote-debugging-port=0',
        path.as_uri()], stdout=diagnostics, stderr=diagnostics,
        text=True, start_new_session=True)
    try:
        output = subprocess.run(['node', str(ROOT / 'tools/dev/read-content-blocking-fixture.mjs'),
                                 directory + '/profile/DevToolsActivePort', path.as_uri()],
                                capture_output=True, text=True, timeout=40)
        if output.returncode:
            raise RuntimeError(output.stderr)
        result = json.loads(output.stdout)
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
        diagnostics.close()
    assert 'error' not in result, result
    result['boundedShutdown'] = True
    (ROOT / '.local-build/original-scriptlet-browser-fixture.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
