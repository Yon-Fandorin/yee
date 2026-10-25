#!/usr/bin/env node
// 실제 Yee 탭에서 요청 차단 결과·상속 프레임·사이트 예외의 렌더링 연결을 검사한다.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {connectCDP, pause, requireShutdown, waitForShutdown} from './content_blocking_test_runtime.mjs';

const [executable, mode = 'verify'] = process.argv.slice(2);
assert.ok(executable, 'Usage: node tools/dev/test-content-blocking-rendering.mjs YEE_EXECUTABLE [record|verify]');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = await fs.mkdtemp(path.join(root, '.local-build/rendering-'));
function runningYeeExecutables() {
  assert.equal(process.platform, 'win32', '현재 프로세스 확인은 Windows용이다');
  const raw = execFileSync('powershell.exe', ['-NoProfile', '-Command',
    '@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path) | ConvertTo-Json -Compress'], {encoding: 'utf8'}).trim();
  const processes = JSON.parse(raw || '[]');
  return (Array.isArray(processes) ? processes : [processes])
    .filter(p => p?.toLowerCase() === executable.replaceAll('/', '\\').toLowerCase());
}
const requests = [];
const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aDVkAAAAASUVORK5CYII=', 'base64');
const server = http.createServer((request, response) => {
  requests.push({host: request.headers.host, url: request.url});
  if (request.url === '/fixture') {
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    response.end(fixture);
  } else if (request.url === '/remote-child') {
    response.setHeader('Content-Type', 'text/html');
    response.end(`<!doctype html><body><script>
      addEventListener('message',e=>{if(e.data?.kind==='remote-blob')top.postMessage(e.data,'*')});
      const frame=document.createElement('iframe');
      frame.src=URL.createObjectURL(new Blob(['<script>parent.postMessage({kind:"remote-blob",alive:true},"*")<\\/script>'],{type:'text/html'}));
      document.body.append(frame);
    </script>`);
  } else if (request.url.startsWith('/redirect-')) {
    response.writeHead(302, {Location: `http://yee-block.test:${server.address().port}/${request.url.slice(10)}`});
    response.end();
  } else if (request.url.endsWith('.png')) {
    response.setHeader('Content-Type', 'image/png'); response.end(png);
  } else if (request.url === '/broken-image') {
    response.writeHead(404); response.end('ordinary missing image');
  } else {
    response.setHeader('Content-Type', 'text/html'); response.end('<!doctype html>ordinary embedded content');
  }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const port = server.address().port;
const childHTML = `<!doctype html><div class="yee-ad">advertisement</div><div id="ordinary">normal</div><script>
parent.postMessage({kind:window.name||(location.protocol==='blob:'?'blob':'srcdoc'),scriptlet:window.__yeeFilterFixture===true,ad:getComputedStyle(document.querySelector('.yee-ad')).display,ordinary:getComputedStyle(document.getElementById('ordinary')).display},'*');
</script>`;
const childSource = JSON.stringify(childHTML).replaceAll('<', '\\u003c');
const fixture = `<!doctype html><meta charset="utf-8"><title>Yee rendering integration</title>
<img id="blocked-image" width="240" height="90" src="http://yee-block.test:${port}/direct.png">
<img id="redirect-image" width="240" height="90" src="/redirect-image.png">
<img id="broken-image" width="240" height="90" src="/broken-image">
<iframe id="blocked-frame" width="240" height="90" src="http://yee-block.test:${port}/direct-frame"></iframe>
<iframe id="redirect-frame" width="240" height="90" src="/redirect-frame"></iframe>
<iframe id="ordinary-frame" width="240" height="90" src="/ordinary"></iframe>
<script>
window.probe={children:{}};
addEventListener('message', e=>{if(e.data?.kind)probe.children[e.data.kind]=e.data;});
const initial=document.createElement('iframe'); document.body.append(initial);
probe.initialBlankScriptlet=initial.contentWindow.__yeeFilterFixture===true;
initial.contentDocument.body.innerHTML='<div class="yee-ad">advertisement</div>';
probe.initialBlankAd=initial.contentWindow.getComputedStyle(initial.contentDocument.querySelector('.yee-ad')).display;
const srcdoc=document.createElement('iframe');srcdoc.srcdoc=${childSource};document.body.append(srcdoc);srcdoc.contentWindow.__yeeFilterFixture=false;
const blob=document.createElement('iframe');blob.src=URL.createObjectURL(new Blob([${childSource}],{type:'text/html'}));document.body.append(blob);blob.contentWindow.__yeeFilterFixture=false;
const opaque=document.createElement('iframe');opaque.name='opaque';opaque.sandbox='allow-scripts';opaque.srcdoc=${childSource};document.body.append(opaque);
const remote=document.createElement('iframe');remote.src='http://yee-outer.test:${port}/remote-child';document.body.append(remote);
window.measure=()=>Object.fromEntries(['blocked-image','redirect-image','broken-image','blocked-frame','redirect-frame','ordinary-frame'].map(id=>{const element=document.getElementById(id),rect=element.getBoundingClientRect();return[id,{width:rect.width,height:rect.height}]}));
</script>`;
const reports = [];
try {
  for (const scenario of ['on', 'off', 'exception']) {
    requireShutdown(runningYeeExecutables());
    requests.length = 0;
    const profile = path.join(output, scenario);
    await fs.mkdir(path.join(profile, 'Default'), {recursive: true});
    if (scenario === 'exception') {
      await fs.writeFile(path.join(profile, 'Default/Preferences'), JSON.stringify({yee: {content_blocking: {disabled_sites: ['yee-fixture.test']}}}));
    }
    const child = spawn(executable, ['--headless=new', `--user-data-dir=${profile}`,
      '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check',
      '--disable-background-networking', '--disable-sync', '--no-proxy-server',
      '--host-resolver-rules=MAP *.test 127.0.0.1', '--yee-content-blocking-test-rules',
      ...(scenario === 'off' ? ['--disable-features=YeeContentBlocking'] : []), 'about:blank'],
      {windowsHide: true, stdio: ['ignore', 'ignore', 'pipe']});
    child.stderr.on('data', data => fs.appendFile(path.join(output, `${scenario}.log`), data).catch(() => {}));
    let browser, page;
    try {
      let endpoint;
      for (let i = 0; i < 200; i++) {
        assert.equal(child.exitCode, null, '브라우저 시작 실패');
        try {endpoint = (await fs.readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).trim().split(/\r?\n/); break;} catch {}
        await pause(100);
      }
      assert.ok(endpoint, '검사 연결 준비');
      browser = await connectCDP(`ws://127.0.0.1:${endpoint[0]}${endpoint[1]}`);
      const tabs = await (await fetch(`http://127.0.0.1:${endpoint[0]}/json/list`)).json();
      page = await connectCDP(tabs.find(t => t.type === 'page').webSocketDebuggerUrl, globalThis.WebSocket, 25000);
      await page.call('Page.enable');
      await page.call('Page.navigate', {url: `http://yee-fixture.test:${port}/fixture`});
      const result = await page.call('Runtime.evaluate', {expression: `(async()=>{
        for(let i=0;i<150;i++){
          if(window.probe?.children.blob&&probe.children.srcdoc&&probe.children.opaque&&probe.children['remote-blob']&&document.readyState==='complete')return {probe,layout:measure()};
          await new Promise(r=>setTimeout(r,100));
        }
        throw new Error('프레임 준비 지연: '+JSON.stringify(window.probe));
      })()`, awaitPromise: true, returnByValue: true});
      assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
      const report = {scenario, ...result.result.value, requests: [...requests]};
      reports.push(report);
      await fs.writeFile(path.join(output, `${scenario}.json`), JSON.stringify(report, null, 2));
      const protectedMode = scenario === 'on';
      for (const id of ['broken-image', 'ordinary-frame']) assert.ok(report.layout[id].width > 0, `정상·통신 실패 요소 보존: ${id}`);
      assert.equal(report.probe.children.opaque.scriptlet, false, 'opaque origin에 부모 사이트 scriptlet을 확장하지 않음');
      assert.equal(report.probe.children.opaque.ad, 'block');
      assert.equal(report.probe.children['remote-blob'].alive, true, 'remote top 아래 blob에서도 renderer 생존');
      if (mode === 'verify') {
        for (const id of ['blocked-image', 'redirect-image', 'blocked-frame', 'redirect-frame']) {
          assert.equal(report.layout[id].width === 0, protectedMode, `차단 요소 접기: ${id}`);
        }
        assert.equal(report.probe.initialBlankScriptlet, protectedMode, '초기 빈 프레임의 동기 scriptlet·사이트 예외');
        assert.equal(report.probe.initialBlankAd, protectedMode ? 'none' : 'block');
        for (const kind of ['blob', 'srcdoc']) {
          assert.equal(report.probe.children[kind].scriptlet, protectedMode, `${kind} 첫 inline script 이전 주입`);
          assert.equal(report.probe.children[kind].ad, protectedMode ? 'none' : 'block', `${kind} 상속 origin CSS`);
          assert.equal(report.probe.children[kind].ordinary, 'block');
        }
      }
      assert.equal(requests.some(r => r.host.startsWith('yee-block.test:')), !protectedMode, '광고 원 요청의 서버 미도달');
      // 차단 후 정상 URL로 재사용하면 Chromium이 접기를 해제해야 한다.
      await page.call('Runtime.evaluate', {expression: `document.getElementById('blocked-frame').src='/recovered';document.getElementById('blocked-image').src='/recovered.png'`});
      await pause(600);
      report.recovered = (await page.call('Runtime.evaluate', {expression: 'measure()', returnByValue: true})).result.value;
      assert.ok(report.recovered['blocked-frame'].width > 0 && report.recovered['blocked-image'].width > 0, '정상 콘텐츠로 바뀐 iframe·이미지 복구');
      const topBlobHTML = '<!doctype html><div class="yee-ad">advertisement</div><script>window.topBlobProbe={scriptlet:window.__yeeFilterFixture===true,ad:getComputedStyle(document.querySelector(".yee-ad")).display};</script>';
      const blobURL = (await page.call('Runtime.evaluate', {expression: `URL.createObjectURL(new Blob([${JSON.stringify(topBlobHTML)}],{type:'text/html'}))`, returnByValue: true})).result.value;
      await page.call('Page.navigate', {url: blobURL});
      for (let i = 0; i < 100; i++) {
        report.topBlob = (await page.call('Runtime.evaluate', {expression: 'window.topBlobProbe', returnByValue: true})).result.value;
        if (report.topBlob) break;
        await pause(100);
      }
      assert.ok(report.topBlob, '최상위 blob 문서 로드');
      assert.equal(report.topBlob.ad, 'block', '설정 전달 대상이 아닌 최상위 blob에는 origin CSS를 확대하지 않음');
      if (scenario !== 'on') assert.equal(report.topBlob.scriptlet, false, '최상위 blob에서도 끄기·사이트 예외 보존');
      console.log(JSON.stringify(report));
      await fs.writeFile(path.join(output, `${scenario}.json`), JSON.stringify(report, null, 2));
    } finally {
      page?.close();
      try {await browser?.call('Browser.close');} catch {}
      browser?.close();
      for (let i = 0; i < 150 && child.exitCode === null; i++) await pause(100);
      assert.notEqual(child.exitCode, null, '검증 브라우저 정상 종료');
      await waitForShutdown(runningYeeExecutables);
    }
  }
} finally {await new Promise(resolve => server.close(resolve));}
await fs.writeFile(path.join(output, 'result.json'), JSON.stringify({mode, reports}, null, 2));
console.log(`Evidence: ${output}`);
