#!/usr/bin/env node
// Opt-in, bounded real-site observation. Ad absence is never a blocking assertion.
import assert from 'node:assert/strict';
import {spawn, execFile} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {promisify} from 'node:util';
import net from 'node:net';
import http from 'node:http';
import {gzip} from 'node:zlib';
import {connectCDP, closeOwnedBrowser, pause, requireShutdown} from './content_blocking_test_runtime.mjs';
import {sanitizeTraceEvent} from './performance_trace.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
let interrupted;
for(const signal of ['SIGINT','SIGTERM']) process.once(signal,()=>{interrupted=signal;});
function checkInterrupted(){if(interrupted)throw new Error(`Observation cancelled by ${interrupted}`);}
const plan = process.argv[2];
assert.equal(process.platform,'darwin','Real app/audio review currently supports macOS');
assert.ok(['all', 'ads', 'perf', 'smoke', 'smoke-on'].includes(plan), 'Usage: observe-youtube-live.mjs all|ads|perf|smoke|smoke-on [seconds]');
const seconds = Number(process.argv[3] ?? (['all','ads'].includes(plan) ? 600 : 100));
assert.ok(Number.isInteger(seconds) && seconds >= 10 && seconds <= 1200);
const warmupSeconds=Number(process.env.YEE_LIVE_YOUTUBE_WARMUP_SECONDS??seconds);
assert.ok(Number.isInteger(warmupSeconds)&&warmupSeconds>=10&&warmupSeconds<=seconds);
const performanceRounds=Number(process.env.YEE_LIVE_PERFORMANCE_ROUNDS??3);
assert.ok(Number.isInteger(performanceRounds)&&performanceRounds>=1&&performanceRounds<=3);
const performanceGroups=(process.env.YEE_LIVE_PERFORMANCE_GROUPS??'chrome-off,yee-off,yee-on').split(',');
assert.ok(performanceGroups.length>=1&&performanceGroups.length<=4&&
  new Set(performanceGroups).size===performanceGroups.length&&
  performanceGroups.every(g=>['chrome-off','yee-off','yee-on','brave-on'].includes(g)));
assert.ok(['0','1'].includes(process.env.YEE_LIVE_PERFORMANCE_IGNORE_OCCLUSION??'0'));
const ignorePerformanceOcclusion=process.env.YEE_LIVE_PERFORMANCE_IGNORE_OCCLUSION==='1';
assert.ok(['0','1'].includes(process.env.YEE_LIVE_PERFORMANCE_CPU_PROFILE??'0'));
const profilePerformanceCPU=process.env.YEE_LIVE_PERFORMANCE_CPU_PROFILE==='1';
assert.ok(['0','1'].includes(process.env.YEE_LIVE_PERFORMANCE_TRACE??'0'));
const tracePerformance=process.env.YEE_LIVE_PERFORMANCE_TRACE==='1';
const performanceVideo=process.env.YEE_LIVE_PERFORMANCE_VIDEO??null;
assert.ok(performanceVideo===null||/^[A-Za-z0-9_-]{11}$/.test(performanceVideo));
const output = path.join(root, '.local-build/youtube-review', `${plan}-${Date.now()}`);
const adProfile=path.resolve(root,process.env.YEE_LIVE_YOUTUBE_PROFILE??path.join(output,'profile-ad-cohort'));
assert.ok(adProfile.startsWith(path.join(root,'.local-build/youtube-review')+path.sep)&&
  path.basename(adProfile).startsWith('profile-'),'Only isolated review profiles may be reused');
