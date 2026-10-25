#!/usr/bin/env node
// 실제 필터를 사용해 iframe 광고의 첫 스타일 계산·첫 프레임부터 숨김이 적용되는지 확인한다.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {connectCDP, pause, requireShutdown} from './content_blocking_test_runtime.mjs';

const [executable, listStore, mode='verify'] = process.argv.slice(2);
assert.ok(executable&&listStore, 'Usage: node tools/dev/test-content-blocking-first-paint.mjs YEE_EXECUTABLE LIST_STORE [record|verify]');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const output=await fs.mkdtemp(path.join(root,'.local-build/first-paint-'));
if(process.platform==='win32') {
  const result=execFileSync('powershell.exe',['-NoProfile','-Command',
    "@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path) | ConvertTo-Json -Compress"],{encoding:'utf8'}).trim();
  const processes=JSON.parse(result||'[]');
  requireShutdown((Array.isArray(processes)?processes:[processes]).filter(p=>p?.toLowerCase()===executable.replaceAll('/','\\').toLowerCase()));
}
const fixtureURL='https://www.dogdrip.net/__yee_first_paint_fixture';
const trace=`(() => {
  if(window!==top)return;
  const started=performance.now(), samples=[];
  function visible(element) {
    if(!element)return false;
    const css=getComputedStyle(element);
    return css.display!=='none'&&css.visibility!=='hidden'&&!/^circle\\(0/.test(css.clipPath)&&element.getBoundingClientRect().width>0&&element.getBoundingClientRect().height>0;
  }
  function sample() {
    const ads=[...document.querySelectorAll('ins.adsbygoogle,[id^="img_"][style],div[class^="ddnad"]')].filter(e=>e.querySelector('iframe'));
    samples.push({at:performance.now()-started,visibleAds:ads.filter(visible).map(e=>e.id||e.className),ordinaryVisible:visible(document.getElementById('ordinary-frame'))});
    if(performance.now()-started<3000)requestAnimationFrame(sample);
  }
  window.__yeePaintTrace={samples,visible};
  requestAnimationFrame(sample);
})();`;
const fixture=`<!doctype html><html lang="ko"><head><meta charset="utf-8"><title>Yee first paint fixture</title></head><body>
<ins id="ad-frame" class="adsbygoogle" style="display:block;width:300px;height:160px;background:red"><iframe srcdoc="ad" style="width:300px;height:150px"></iframe></ins>
<div id="img_first_paint" style="width:300px;height:160px;background:red"><iframe frameborder="0" style="width:300px;height:150px" srcdoc="ad"></iframe></div>
<iframe id="ordinary-frame" style="width:320px;height:100px" srcdoc="normal embedded content"></iframe>
<script>
window.__yeeImmediate={ad:__yeePaintTrace.visible(document.getElementById('ad-frame')),conditional:__yeePaintTrace.visible(document.getElementById('img_first_paint'))};
setTimeout(()=>{const late=document.createElement('ins');late.id='late-ad';late.className='adsbygoogle';late.style='display:block;width:300px;height:160px;background:red';late.innerHTML='<iframe srcdoc="ad" style="width:300px;height:150px"></iframe>';document.body.append(late);window.__yeeImmediate.late=__yeePaintTrace.visible(late)},200);
</script></body></html>`;
const reports=[];
for(const scenario of ['on','off','exception']) {
  const profile=path.join(output,scenario);
  await fs.mkdir(path.join(profile,'Default'),{recursive:true});
  await fs.cp(listStore,path.join(profile,'YeeContentBlockingLists'),{recursive:true});
  if(scenario==='exception')await fs.writeFile(path.join(profile,'Default/Preferences'),JSON.stringify({yee:{content_blocking:{disabled_sites:['www.dogdrip.net']}}}));
  const child=spawn(executable,['--headless=new',`--user-data-dir=${profile}`,'--remote-debugging-port=0','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--window-size=1440,1000','--lang=ko',
    '--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36',
    ...(scenario==='off'?['--disable-features=YeeContentBlocking']:[]),'about:blank'],{windowsHide:true,stdio:['ignore','ignore','pipe']});
  child.stderr.on('data',data=>fs.appendFile(path.join(output,`${scenario}.log`),data).catch(()=>{}));
  let browser,page;
  try {
    let endpoint;
    for(let i=0;i<200;i++) {
      assert.equal(child.exitCode,null,'브라우저 시작 실패');
      try {endpoint=(await fs.readFile(path.join(profile,'DevToolsActivePort'),'utf8')).trim().split(/\r?\n/);break;} catch {}
      await pause(100);
    }
    assert.ok(endpoint,'브라우저 검사 연결 없음');
    browser=await connectCDP(`ws://127.0.0.1:${endpoint[0]}${endpoint[1]}`);
    const tabs=await(await fetch(`http://127.0.0.1:${endpoint[0]}/json/list`)).json();
    page=await connectCDP(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
    await page.call('Page.enable');
    await page.call('Page.addScriptToEvaluateOnNewDocument',{source:trace});
    await page.call('Fetch.enable',{patterns:[{urlPattern:fixtureURL,resourceType:'Document',requestStage:'Request'}]});
    page.on('Fetch.requestPaused',p=>{page.call('Fetch.fulfillRequest',{requestId:p.requestId,responseCode:200,responseHeaders:[{name:'Content-Type',value:'text/html; charset=utf-8'}],body:Buffer.from(fixture).toString('base64')}).catch(()=>{});});
    await page.call('Page.navigate',{url:fixtureURL});
    await pause(3500);
    const report=JSON.parse((await page.call('Runtime.evaluate',{expression:'JSON.stringify({immediate:window.__yeeImmediate,samples:window.__yeePaintTrace.samples})',returnByValue:true})).result.value);
    assert.ok(report.samples.length>10,'실제 프레임 샘플이 충분해야 함');
    const visibleFrames=report.samples.filter(s=>s.visibleAds.length).length;
    const ordinaryFrames=report.samples.filter(s=>s.ordinaryVisible).length;
    assert.ok(ordinaryFrames>0,'정상 iframe이 표시되어야 함');
    if(scenario!=='on')assert.ok(visibleFrames>0&&report.immediate.ad&&report.immediate.late,'끄기·사이트 예외에서는 광고 요소도 보존해야 함');
    if(mode==='verify'&&scenario==='on') {
      assert.deepEqual(report.immediate,{ad:false,conditional:false,late:false},'첫 계산과 동적 삽입 시점부터 광고 스타일 적용');
      assert.equal(visibleFrames,0,'광고 프레임이 한 번도 표시되지 않아야 함');
    }
    reports.push({scenario,visibleFrames,ordinaryFrames,...report});
    await fs.writeFile(path.join(output,`${scenario}.json`),JSON.stringify(reports.at(-1),null,2));
    console.log(JSON.stringify({scenario,visibleFrames,ordinaryFrames,immediate:report.immediate}));
    if(scenario==='on') {
      await page.call('Fetch.disable');
      await page.call('Page.navigate',{url:'https://www.dogdrip.net/'});
      await pause(5000);
      const live=JSON.parse((await page.call('Runtime.evaluate',{expression:`JSON.stringify({
        title:document.title,samples:window.__yeePaintTrace.samples,
        bodyText:document.body.innerText.length,
        visibleLinks:[...document.querySelectorAll('a')].filter(e=>{
          const r=e.getBoundingClientRect();
          if(!e.checkVisibility({checkOpacity:true,checkVisibilityCSS:true})||
            r.width<=0||r.height<=0||r.top>=innerHeight||r.bottom<=0||
            r.left>=innerWidth||r.right<=0)return false;
          const x=(Math.max(0,r.left)+Math.min(innerWidth-1,r.right))/2;
          const y=(Math.max(0,r.top)+Math.min(innerHeight-1,r.bottom))/2;
          const painted=document.elementFromPoint(x,y);
          return painted!==null&&(painted===e||e.contains(painted));
        }).map(e=>e.innerText.trim()).filter(Boolean),
        rootStyle:{display:getComputedStyle(document.documentElement).display,
          visibility:getComputedStyle(document.documentElement).visibility,
          clipPath:getComputedStyle(document.documentElement).clipPath},
        bodyStyle:{display:getComputedStyle(document.body).display,
          visibility:getComputedStyle(document.body).visibility,
          clipPath:getComputedStyle(document.body).clipPath}
      })`,returnByValue:true})).result.value);
      assert.match(live.title,/DogDrip/,'실제 개드립 본문 로드');
      live.visibleFrames=live.samples.filter(s=>s.visibleAds.length).length;
      if(mode==='verify')assert.equal(live.visibleFrames,0,'실제 개드립 광고 프레임의 초기 노출 방지');
      await fs.writeFile(path.join(output,'dogdrip.json'),JSON.stringify(live,null,2));
      const shot=await page.call('Page.captureScreenshot',{format:'png'});
      await fs.writeFile(path.join(output,'dogdrip.png'),Buffer.from(shot.data,'base64'));
      assert.ok(live.bodyText>500&&live.visibleLinks.length>10,'실제 개드립 본문과 화면 안의 정상 링크가 표시되어야 함');
      console.log(JSON.stringify({liveDogdrip:true,visibleFrames:live.visibleFrames}));
    }
  } finally {
    page?.close();
    try {await browser?.call('Browser.close');} catch {}
    browser?.close();
    for(let i=0;i<150&&child.exitCode===null;i++)await pause(100);
    assert.notEqual(child.exitCode,null,'검증 브라우저 정상 종료');
  }
}
await fs.writeFile(path.join(output,'result.json'),JSON.stringify({actualYee:true,mode,reports},null,2));
console.log(`Evidence: ${output}`);
