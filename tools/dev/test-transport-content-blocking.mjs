#!/usr/bin/env node
// 새 실제 Yee에서 WS/WSS/HTTP3 및 서비스 워커 캐시·합성 응답을 대조한다.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import https from 'node:https';
import path from 'node:path';
import readline from 'node:readline';
import {fileURLToPath} from 'node:url';
import {connectCDP, pause, requireShutdown} from './content_blocking_test_runtime.mjs';

const [executable, python = 'python3'] = process.argv.slice(2);
assert.ok(executable, 'Usage: node tools/dev/test-transport-content-blocking.mjs YEE_EXECUTABLE PYTHON_WITH_AIOQUIC');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = await fs.mkdtemp(path.join(root, '.local-build/transports-'));
const events = [], quicRequests = [], sockets = new Set(), reports = [];
const quic = spawn(python, [path.join(root, 'tests/fixtures/content-blocking/webtransport_server.py'), output],
  {windowsHide: true, stdio: ['pipe', 'pipe', 'pipe']});
let quicErrors = '';
quic.stderr.on('data', d => {
  quicErrors = (quicErrors + d).slice(-8000);
  fs.appendFile(path.join(output, 'quic.log'), d).catch(() => {});
});
let config;
readline.createInterface({input: quic.stdout}).on('line', line => {
  const entry = JSON.parse(line);
  if (entry.port) config = entry;
  else if (entry.request) quicRequests.push(entry.request);
});
let server, secureServer;
try {
  for (let i = 0; i < 150 && !config; i++) {
    assert.equal(quic.exitCode, null, `로컬 HTTP3 서버 시작 실패 (aioquic 필요): ${quicErrors}`);
    await pause(100);
  }
  assert.ok(config, '로컬 HTTP3 서버 준비');
  const pageScript = () => `
    const timeout=(promise,budget=5000)=>Promise.race([promise,new Promise((_,reject)=>setTimeout(()=>reject(Error('timeout')),budget))]);
    async function ws(url){globalThis.__connectionStage=url;return timeout(new Promise(resolve=>{const socket=new WebSocket(url);socket.onopen=()=>{socket.close();resolve(true)};socket.onerror=()=>resolve(false)}));}
    async function wt(url){globalThis.__connectionStage=url;const transport=new WebTransport(url,{serverCertificateHashes:[{algorithm:'sha-256',value:new Uint8Array(${JSON.stringify(config.certificateHash)})}]});transport.closed.catch(()=>{});let phase='ready';try{await timeout(transport.ready);phase='close';transport.close();await timeout(transport.closed);return true}catch(error){(globalThis.__wtFailures??=[]).push({url,phase,message:error.message});transport.close();return false}}
    async function blockedFetch(url){try{return {blocked:false,text:await(await fetch(url)).text()}}catch{return {blocked:true}}}
    async function connections(){return {
      ws:await ws('ws://yee-socket.test:'+HTTP_PORT+'/blocked'),
      wss:await ws('wss://yee-socket.test:'+TLS_PORT+'/blocked'),
      wsAllowed:await ws('ws://yee-socket.test:'+HTTP_PORT+'/allowed'),
      wt:await wt('https://yee-transport.test:${config.port}/blocked'),
      wtAllowed:await wt('https://yee-transport.test:${config.port}/allowed'),
      domain:await ws('ws://yee-custom.test:'+HTTP_PORT+'/custom'),
      failures:globalThis.__wtFailures??[]
    };}
  `;
  let serviceWorker, fixture, worker, crossProbe, crossOuter, cacheWorker;
  function respond(request, response) {
    events.push({host: request.headers.host, url: request.url, method: request.method});
    response.setHeader('Cache-Control', 'no-store');
    if (request.url === '/scope/sw.js') {
      response.setHeader('Content-Type', 'text/javascript'); response.end(serviceWorker);
    } else if (request.url === '/scope/fixture') {
      response.setHeader('Content-Type', 'text/html'); response.end(fixture);
    } else if (request.url === '/cross') {
      response.setHeader('Content-Type', 'text/html'); response.end(crossOuter);
    } else if (request.url === '/scope/cross-probe') {
      response.setHeader('Content-Type', 'text/html'); response.end(crossProbe);
    } else if (request.url === '/scope/cache-worker.js') {
      response.setHeader('Content-Type', 'text/javascript'); response.end(cacheWorker);
    } else if (request.url === '/scope/csp-image') {
      response.setHeader('Content-Type','image/gif');
      response.end(Buffer.from('R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7','base64'));
    } else if (request.url === '/scope/worker.js' || request.url === '/scope/shared.js') {
      response.setHeader('Content-Type', 'text/javascript'); response.end(worker);
    } else response.end('normal-network');
  }
  server = http.createServer(respond);
  secureServer = https.createServer({cert: await fs.readFile(path.join(output, 'certificate.pem')),
    key: await fs.readFile(path.join(output, 'key.pem'))}, respond);
  for (const endpoint of [server, secureServer]) {
    endpoint.on('connection', socket => {sockets.add(socket);socket.on('close',()=>sockets.delete(socket));});
    endpoint.on('upgrade', (request, socket) => {
      events.push({host: request.headers.host, url: request.url, ws: true, ua: request.headers['user-agent']});
      const accept = crypto.createHash('sha1').update(request.headers['sec-websocket-key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
      socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
      socket.on('data', data => {if((data[0]&15)===8){socket.end(Buffer.from([0x88,0]));}});
      socket.on('error',()=>{});
    });
    await new Promise(resolve => endpoint.listen(0, '127.0.0.1', resolve));
  }
  const httpPort = server.address().port, tlsPort = secureServer.address().port;
  const constants = `const HTTP_PORT=${httpPort},TLS_PORT=${tlsPort};`;
  serviceWorker = `${constants}${pageScript()}
    self.addEventListener('install',e=>e.waitUntil((async()=>{
      const cache=await caches.open('yee-native-test');
      await cache.put('/scope/sw-blocked-cache',new Response('cached-ad'));
      await cache.put('/scope/normal-cache',new Response('preserved-cache'));
      await self.skipWaiting();
    })()));
    self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()));
    self.addEventListener('message',e=>{
      if(e.data==='probe')e.waitUntil(connections().then(result=>e.ports[0].postMessage(result)));
    });
    self.addEventListener('fetch',e=>{
      const url=new URL(e.request.url),p=url.pathname;
      if(p.endsWith('/sw-blocked-cache')||p.endsWith('/normal-cache'))e.respondWith(caches.open('yee-native-test').then(c=>c.match(e.request)));
      else if(url.hostname==='yee-custom.test'&&p.endsWith('/custom-cache'))e.respondWith(new Response('custom-cached-ad'));
      else if(p.endsWith('/sw-blocked-synthetic'))e.respondWith(new Response('synthetic-ad'));
      else if(p.endsWith('/sw-replacement.js'))e.respondWith(new Response('original-ad-script'));
      else if(p.endsWith('/sw-query'))e.respondWith(new Response(url.search));
      else if(p.endsWith('/sw-redirect-blocked'))e.respondWith(Response.redirect(new URL('/scope/sw-blocked-cache',url).href));
      else if(p.endsWith('/sw-frame')||p.endsWith('/sw-main')||p.endsWith('/sw-normal-frame'))e.respondWith(new Response('<!doctype html><div id="cached-document">cached document</div>',{headers:{'Content-Type':'text/html'}}));
      else if(p.endsWith('/csp-frame'))e.respondWith(new Response('<!doctype html><img src="/scope/csp-image">',{headers:{'Content-Type':'text/html'}}));
      else if(p.endsWith('/sw-worker-blocked.js'))e.respondWith(new Response('postMessage("unfiltered-worker")',{headers:{'Content-Type':'text/javascript'}}));
    });`;
  worker = `${constants}${pageScript()}
    async function probe(){return {connections:await connections(),cache:await blockedFetch('/scope/sw-blocked-cache'),synthetic:await blockedFetch('/scope/sw-blocked-synthetic'),normal:await blockedFetch('/scope/normal-cache'),custom:await blockedFetch('http://yee-custom.test:'+HTTP_PORT+'/custom-cache')}}
    if('onconnect' in self)onconnect=e=>{probe().then(r=>e.ports[0].postMessage(r),error=>e.ports[0].postMessage({error:error.message,stage:self.__connectionStage}))};
    else probe().then(r=>postMessage(r),error=>postMessage({error:error.message,stage:self.__connectionStage}));`;
  fixture = `<!doctype html><meta charset="utf-8"><title>Yee transport and cached content</title><h1>Normal visible content</h1><script>
    ${constants}${pageScript()}
    window.done=(async()=>{
      window.__testStage='registration';
      await navigator.serviceWorker.register('/scope/sw.js');await navigator.serviceWorker.ready;
      if(!navigator.serviceWorker.controller)await new Promise(r=>navigator.serviceWorker.addEventListener('controllerchange',r,{once:true}));
      window.__testStage='document-connections';
      const result={connections:await connections(),cache:await blockedFetch('/scope/sw-blocked-cache'),synthetic:await blockedFetch('/scope/sw-blocked-synthetic'),normal:await blockedFetch('/scope/normal-cache'),replacement:await blockedFetch('/scope/sw-replacement.js'),query:await blockedFetch('/scope/sw-query?tracking=1&keep=2'),redirect:await blockedFetch('/scope/sw-redirect-blocked')};
      window.__partialResult=result;
      result.custom=await blockedFetch('http://yee-custom.test:'+HTTP_PORT+'/custom-cache');
      window.__testStage='dedicated-worker';
      result.worker=await timeout(new Promise(resolve=>{const w=new Worker('/scope/worker.js');w.onmessage=e=>{w.terminate();resolve(e.data)};w.onerror=e=>resolve({error:e.message})}),35000);
      window.__testStage='shared-worker';
      result.shared=await timeout(new Promise(resolve=>{const w=new SharedWorker('/scope/shared.js');w.port.onmessage=e=>{w.port.close();resolve(e.data)};w.onerror=e=>resolve({error:e.message})}),35000);
      window.__testStage='service-worker';
      result.serviceWorker=await timeout(new Promise(resolve=>{const c=new MessageChannel();c.port1.onmessage=e=>{c.port1.close();resolve(e.data)};navigator.serviceWorker.controller.postMessage('probe',[c.port2])}),35000);
      window.__testStage='worker-main';
      result.workerMain=await timeout(new Promise(resolve=>{const w=new Worker('/scope/sw-worker-blocked.js');w.onmessage=()=>{w.terminate();resolve(false)};w.onerror=e=>{e.preventDefault();w.terminate();resolve(true)}}));
      window.__testStage='iframe';
      const frame=document.createElement('iframe');frame.src='/scope/sw-frame';document.body.append(frame);
      await timeout(new Promise(resolve=>{const poll=()=>{
        if(frame.getBoundingClientRect().width===0)return resolve();
        try{if(frame.contentDocument?.getElementById('cached-document'))return resolve()}catch{}
        setTimeout(poll,50);
      };poll()}));
      result.frameWidth=frame.getBoundingClientRect().width;
      window.__testStage='recovery';
      frame.src='/scope/sw-normal-frame';
      await timeout(new Promise(resolve=>{const poll=()=>{
        try{if(frame.getBoundingClientRect().width>0&&
          frame.contentWindow.location.pathname==='/scope/sw-normal-frame'&&
          frame.contentDocument?.getElementById('cached-document'))return resolve()}catch{}
        setTimeout(poll,50);
      };poll()}));
      result.recoveredWidth=frame.getBoundingClientRect().width;
      window.__testStage='csp';
      const csp=document.createElement('iframe');csp.src='/scope/csp-frame';document.body.append(csp);
      await new Promise(r=>csp.onload=r);
      result.cspBlocked=csp.contentDocument.querySelector('img').naturalWidth===0;
      return result;
    })();
  </script>`;
  cacheWorker = `async function probe(){let blocked=false;try{await fetch('/scope/sw-blocked-cache')}catch{blocked=true}return {blocked,normal:await(await fetch('/scope/normal-cache')).text()}}probe().then(r=>postMessage(r));`;
  crossProbe = `<!doctype html><body><script>
    window.done=(async()=>{
      await navigator.serviceWorker.register('/scope/sw.js');await navigator.serviceWorker.ready;
      if(!navigator.serviceWorker.controller)await new Promise(r=>navigator.serviceWorker.addEventListener('controllerchange',r,{once:true}));
      let blocked=false;try{await fetch('/scope/sw-blocked-cache')}catch{blocked=true}
      const normal=await(await fetch('/scope/normal-cache')).text();
      const worker=await new Promise(resolve=>{const w=new Worker('/scope/cache-worker.js');w.onmessage=e=>{w.terminate();resolve(e.data)}});
      return {blocked,normal,worker};
    })();window.done.then(result=>parent.postMessage({kind:'cross-result',result},'*'));
  </script>`;
  crossOuter = `<!doctype html><body><script>window.done=new Promise(resolve=>addEventListener('message',e=>{if(e.data?.kind==='cross-result')resolve(e.data.result)}));</script><iframe src="http://yee-fixture.test:${httpPort}/scope/cross-probe"></iframe>`;
  for (const scenario of ['off', 'on', 'exception', 'domain']) {
    const raw = execFileSync('powershell.exe',['-NoProfile','-Command',
      '@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path) | ConvertTo-Json -Compress'],{encoding:'utf8'}).trim();
    const processes = JSON.parse(raw || '[]');
    requireShutdown((Array.isArray(processes)?processes:[processes]).filter(p=>p?.toLowerCase()===executable.replaceAll('/','\\').toLowerCase()));
    events.length = 0;quicRequests.length = 0;
    const profile=path.join(output,scenario);
    await fs.mkdir(path.join(profile,'Default'),{recursive:true});
    const prefs = scenario==='exception'?{disabled_sites:['yee-fixture.test']}:
      scenario==='domain'?{blocked_domains:[{domain:'yee-custom.test',includeSubdomains:true}]}:{};
    await fs.writeFile(path.join(profile,'Default/Preferences'),JSON.stringify({yee:{content_blocking:prefs}}));
    const child=spawn(executable,['--headless=new',`--user-data-dir=${profile}`,'--remote-debugging-port=0',
      '--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--no-proxy-server',
      '--host-resolver-rules=MAP *.test 127.0.0.1',`--unsafely-treat-insecure-origin-as-secure=http://yee-fixture.test:${httpPort},http://yee-outer.test:${httpPort}`,
      `--ignore-certificate-errors-spki-list=${config.spkiHash}`,'--yee-content-blocking-test-rules',
      `--log-net-log=${path.join(output,scenario+'-netlog.json')}`,'--net-log-capture-mode=Everything',
      ...(scenario==='off'?['--disable-features=YeeContentBlocking']:[]),'about:blank'],{windowsHide:true,stdio:['ignore','ignore','pipe']});
    child.stderr.on('data',d=>fs.appendFile(path.join(output,`${scenario}.log`),d).catch(()=>{}));
    let browser,page,tab;
    try {
      let endpoint;
      for(let i=0;i<200;i++){
        assert.equal(child.exitCode,null,'Yee 시작 실패');
        try{endpoint=(await fs.readFile(path.join(profile,'DevToolsActivePort'),'utf8')).trim().split(/\r?\n/);break}catch{}
        await pause(100);
      }
      assert.ok(endpoint,'검사 연결 준비');
      browser=await connectCDP(`ws://127.0.0.1:${endpoint[0]}${endpoint[1]}`);
      const tabs=await(await fetch(`http://127.0.0.1:${endpoint[0]}/json/list`)).json();
      tab=tabs.find(t=>t.type==='page');page=await connectCDP(tab.webSocketDebuggerUrl,globalThis.WebSocket,90000);
      await page.call('Page.enable');
      await page.call('Page.navigate',{url:`http://yee-fixture.test:${httpPort}/scope/fixture`});
      const evaluation=await page.call('Runtime.evaluate',{expression:`(async()=>{for(let i=0;i<200;i++){if(location.pathname==='/scope/fixture'&&window.done)return await window.done;await new Promise(r=>setTimeout(r,100))}throw Error('fixture 준비 지연')})()`,awaitPromise:true,returnByValue:true});
      if(evaluation.exceptionDetails){
        const diagnostic=(await page.call('Runtime.evaluate',{expression:`({stage:window.__testStage,partial:window.__partialResult,frames:[...document.querySelectorAll('iframe')].map(f=>{try{return {src:f.src,url:f.contentWindow.location.href,width:f.getBoundingClientRect().width,text:f.contentDocument.body?.innerText}}catch(e){return {src:f.src,width:f.getBoundingClientRect().width,error:e.message}}})})`,returnByValue:true})).result.value;
        await fs.writeFile(path.join(output,`${scenario}-failure.json`),JSON.stringify({diagnostic,events,quicRequests,exception:evaluation.exceptionDetails},null,2));
      }
      assert.ok(!evaluation.exceptionDetails,JSON.stringify(evaluation.exceptionDetails));
      const report={scenario,result:evaluation.result.value,events:[...events],quicRequests:[...quicRequests]};
      reports.push(report);
      await fs.writeFile(path.join(output,`${scenario}.json`),JSON.stringify(report,null,2));
      const protectedMode=scenario==='on'||scenario==='domain';
      const r=report.result;
      for(const connections of [r.connections,r.worker.connections,r.shared.connections,r.serviceWorker]){
        assert.equal(connections.ws,!protectedMode,'WS 차단·예외');
        assert.equal(connections.wss,!protectedMode,'WSS 차단·예외');
        assert.equal(connections.wt,!protectedMode,'HTTP3 WebTransport 차단·예외');
        assert.equal(connections.wsAllowed,true,'필터 예외인 정상 WS 유지');
        assert.equal(connections.wtAllowed,true,'필터 예외인 정상 HTTP3 유지');
        assert.equal(connections.domain,scenario!=='domain','사용자 도메인 WS 차단');
      }
      for(const context of [r,r.worker,r.shared]){
        assert.equal(context.cache.blocked,protectedMode,'문서·worker CacheStorage 응답 차단');
        assert.equal(context.synthetic.blocked,protectedMode,'문서·worker 합성 응답 차단');
        assert.equal(context.normal.text,'preserved-cache','정상 캐시 유지');
        assert.equal(context.custom.blocked,scenario==='domain','사용자 도메인의 SW 캐시 차단');
        if(scenario!=='domain')assert.equal(context.custom.text,'custom-cached-ad');
      }
      assert.equal(r.workerMain,protectedMode,'SW가 응답한 워커 main script');
      assert.equal(r.frameWidth===0,protectedMode,'SW가 응답한 iframe 접기');
      assert.ok(r.recoveredWidth>0,'정상 SW 응답으로 바꾼 iframe 복구');
      assert.equal(r.cspBlocked,protectedMode,'SW 문서 응답의 CSP 필터');
      assert.equal(r.replacement.text,protectedMode?'/* Yee empty replacement. */':'original-ad-script','캐시 경로의 로컬 대체 응답');
      assert.equal(r.query.text,protectedMode?'?keep=2':'?tracking=1&keep=2','SW 앞 removeparam');
      assert.equal(r.redirect.blocked,protectedMode,'SW redirect 대상 재판정');
      assert.equal(events.some(e=>e.ws&&e.url==='/blocked'),!protectedMode,'차단 WS의 서버 미도달');
      assert.equal(quicRequests.includes('/blocked'),!protectedMode,'차단 HTTP3의 서버 미도달');
      assert.equal(events.some(e=>e.url.includes('/sw-blocked-')),false,'캐시·합성 요청을 네트워크 실패로 오인하지 않음');
      await page.call('Page.navigate',{url:`http://yee-outer.test:${httpPort}/cross`});
      const cross=await page.call('Runtime.evaluate',{expression:`(async()=>{for(let i=0;i<200;i++){if(location.pathname==='/cross'&&window.done)return await window.done;await new Promise(r=>setTimeout(r,100))}throw Error('cross 준비 지연')})()`,awaitPromise:true,returnByValue:true});
      assert.ok(!cross.exceptionDetails,JSON.stringify(cross.exceptionDetails));
      report.cross=cross.result.value;
      // iframe 주소의 예외는 예외 없는 최상위 사이트까지 확장하지 않는다.
      assert.equal(report.cross.blocked,scenario!=='off','cross-site 문서의 publisher 정책');
      assert.equal(report.cross.worker.blocked,scenario!=='off','cross-site worker의 publisher 정책');
      assert.equal(report.cross.normal,'preserved-cache');
      assert.equal(report.cross.worker.normal,'preserved-cache');
      report.mainNavigation=await page.call('Page.navigate',{url:`http://yee-fixture.test:${httpPort}/scope/sw-main`});
      if(protectedMode)assert.match(report.mainNavigation.errorText||'',/ERR_BLOCKED_BY_CLIENT/,'SW 최상위 탐색 차단');
      else {
        assert.ok(!report.mainNavigation.errorText,'SW 최상위 정상 탐색');
        await pause(300);
        assert.equal((await page.call('Runtime.evaluate',{expression:'!!document.getElementById("cached-document")',returnByValue:true})).result.value,true,'SW 최상위 응답 표시');
      }
      await fs.writeFile(path.join(output,`${scenario}.json`),JSON.stringify(report,null,2));
      console.log(JSON.stringify({scenario,connections:r.connections,cache:r.cache,workerMain:r.workerMain,frameWidth:r.frameWidth}));
    } finally {
      page?.close();try{await browser?.call('Browser.close')}catch{}browser?.close();
      for(let i=0;i<150&&child.exitCode===null;i++)await pause(100);
      assert.notEqual(child.exitCode,null,'검증 Yee 정상 종료');
    }
  }
  await fs.writeFile(path.join(output,'result.json'),JSON.stringify({reports},null,2));
  console.log(`Evidence: ${output}`);
} finally {
  for(const socket of sockets)socket.destroy();
  for(const endpoint of [server,secureServer])if(endpoint)await new Promise(r=>endpoint.close(r));
  quic.stdin.end('\n');
  for(let i=0;i<100&&quic.exitCode===null;i++)await pause(100);
  assert.notEqual(quic.exitCode,null,'로컬 HTTP3 서버 정상 종료');
}