const yeeApp = path.join(root, '.local-build/chromium/src/out/YeePilot/Yee.app');
const chromeApp = '/Applications/Google Chrome.app';
const braveApp = '/Applications/Brave Browser.app';
const shutdown = path.join(root, '.local-build/yee-shutdown-helper/gracefully-quit-yee');
const audioHelper = path.join(root, '.local-build/youtube-review/audio-helper/capture-browser-output-audio');
const videos = (process.env.YEE_LIVE_YOUTUBE_VIDEOS ?? '5EzB_2Qcakw,uq14seOjILU,mfmdXPT7nAM').split(',');
assert.ok(videos.length>=1&&videos.length<=12&&videos.every(v=>/^[A-Za-z0-9_-]{11}$/.test(v)));
const calibration = http.createServer((request,response)=>{
  response.setHeader('Content-Type','text/html');
  response.end('<!doctype html><title>Yee viewport calibration</title>');
});
await new Promise(resolve=>calibration.listen(0,'127.0.0.1',resolve));
const calibrationURL=`http://127.0.0.1:${calibration.address().port}/`;
await fs.mkdir(output, {recursive: true});
if(process.env.YEE_LIVE_YOUTUBE_PROFILE){
  const realRoot=await fs.realpath(path.dirname(output)),realProfile=await fs.realpath(adProfile);
  assert.ok(realProfile.startsWith(realRoot+path.sep)&&path.basename(realProfile).startsWith('profile-'),
    'Review profile must resolve inside the generated review directory');
}
if (plan!=='perf') {
  const helperDirectory=path.dirname(audioHelper), source=path.join(root,'tools/dev/capture-browser-output-audio.swift');
  const cache=path.join(helperDirectory,'module-cache'), info=path.join(helperDirectory,'Info.plist');
  await fs.mkdir(cache,{recursive:true});
  await fs.writeFile(info,`<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>dev.yee.audio-validation</string>
<key>CFBundleName</key><string>Yee output audio validation</string>
<key>NSAudioCaptureUsageDescription</key><string>Record the test browser's output locally to verify advertisement audio. Microphone input is not captured.</string>
</dict></plist>`);
  await promisify(execFile)('xcrun',['swiftc','-parse-as-library','-module-cache-path',cache,source,
    '-Xlinker','-sectcreate','-Xlinker','__TEXT','-Xlinker','__info_plist','-Xlinker',info,'-o',audioHelper],{timeout:60000});
}
const report = {schema: 'yee.youtube-real-site-review.v1', plan, startedAt: new Date().toISOString(),
  seconds, warmupSeconds, videos, performanceRounds, performanceGroups,
  performanceCondition:ignorePerformanceOcclusion?'test-occlusion-override':'native-visible',
  cpuProfiling:profilePerformanceCPU,
  nativeTracing:tracePerformance,
  performanceVideo,
  cases: [], limitations: ['Ad delivery is probabilistic.',
    'Remote debugging is enabled equally for measured browsers; no DOM automation controller.',
    'Chrome and Yee may use different Chromium versions.']};

