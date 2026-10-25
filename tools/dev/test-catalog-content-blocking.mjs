#!/usr/bin/env node
// 새 프로필의 자동 다운로드와 재시작 후 실제 개드립 차단을 검증한다.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {connectCDP, pause, requireShutdown} from './content_blocking_test_runtime.mjs';

const executable = process.argv[2];
assert.ok(executable, 'Usage: node tools/dev/test-catalog-content-blocking.mjs ABSOLUTE_YEE_EXECUTABLE');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = await fs.mkdtemp(path.join(root, '.local-build/catalog-validation-'));
const profile = path.join(output, 'profile');
if (process.platform === 'win32') {
  const processes = JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-Command',
    "@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path) | ConvertTo-Json -Compress"], {encoding:'utf8'}).trim() || '[]');
  requireShutdown((Array.isArray(processes) ? processes : [processes]).filter(p=>p?.toLowerCase()===executable.replaceAll('/', '\\').toLowerCase()));
}

async function launch() {
  await fs.mkdir(profile, {recursive:true});
  const portFile = path.join(profile, 'DevToolsActivePort');
  await fs.rm(portFile, {force:true});
  const child = spawn(executable, [
    '--headless=new', `--user-data-dir=${profile}`, '--remote-debugging-port=0',
    '--no-first-run', '--no-default-browser-check', '--disable-sync',
    '--window-size=1440,1000', '--lang=ko',
    `--log-net-log=${path.join(output,'network.json')}`,
    '--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36',
    'about:blank'
  ], {windowsHide:true, stdio:['ignore','ignore','pipe']});
  child.stderr.on('data', data=>fs.appendFile(path.join(output,'browser.log'),data).catch(()=>{}));
  let endpoint;
  for(let i=0;i<200;i++) {
    assert.equal(child.exitCode,null,'검증용 브라우저가 시작 중 종료됨');
    try {endpoint=(await fs.readFile(portFile,'utf8')).trim().split(/\r?\n/);break;} catch {}
    await pause(100);
  }
  assert.ok(endpoint,'브라우저 검사 연결이 준비되지 않음');
  const browser=await connectCDP(`ws://127.0.0.1:${endpoint[0]}${endpoint[1]}`);
  const tabs=await (await fetch(`http://127.0.0.1:${endpoint[0]}/json/list`)).json();
  const page=await connectCDP(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
  await page.call('Network.enable');
  await page.call('Network.setCacheDisabled',{cacheDisabled:true});
  await page.call('Page.enable');
  return {child,browser,page};
}
async function close(session) {
  session.page.close();
  try {await session.browser.call('Browser.close');} catch {}
  session.browser.close();
  for(let i=0;i<150&&session.child.exitCode===null;i++) await pause(100);
  assert.notEqual(session.child.exitCode,null,'검증용 브라우저가 정상 종료되지 않음');
}
async function inspect(session,label) {
  const requests=[], responses=[], failures=[];
  const unsubscribe=[
    session.page.on('Network.requestWillBeSent',p=>requests.push({id:p.requestId,url:p.request.url,type:p.type})),
    session.page.on('Network.responseReceived',p=>responses.push({id:p.requestId,url:p.response.url,status:p.response.status,remoteIPAddress:p.response.remoteIPAddress})),
    session.page.on('Network.loadingFailed',p=>failures.push(p))
  ];
  await session.page.call('Page.navigate',{url:'https://www.dogdrip.net/'});
  await pause(20000);
  const document=(await session.page.call('Runtime.evaluate',{expression:`JSON.stringify({title:document.title,url:location.href,frames:[...document.querySelectorAll('iframe')].map(e=>({src:e.src,width:e.offsetWidth,height:e.offsetHeight})),adSlots:[...document.querySelectorAll('div[class^="ddnad"],ins.adsbygoogle')].map(e=>({className:e.className,display:getComputedStyle(e).display,visibility:getComputedStyle(e).visibility,height:e.offsetHeight}))})`,returnByValue:true})).result.value;
  const screenshot=await session.page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
  await fs.writeFile(path.join(output,`${label}.png`),Buffer.from(screenshot.data,'base64'));
  unsubscribe.forEach(unsubscribe=>unsubscribe());
  const report={document:JSON.parse(document),requests,responses,failures};
  assert.match(report.document.title,/DogDrip/,'개드립 본문을 실제로 받아야 함');
  await fs.writeFile(path.join(output,`${label}.json`),JSON.stringify(report,null,2));
  return report;
}

let session;
try {
  session=await launch();
  const before=await inspect(session,'before-update');
  console.log('초기 페이지 확인 완료. 시작 30초 후의 자동 갱신을 기다립니다.');
  const store=path.join(profile,'YeeContentBlockingLists');
  let state,manifest;
  const deadline=Date.now()+6*60*1000;
  while(Date.now()<deadline) {
    try {
      state=JSON.parse(await fs.readFile(path.join(store,'state.json'),'utf8'));
      manifest=JSON.parse(await fs.readFile(path.join(store,'generations',state.current,'manifest.json'),'utf8'));
      if(manifest.schema_version===3) break;
      if(manifest.schema_version===2&&state.checked_at===0)
        throw new Error('카탈로그 자동 갱신 실패. browser.log와 network.json을 확인하세요.');
    } catch(error) {
      if(error.message?.startsWith('카탈로그 자동 갱신 실패')) throw error;
    }
    await pause(1000);
  }
  assert.equal(manifest?.schema_version,3,'기본·한국어 카탈로그 자동 갱신이 완료되지 않음');
  const generation=state.current;
  const compatibility=JSON.parse(await fs.readFile(path.join(store,'generations',generation,'compatibility.json'),'utf8'));
  const catalog=JSON.parse(await fs.readFile(path.join(store,'generations',generation,'catalog.json'),'utf8'));
  console.log(JSON.stringify({generation,sources:catalog.downloads.length,omittedRules:compatibility.omitted_rules}));
  await close(session); session=null;
  session=await launch();
  const after=await inspect(session,'after-restart');
  const gfp=/^https:\/\/ssl\.pstatic\.net\/tveta\/libs\/glad\/prod\/gfp-core\.js/;
  assert.ok(before.responses.some(r=>gfp.test(r.url)&&r.remoteIPAddress),'대조 실행에서 네이버 광고 SDK가 실제 내려와야 함');
  assert.ok(after.requests.some(r=>gfp.test(r.url)&&after.failures.some(f=>f.requestId===r.id&&f.errorText==='net::ERR_BLOCKED_BY_CLIENT')),'갱신 후 네이버 광고 SDK가 차단되어야 함');
  assert.ok(!after.responses.some(r=>/ssl\.pstatic\.net\/(tveta|melona)\/libs\//.test(r.url)&&r.remoteIPAddress),'네이버 광고 SDK의 후속 요청도 전송되지 않아야 함');
  assert.ok(after.document.frames.every(f=>f.width===0||f.height===0),'관측된 광고 프레임이 남으면 안 됨');
  await fs.writeFile(path.join(output,'result.json'),JSON.stringify({actualYee:true,automaticUpdate:true,generation,sourceCount:catalog.downloads.length,compatibility,beforeFrames:before.document.frames.length,afterFrames:after.document.frames.length},null,2));
  console.log(`PASS: ${path.join(output,'result.json')}`);
} finally {
  if(session) await close(session);
}