// Paint metrics are document metrics; SPA transitions have separately named timings.
function installMetrics() {
  if (window === top) performance.mark('yee-review-document-start');
  const state = window.__yeeReview = {started: performance.now(), fcp: null, lcp: null,
    cls: 0, longTasks: [], frames: [],
    visibility:[{time:performance.now(),state:document.visibilityState}],environment: {
      webdriver: navigator.webdriver, domAutomationController: 'domAutomationController' in window}};
  document.addEventListener('visibilitychange',()=>state.visibility.push({time:performance.now(),state:document.visibilityState}));
  for (const type of ['paint', 'largest-contentful-paint', 'layout-shift', 'longtask']) {
    try {
      new PerformanceObserver(list => {
        for (const entry of list.getEntries()) {
          if (type === 'paint' && entry.name === 'first-contentful-paint') state.fcp = entry.startTime;
          if (type === 'largest-contentful-paint') state.lcp = entry.startTime;
          if (type === 'layout-shift' && !entry.hadRecentInput) state.cls += entry.value;
          if (type === 'longtask') state.longTasks.push({start: entry.startTime, duration: entry.duration});
        }
      }).observe({type, buffered: true});
    } catch {}
  }
  let previous;
  const frame = time => {
    if (previous !== undefined) state.frames.push({time, interval: time - previous});
    previous = time;
    if (time < 15000) requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

async function saveReport() {
  report.updatedAt = new Date().toISOString();
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
}
async function evaluate(cdp, expression, awaitPromise = false) {
  const response = await cdp.call('Runtime.evaluate', {expression, awaitPromise,
    returnByValue: true, userGesture: true});
  if (response.exceptionDetails) throw new Error(response.exceptionDetails.exception?.description??response.exceptionDetails.text);
  return response.result.value;
}
async function captureCPUProfile(session, phase) {
  const {profile}=await session.cdp.call('Profiler.stop');
  // Keep public script paths; discard query strings, fragments, and inline URLs.
  for(const node of profile.nodes) {
    try {
      const url=new URL(node.callFrame.url);
      node.callFrame.url=['http:','https:'].includes(url.protocol)?url.origin+url.pathname:'(inline)';
    } catch {node.callFrame.url='';}
  }
  const nodes=new Map(profile.nodes.map(n=>[n.id,n])),parents=new Map(),totals=new Map();
  for(const node of profile.nodes)for(const child of node.children??[])parents.set(child,node.id);
  const key=node=>JSON.stringify([node.callFrame.functionName,node.callFrame.url,node.callFrame.lineNumber]);
  const add=(id,field,ms)=>{
    const node=nodes.get(id);if(!node)return;
    const row=totals.get(key(node))??{function:node.callFrame.functionName,url:node.callFrame.url,
      line:node.callFrame.lineNumber+1,selfMs:0,inclusiveMs:0};
    row[field]+=ms;totals.set(key(node),row);
  };
  for(let i=0;i<(profile.samples?.length??0);i++) {
    const id=profile.samples[i],ms=(profile.timeDeltas?.[i]??0)/1000;
    add(id,'selfMs',ms);
    const visited=new Set();
    for(let current=id;current!==undefined;current=parents.get(current)) {
      const node=nodes.get(current);if(!node)break;
      if(!visited.has(key(node))){add(current,'inclusiveMs',ms);visited.add(key(node));}
    }
  }
  const rows=[...totals.values()].filter(r=>!['(root)','(idle)','(program)'].includes(r.function));
  const file=`${phase}.cpuprofile.gz`;
  await fs.writeFile(path.join(session.directory,file),await promisify(gzip)(JSON.stringify(profile)));
  return {file:path.relative(output,path.join(session.directory,file)),samplingIntervalMicroseconds:1000,
    durationMs:(profile.endTime-profile.startTime)/1000,samples:profile.samples?.length??0,
    topSelf:[...rows].sort((a,b)=>b.selfMs-a.selfMs).slice(0,25),
    topInclusive:[...rows].sort((a,b)=>b.inclusiveMs-a.inclusiveMs).slice(0,25)};
}
async function startPerformanceTrace(session) {
  await session.browserCDP.call('Tracing.start', {transferMode:'ReturnAsStream',streamFormat:'json',
    traceConfig:{recordMode:'recordUntilFull',traceBufferSizeInKb:65536,enableArgumentFilter:true,
      includedCategories:['devtools.timeline','blink','blink.user_timing','loading','v8','cc','viz','input','latencyInfo','brave.adblock']}});
  session.traceActive=true;
}
async function capturePerformanceTrace(session, phase) {
  let timer, unsubscribe;
  const completed=new Promise((resolve,reject)=>{
    unsubscribe=session.browserCDP.on('Tracing.tracingComplete',resolve);
    timer=setTimeout(()=>reject(new Error('Trace completion timeout')),30000);
  });
  let completion;
  try {
    await session.browserCDP.call('Tracing.end');
    session.traceActive=false;
    completion=await completed;
  } finally {clearTimeout(timer);unsubscribe();}
  assert.ok(!completion.dataLossOccurred,'Trace buffer must not lose events');
  assert.ok(completion.stream,'Trace stream must be returned');
  const chunks=[];
  let bytes=0;
  try {
    for (;;) {
      const chunk=await session.browserCDP.call('IO.read',{handle:completion.stream,size:1024*1024});
      const data=Buffer.from(chunk.data,chunk.base64Encoded?'base64':'utf8');
      bytes+=data.length;assert.ok(bytes<=128*1024*1024,'Bounded trace size');chunks.push(data);
      if(chunk.eof)break;
    }
  } finally {await session.browserCDP.call('IO.close',{handle:completion.stream});}
  const trace=JSON.parse(Buffer.concat(chunks).toString('utf8'));
  const events=trace.traceEvents.map(sanitizeTraceEvent);
  const file=`${phase}.trace.json.gz`;
  await fs.writeFile(path.join(session.directory,file),await promisify(gzip)(JSON.stringify({traceEvents:events})));
  return {file:path.relative(output,path.join(session.directory,file)),events:events.length,
    dataLossOccurred:completion.dataLossOccurred,eventArguments:'thread/process-names-and-frame/input-counter-allowlist'};
}
async function processSnapshot(session) {
  const {processInfo}=await session.browserCDP.call('SystemInfo.getProcessInfo');
  const processes=processInfo.filter(p=>Number.isInteger(p.id)&&p.id>0);
  let stdout;
  try {
    ({stdout}=await promisify(execFile)('/bin/ps', ['-o','pid=,rss=','-p',
      processes.map(p=>p.id).join(',')]));
  } catch(error) {stdout=error.stdout??'';}
  const rss=new Map(stdout.trim().split('\n').map(line=>line.trim().split(/\s+/).map(Number)));
  return {processes:processes.map(p=>({pid:p.id,type:p.type,cpuSeconds:p.cpuTime,
    rssKiB:rss.get(p.id)??null})),rssMeaning:'Resident size per process; shared pages may be counted more than once.'};
}
async function launch(label, app, mode, profile, performanceCase=false) {
  if(app===yeeApp) await promisify(execFile)('zsh', ['-c',
    'source "$1"; require_integrated_yee_app_current', 'yee-live-review', path.join(root,'tools/dev/common.zsh')]);
  const {stdout} = await promisify(execFile)('python3',
    [path.join(root, 'tools/dev/browser_bundle_executables.py'), '--running']);
  requireShutdown(JSON.parse(stdout));
  const directory = path.join(output, label); await fs.mkdir(directory, {recursive: true});
  await fs.mkdir(profile, {recursive: true});
  await fs.rm(path.join(profile, 'DevToolsActivePort'), {force: true});
  const reservation = net.createServer();
  await new Promise(resolve=>reservation.listen(0,'127.0.0.1',resolve));
  const port = reservation.address().port;
  await new Promise(resolve=>reservation.close(resolve));
  const log = await fs.open(path.join(directory, 'browser.log'), 'w');
  const args = ['-W', '-n', app, '--args', `--user-data-dir=${profile}`, '--no-first-run',
    '--no-default-browser-check', '--hide-crash-restore-bubble', `--remote-debugging-port=${port}`,
    ...(performanceCase&&ignorePerformanceOcclusion?['--disable-backgrounding-occluded-windows']:[]),
    ...(mode === 'off' ? ['--disable-features=YeeContentBlocking'] : []), 'about:blank'];
  const browser = spawn('/usr/bin/open', args, {stdio: ['ignore', log.fd, log.fd]});
  await new Promise((resolve, reject) => {browser.once('spawn', resolve); browser.once('error', reject);});
  let browserCDP, tabCDP, audio, audioDirectory, audioSpawnError, pid;
  const closeBrowser=()=>closeOwnedBrowser(browser,browserCDP,async()=>{
    if(app===yeeApp&&pid) await promisify(execFile)(shutdown,
      [path.join(app,'Contents/MacOS/Yee'),`--pid=${pid}`]);
    else throw new Error('Browser did not close through Browser.close');
  });
  try {
    let version;
    for (let i = 0; i < 200; i++) {
      try {version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); break;}
      catch {await pause(100);}
      if (browser.exitCode !== null) throw new Error('Browser exited before DevTools startup');
    }
    assert.ok(version, 'DevTools startup');
    browserCDP = await connectCDP(version.webSocketDebuggerUrl);
    const processes = await browserCDP.call('SystemInfo.getProcessInfo');
    pid = processes.processInfo.find(p => p.type === 'browser').id;
    const tabs = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const tab = tabs.find(t => t.type === 'page'&&t.url==='about:blank')??tabs.find(t=>t.type==='page'); assert.ok(tab);
    tabCDP = await connectCDP(tab.webSocketDebuggerUrl, undefined, 45000);
    await tabCDP.call('Page.enable');
    await tabCDP.call('Performance.enable');
    await tabCDP.call('Page.addScriptToEvaluateOnNewDocument', {source: `(${installMetrics})();`});
    let braveFilters;
    if(app===braveApp) {
      await tabCDP.call('Page.navigate',{url:'brave://adblock-internals/'});
      await pause(500);
      const started=Date.now();
      while(Date.now()-started<90000) {
        checkInterrupted();
        braveFilters=await evaluate(tabCDP,`(async()=>{
          // Use the page's existing WebUI bridge. Importing another cr module
          // conflicts with the global bridge already installed by the page.
          const bridge=window.cr,original=bridge.webUIResponse;
          const id='yee-filter-review-'+crypto.randomUUID();
          const info=await new Promise((resolve,reject)=>{
            const restore=()=>{bridge.webUIResponse=original;clearTimeout(timer);};
            const timer=setTimeout(()=>{restore();reject(new Error('Brave filter info timeout'));},10000);
            bridge.webUIResponse=(callbackId,success,response)=>{
              if(callbackId!==id)return original(callbackId,success,response);
              restore();success?resolve(response):reject(new Error('Brave filter info unavailable'));
            };
            chrome.send('brave_adblock_internals.getDebugInfo',[id]);
          });
          const summarize=engine=>({flatbufferBytes:engine.flatbuffer_size,
            sources:(engine.source_info??[]).map(source=>({title:source.title,
              networkRules:source.network_filter_count,cosmeticRules:source.cosmetic_filter_count}))});
          return {debugMode:info.debug_mode,default:summarize(info.default_engine),
            additional:summarize(info.additional_engine)};
        })()`,true);
        const sources=[...braveFilters.default.sources,...braveFilters.additional.sources];
        if(sources.some(source=>source.networkRules>1000))break;
        await pause(2000);
      }
      braveFilters.waitedMs=Date.now()-started;
      assert.equal(braveFilters.debugMode,false,'Brave adblock debug mode adds overhead');
      assert.ok([...braveFilters.default.sources,...braveFilters.additional.sources]
        .some(source=>source.networkRules>1000),'Brave must load its actual filter lists before timing');
    }
    await tabCDP.call('Page.navigate',{url:calibrationURL});
    await pause(800);
    await tabCDP.call('Page.bringToFront');
    const window = await browserCDP.call('Browser.getWindowForTarget', {targetId:tab.id});
    for (let attempt=0;attempt<3;attempt++) {
      const viewport = await evaluate(tabCDP,'({width:innerWidth,height:innerHeight})');
      if (viewport.width===1000 && viewport.height===720) break;
      const {bounds} = await browserCDP.call('Browser.getWindowBounds',{windowId:window.windowId});
      await browserCDP.call('Browser.setWindowBounds',{windowId:window.windowId,bounds:{
        windowState:'normal',width:bounds.width+1000-viewport.width,height:bounds.height+720-viewport.height}});
      await pause(300);
    }
    await tabCDP.call('Page.bringToFront');
    // macOS window restoration can briefly undo the first activation. Wait for
    // stable foreground state on the inert page, before starting any timings.
    let stableSince, foregroundReady=false, lastForeground;
    const foregroundStart=Date.now();
    while(Date.now()-foregroundStart<45000) {
      checkInterrupted();
      const foreground=await evaluate(tabCDP,'({visible:document.visibilityState==="visible",focused:document.hasFocus(),width:innerWidth,height:innerHeight})');
      lastForeground=foreground;
      if(foreground.width!==1000||foreground.height!==720) {
        const {bounds}=await browserCDP.call('Browser.getWindowBounds',{windowId:window.windowId});
        await browserCDP.call('Browser.setWindowBounds',{windowId:window.windowId,bounds:{windowState:'normal',
          width:bounds.width+1000-foreground.width,height:bounds.height+720-foreground.height}});
        stableSince=undefined;
      } else if(foreground.visible&&(foreground.focused||(performanceCase&&ignorePerformanceOcclusion))) {
        stableSince??=Date.now();
        if(Date.now()-stableSince>=2500){foregroundReady=true;break;}
      } else {stableSince=undefined;await tabCDP.call('Page.bringToFront');}
      await pause(250);
    }
    assert.ok(foregroundReady,`Measurement viewport did not stabilize: ${JSON.stringify(lastForeground)}`);
    return {label, directory, profile, browser, browserCDP, cdp: tabCDP, version: version.Browser, pid,braveFilters,windowId:window.windowId,
      async startAudio(duration,recordDirectory=directory) {
        audioDirectory=recordDirectory;
        audioSpawnError=undefined;
        await fs.writeFile(path.join(audioDirectory, 'audio-state.json'), '{"ad":false}');
        const audioLog = await fs.open(path.join(audioDirectory, 'audio.log'), 'w');
        audio = spawn(audioHelper, [String(pid), audioDirectory, String(duration + 10)],
          {stdio: ['ignore', audioLog.fd, audioLog.fd]});
        audio.once('error', error => {audioSpawnError=error;}); await audioLog.close();
      },
      async stopAudio() {
        if (audio) {
          await fs.writeFile(path.join(audioDirectory, 'audio-stop'), '');
          for (let i = 0; !audioSpawnError && audio.exitCode === null && audio.signalCode === null && i < 50; i++) await pause(100);
          assert.ok(audioSpawnError || audio.exitCode !== null || audio.signalCode !== null, 'Audio capture stopped');
          audio=undefined;
        }
      },
      async close() {
        try {await this.stopAudio();}
        finally {
          if(this.traceActive)try {await browserCDP.call('Tracing.end');} catch {}
          tabCDP.close();
          try {await closeBrowser();} finally {await log.close();}
        }
      }};
  } catch (error) {
    try {await closeBrowser();}
    finally {browserCDP?.close();tabCDP?.close();await log.close();}
    throw error;
  }
}

async function observeAdCase(mode, video, duration, suffix = '', sharedSession) {
  const label = `${mode}-${video}${suffix}`;
  const profile = adProfile;
  const session = sharedSession??await launch(label, yeeApp, mode, profile);
  const caseDirectory=path.join(output,label);await fs.mkdir(caseDirectory,{recursive:true});
  const observation = {label, mode, video, seconds:duration, profileKey:path.relative(root,session.profile),
    version: session.version, samples: [],
    adSegments: [], preRoll: false, midRoll: false, contentSeconds: 0, error: null};
  report.cases.push(observation); await saveReport();
  try {
    await session.cdp.call('Page.navigate', {url: `https://www.youtube.com/watch?v=${video}`});
    const start = Date.now(); let previousTime, previousAd = false, contentAfterMidroll = 0;
    let screenshots = 0, stalled = 0, audioStarted=false;
    while ((Date.now() - start) / 1000 < duration) {
      checkInterrupted();
      await pause(1000);
      let sample;
      try {
        sample = await evaluate(session.cdp, `(() => {
          const p=document.getElementById('movie_player'),v=p?.querySelector('video');
          const visible=e=>!!e&&e.getClientRects().length>0;
          const error=document.querySelector('.ytp-error-content-wrap');
          if(v){v.muted=false;v.volume=0.2;if(v.paused&&!v.ended)v.play().catch(()=>{});}
          return {unixMs:Date.now(),ad:!!p&&(p.classList.contains('ad-showing')||p.classList.contains('ad-interrupting')),
            time:v?.currentTime??null,duration:Number.isFinite(v?.duration)?v.duration:null,
            paused:v?.paused??true,muted:v?.muted??null,readyState:v?.readyState??0,
            visibility:document.visibilityState,hasFocus:document.hasFocus(),
            error:v?.error?.code??(visible(error)?error.textContent.trim().slice(0,160):null),
            metadata:window.ytInitialPlayerResponse?.videoDetails?{
              videoId:window.ytInitialPlayerResponse.videoDetails.videoId,
              title:window.ytInitialPlayerResponse.videoDetails.title,
              author:window.ytInitialPlayerResponse.videoDetails.author,
              lengthSeconds:window.ytInitialPlayerResponse.videoDetails.lengthSeconds}:null,
            adUI:[...document.querySelectorAll('.ytp-ad-text,.ytp-ad-badge__text,.ytp-ad-skip-button,.ytp-ad-skip-button-modern,.ytp-ad-preview-text')]
              .filter(visible).map(e=>e.textContent.trim().slice(0,80)),environment:window.__yeeReview?.environment};
        })()`);
      } catch (error) {
        if (error.message.includes('context')) continue;
        throw error;
      }
      observation.samples.push(sample);
      if(!audioStarted&&sample.time>0&&sample.readyState>=2&&!sample.paused){
        await session.startAudio(duration,caseDirectory);audioStarted=true;
      }
      if (sample.environment) assert.deepEqual(sample.environment,
        {webdriver:false,domAutomationController:false},'Ordinary debugger environment');
      await fs.writeFile(path.join(caseDirectory, 'audio-state.json'), JSON.stringify({ad:sample.ad}));
      if (sample.ad) {
        if (observation.contentSeconds >= 20) observation.midRoll = true;
        else observation.preRoll = true;
        if(!previousAd)observation.adSegments.push({
          phase:observation.contentSeconds>=20?'midroll':'preroll',
          contentSecondsBefore:observation.contentSeconds,startUnixMs:sample.unixMs,lastUnixMs:sample.unixMs});
        observation.adSegments.at(-1).lastUnixMs=sample.unixMs;
      }
      const delta = sample.time !== null && previousTime !== undefined ? sample.time - previousTime : 0;
      if (!sample.ad && !previousAd && !sample.paused && delta > 0 && delta < 3) {
        observation.contentSeconds += delta;
        if (observation.midRoll) contentAfterMidroll += delta;
      }
      stalled = !sample.ad && !sample.paused && delta >= 0 && delta < 0.05 ? stalled + 1 : 0;
      if (sample.ad && !previousAd && screenshots < 6) {
        const shot = await session.cdp.call('Page.captureScreenshot', {format:'png'});
        await fs.writeFile(path.join(caseDirectory, `advertisement-${++screenshots}.png`), Buffer.from(shot.data,'base64'));
      }
      previousTime = sample.time; previousAd = sample.ad;
      if (sample.error || stalled > 30) {
        observation.error = sample.error ?? (sample.visibility==='hidden'?'measurement-occluded':'content-stalled');break;
      }
      if (mode === 'off' && observation.midRoll && !sample.ad && contentAfterMidroll >= 20 &&
          observation.contentSeconds>=60) break;
    }
    observation.elapsedSeconds = (Date.now()-start)/1000;
    observation.last = observation.samples.at(-1);
    observation.environment = observation.last?.environment;
    observation.playbackPassed = !observation.error && observation.contentSeconds >= 60 && !observation.last?.paused;
  } finally {
    try {await evaluate(session.cdp,"document.querySelector('#movie_player video')?.pause()");} catch {}
    if(sharedSession)await session.stopAudio();else await session.close();
    await saveReport();
  }
  try {
    const lines = (await fs.readFile(path.join(caseDirectory,'output-audio.jsonl'),'utf8')).trim().split('\n').filter(Boolean).map(JSON.parse);
    const adSeconds = new Set(observation.samples.filter(s=>s.ad).map(s=>Math.floor(s.unixMs/1000)));
    const adAudio = lines.filter(l=>adSeconds.has(l.unixSecond));
    for(const segment of observation.adSegments) {
      const segmentSeconds=new Set(observation.samples.filter(s=>s.ad&&
        s.unixMs>=segment.startUnixMs&&s.unixMs<=segment.lastUnixMs).map(s=>Math.floor(s.unixMs/1000)));
      const segmentAudio=lines.filter(l=>segmentSeconds.has(l.unixSecond));
      segment.observedSpanSeconds=(segment.lastUnixMs-segment.startUnixMs)/1000;
      segment.audioCapturedSeconds=segmentAudio.length;
      segment.audioNonSilentSeconds=segmentAudio.filter(l=>l.rms>0.0001).length;
    }
    observation.outputAudio = {capturedSeconds:lines.length, adCapturedSeconds:adAudio.length,
      adNonSilentSeconds:adAudio.filter(l=>l.rms>0.0001).length,
      contentNonSilentSeconds:lines.filter(l=>!adSeconds.has(l.unixSecond)&&l.rms>0.0001).length,
      captureSummary:JSON.parse(await fs.readFile(path.join(caseDirectory,'output-audio-summary.json'),'utf8'))};
  } catch (error) {observation.outputAudio = {unavailable:error.message};}
  await saveReport();
  console.log(JSON.stringify({label,contentSeconds:observation.contentSeconds,preRoll:observation.preRoll,
    midRoll:observation.midRoll,error:observation.error,audio:observation.outputAudio}));
  return observation;
}

async function observePerformance(browserName, mode, ordinal) {
  checkInterrupted();
  const label = `${browserName}-${mode}-${ordinal}`;
  const app={yee:yeeApp,chrome:chromeApp,brave:braveApp}[browserName];
  const session = await launch(label,app,mode,path.join(output,`profile-${label}`),true);
  const observation = {label,browser:browserName,mode,version:session.version,
    ...(session.braveFilters?{braveFilters:session.braveFilters}:{})};
  report.cases.push(observation);
  try {
    observation.processStart=await processSnapshot(session);
    observation.windowStart=(await session.browserCDP.call('Browser.getWindowBounds',{windowId:session.windowId})).bounds;
    if(tracePerformance)await startPerformanceTrace(session);
    if(profilePerformanceCPU) {
      await session.cdp.call('Profiler.enable');
      await session.cdp.call('Profiler.setSamplingInterval',{interval:1000});
      await session.cdp.call('Profiler.start');
    }
    await session.cdp.call('Page.navigate',{url:'https://www.youtube.com/results?search_query=veritasium'});
    await pause(20000);
    checkInterrupted();
    if(profilePerformanceCPU)observation.coldCPU=await captureCPUProfile(session,'cold');
    if(tracePerformance)observation.coldTrace=await capturePerformanceTrace(session,'cold');
    observation.cold = await evaluate(session.cdp, `({...window.__yeeReview,
      navigation:performance.getEntriesByType('navigation')[0]?.toJSON(),
      elements:document.getElementsByTagName('*').length,
      videos:document.querySelectorAll('ytd-video-renderer').length,
      viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio,outerWidth,outerHeight},
      visibilityState:document.visibilityState,hasFocus:document.hasFocus(),
      resources:performance.getEntriesByType('resource').reduce((out,e)=>{
        const k=e.initiatorType||'other';const s=out[k]??={count:0,transferBytes:0,decodedBytes:0};
        s.count++;s.transferBytes+=e.transferSize;s.decodedBytes+=e.decodedBodySize;return out;},{})})`);
    observation.windowCold=(await session.browserCDP.call('Browser.getWindowBounds',{windowId:session.windowId})).bounds;
    assert.deepEqual(observation.cold.environment,{webdriver:false,domAutomationController:false});
    assert.equal(observation.cold.viewport.width,1000,'Matched content width');
    assert.equal(observation.cold.viewport.height,720,'Matched content height');
    assert.ok(observation.cold.visibility.every(v=>v.state==='visible'),'Cold measurement was occluded');
    if(!ignorePerformanceOcclusion)assert.ok(observation.cold.hasFocus,'Native cold window must have focus');
    observation.processCold=await processSnapshot(session);
    if(tracePerformance)await startPerformanceTrace(session);
    await evaluate(session.cdp, `(()=>{
      performance.mark('yee-review-scroll-start');
      const start=performance.now(),state=window.__yeeScroll={frames:[],longTasks:[],visibility:[],
        initialY:scrollY,finished:false};let previous;
      const observer=new PerformanceObserver(list=>state.longTasks.push(...list.getEntries().map(e=>({start:e.startTime-start,duration:e.duration}))));
      observer.observe({type:'longtask'});
      const frame=t=>{state.visibility.push(document.visibilityState);
        if(previous!==undefined)state.frames.push(t-previous);previous=t;
        if(!state.finished)requestAnimationFrame(frame)};requestAnimationFrame(frame);
      state.stop=()=>{performance.mark('yee-review-scroll-end');state.finished=true;observer.disconnect();const {stop,...result}=state;
        return {...result,elapsed:performance.now()-start,finalY:scrollY,visibilityState:document.visibilityState,hasFocus:document.hasFocus()}};
    })()`);
    observation.scrollPositions=[];
    for(const distance of [-600,600,-600,600]) {
      await session.cdp.call('Input.synthesizeScrollGesture',{x:700,y:350,yDistance:distance,speed:500,gestureSourceType:'mouse'});
      observation.scrollPositions.push(await evaluate(session.cdp,'scrollY'));
      await pause(300);
    }
    observation.scroll=await evaluate(session.cdp,'window.__yeeScroll.stop()');
    assert.equal(observation.scroll.visibilityState,'visible');
    if(!ignorePerformanceOcclusion)assert.ok(observation.scroll.hasFocus,'Native scroll window must have focus');
    assert.ok(observation.scroll.visibility.length>0&&observation.scroll.visibility.every(v=>v==='visible'),'Scroll measurement was occluded');
    assert.ok(observation.scrollPositions.some(y=>Math.abs(y-observation.scroll.initialY)>100),'Scroll input moved the page');
    if(tracePerformance)observation.scrollTrace=await capturePerformanceTrace(session,'scroll');
    if(tracePerformance)await startPerformanceTrace(session);
    if(profilePerformanceCPU)await session.cdp.call('Profiler.start');
    await evaluate(session.cdp, `(()=>{
      const expected=${JSON.stringify(report.performanceVideo)};
      const link=[...document.querySelectorAll('ytd-search ytd-video-renderer a[href^="/watch?v="]')].find(a=>
        !a.closest('ytd-ad-slot-renderer,ytd-promoted-video-renderer,ytd-in-feed-ad-layout-renderer')&&
        a.getClientRects().length&&(!expected||new URL(a.href).searchParams.get('v')===expected));
      if(!link)throw new Error('No matching organic search video link');
      const start=performance.now(),state=window.__yeeTransition={started:start,frames:[],longTasks:[],
        selectedVideoId:new URL(link.href).searchParams.get('v'),watchTitleAt:null,playerReadyAt:null,playerReadyIsAd:null,
        visibility:[],finished:false};let previous;
      const observer=new PerformanceObserver(list=>state.longTasks.push(...list.getEntries().map(e=>({start:e.startTime-start,duration:e.duration}))));
      observer.observe({type:'longtask'});
      const frame=t=>{if(previous!==undefined)state.frames.push(t-previous);previous=t;if(!state.finished)requestAnimationFrame(frame)};
      requestAnimationFrame(frame);
      const poll=setInterval(()=>{
        state.visibility.push(document.visibilityState);
        const title=document.querySelector('ytd-watch-metadata h1');
        if(state.watchTitleAt===null&&title?.getClientRects().length&&title.textContent.trim())state.watchTitleAt=performance.now()-start;
        const player=document.getElementById('movie_player'),video=player?.querySelector('video');
        if(state.playerReadyAt===null&&video?.readyState>=2){
          state.playerReadyAt=performance.now()-start;state.playerReadyIsAd=player.classList.contains('ad-showing');}
      },100);
      state.stop=()=>{state.finished=true;clearInterval(poll);observer.disconnect();
        document.querySelector('#movie_player video')?.pause();
        const {stop,...result}=state;return {...result,elapsed:performance.now()-start,
          finalVideoId:new URL(location.href).searchParams.get('v'),
          visibilityState:document.visibilityState,hasFocus:document.hasFocus(),
          viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio}}};
      link.click();return state.selectedVideoId;
    })()`);
    await pause(10000);
    checkInterrupted();
    if(profilePerformanceCPU)observation.transitionCPU=await captureCPUProfile(session,'transition');
    if(tracePerformance)observation.transitionTrace=await capturePerformanceTrace(session,'transition');
    observation.transition = await evaluate(session.cdp,'window.__yeeTransition.stop()');
    report.performanceVideo??=observation.transition.selectedVideoId;
    assert.equal(observation.transition.visibilityState,'visible');
    if(!ignorePerformanceOcclusion)assert.ok(observation.transition.hasFocus,'Native transition window must have focus');
    assert.ok(observation.transition.visibility.every(v=>v==='visible'),'Transition measurement was occluded');
    assert.equal(observation.transition.selectedVideoId,observation.transition.finalVideoId,'Expected video navigation');
    assert.equal(observation.transition.viewport.width,1000);
    assert.equal(observation.transition.viewport.height,720);
    observation.metrics = await session.cdp.call('Performance.getMetrics');
    observation.processEnd=await processSnapshot(session);
  } finally {await session.close(); await saveReport();}
  console.log(`${label} completed`);
}

try {
  if (plan!=='perf') {
    const mode=plan==='smoke-on'?'on':'off';
    const cohort=await launch(`${mode}-cohort`,yeeApp,mode,adProfile);
    let matched;
    try {
      const selected=plan.startsWith('smoke')?[videos[0]]:videos;
      for (let i=0;i<selected.length;i++) {
        const duration=i===selected.length-1?seconds:warmupSeconds;
        const control=await observeAdCase(mode,selected[i],duration,'',cohort);
        if(mode==='off'&&control.midRoll&&!control.error){matched=control;break;}
      }
    } finally {await cohort.close();}
    if(matched) {
      const protectedCase=await observeAdCase('on',matched.video,seconds);
      const midroll=matched.adSegments.find(s=>s.phase==='midroll');
      const positionCovered=!!midroll&&protectedCase.contentSeconds>=midroll.contentSecondsBefore;
      report.midrollComparison={control:matched.label,protected:protectedCase.label,
        scope:'observed-video-profile-interval',contentPositionCovered:positionCovered,
        status:matched.playbackPassed&&midroll?.audioNonSilentSeconds>0&&
          protectedCase.playbackPassed&&positionCovered&&!protectedCase.preRoll&&!protectedCase.midRoll&&
          protectedCase.outputAudio?.contentNonSilentSeconds>=60?'observed-off-on-contrast':'inconclusive'};
    }
    report.midrollControlObserved = report.cases.some(c=>c.mode==='off'&&c.midRoll&&!c.error);
    await saveReport();
  }
  if (plan==='perf'||plan==='all') {
    console.log('Starting alternating browser performance comparison');
    // Alternate browser order to expose warm-network/order effects.
    for (let ordinal=1;ordinal<=performanceRounds;ordinal++) {
      for (const [browser,mode] of ordinal%2 ? [['yee','on'],['chrome','off'],['yee','off'],['brave','on']] :
        [['brave','on'],['chrome','off'],['yee','off'],['yee','on']]) {
        if(performanceGroups.includes(`${browser}-${mode}`)) await observePerformance(browser,mode,ordinal);
      }
    }
  }
  report.completedAt = new Date().toISOString(); await saveReport();
  console.log(`Observation complete: ${path.join(output,'report.json')}`);
} catch (error) {
  report[interrupted?'cancelledAt':'failedAt']=new Date().toISOString();
  report.failure=error.message;await saveReport();
  console.error(error.message);process.exitCode=interrupted?(interrupted==='SIGINT'?130:143):1;
} finally {await new Promise(resolve=>calibration.close(resolve));}
