#!/usr/bin/env node
// 실제 Windows 앱에서 H.264/AAC의 직접 재생과 MSE 디코딩을 확인한다.
import assert from 'node:assert/strict';
import {execFileSync, spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {closeOwnedBrowser, connectCDP, pause, requireShutdown, waitForShutdown} from './content_blocking_test_runtime.mjs';

const [executableArg, chromiumArg] = process.argv.slice(2);
assert.ok(executableArg && chromiumArg,
  '사용법: node tools/dev/test-media-codecs.mjs YEE_EXECUTABLE CHROMIUM_SRC');
assert.equal(process.platform, 'win32', '이 실제 앱 검사는 Windows용이다');
const executable = await fs.realpath(path.resolve(executableArg));
const chromium = await fs.realpath(path.resolve(chromiumArg));
// Chromium의 기존 공개 시험 파일로 일반 MP4와 조각 MP4의 재생 경로를 나눈다.
const fixtures = {};
for (const [mode, name] of Object.entries({
  direct: 'bear-1280x720.mp4', mse: 'bear-640x360-av_frag.mp4',
})) {
  const filename = path.join(chromium, 'media/test/data', name);
  const media = await fs.readFile(filename);
  fixtures[mode] = {filename, media, sha256: createHash('sha256').update(media).digest('hex')};
}
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
await fs.mkdir(path.join(root, '.local-build'), {recursive: true});
const output = await fs.mkdtemp(path.join(root, '.local-build/media-codecs-'));

function runningProcesses() {
  const command = '@(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @(\'chrome.exe\',\'yee.exe\') } | Select-Object ProcessId,ExecutablePath) | ConvertTo-Json -Compress';
  const raw = execFileSync('powershell.exe', ['-NoProfile', '-Command', command],
    {encoding: 'utf8', windowsHide: true});
  const processes = JSON.parse(raw.trim() || '[]');
  return (Array.isArray(processes) ? processes : [processes])
    .filter(item => item.ExecutablePath?.toLowerCase() === executable.toLowerCase());
}

requireShutdown(runningProcesses());
// 긴 작업 폴더 경로가 Chromium의 캐시 저장을 방해하지 않도록 프로필은 Temp에 둔다.
const profile = await fs.mkdtemp(path.join(os.tmpdir(), 'yee-media-'));
const server = http.createServer((request, response) => {
  const fixture = fixtures[request.url?.match(/^\/(direct|mse)\.mp4$/)?.[1]];
  if (!fixture) {
    response.writeHead(200, {'Content-Type': 'text/html; charset=utf-8'});
    response.end('<!doctype html><title>Yee H.264/AAC 검사</title><h1>H.264/AAC 실제 디코딩 검사</h1>');
    return;
  }
  const media = fixture.media;
  const range = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range ?? '');
  const start = range ? Number(range[1]) : 0;
  const end = range?.[2] ? Number(range[2]) : media.length - 1;
  if (start > end || end >= media.length) {
    response.writeHead(416, {'Content-Range': `bytes */${media.length}`});
    response.end();
    return;
  }
  response.writeHead(range ? 206 : 200, {'Content-Type': 'video/mp4',
    'Accept-Ranges': 'bytes', 'Content-Length': end - start + 1,
    ...(range ? {'Content-Range': `bytes ${start}-${end}/${media.length}`} : {})});
  response.end(media.subarray(start, end + 1));
});

let child, browser, tab;
const report = {executable, chromium, profile,
  fixtures: Object.fromEntries(Object.entries(fixtures).map(([mode, fixture]) =>
    [mode, {filename: fixture.filename, sha256: fixture.sha256}])), cases: [], passed: false};
const mediaEvents = [];
async function evaluate(expression) {
  const result = await tab.call('Runtime.evaluate',
    {expression, awaitPromise: true, returnByValue: true, userGesture: true});
  assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}

async function shutdown() {
  if (child) await closeOwnedBrowser(child, browser, pid => {
    assert.ok(Number.isInteger(pid) && pid > 0, '검사가 실행한 프로세스만 종료한다');
    execFileSync('powershell.exe', ['-NoProfile', '-Command',
      `$codecBrowser = Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if ($codecBrowser) { [void]$codecBrowser.CloseMainWindow() }`],
    {windowsHide: true});
  });
  tab?.close(); browser?.close();
  await waitForShutdown(runningProcesses);
}

try {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const reservation = net.createServer();
  await new Promise(resolve => reservation.listen(0, '127.0.0.1', resolve));
  const port = reservation.address().port;
  await new Promise(resolve => reservation.close(resolve));
  child = spawn(executable, [`--user-data-dir=${profile}`, `--remote-debugging-port=${port}`,
    '--remote-debugging-address=127.0.0.1', '--no-first-run', '--no-default-browser-check',
    '--hide-crash-restore-bubble', '--disable-sync', '--mute-audio',
    '--autoplay-policy=no-user-gesture-required', '--disable-backgrounding-occluded-windows', 'about:blank'],
    {windowsHide: true, stdio: ['ignore', 'ignore', 'pipe']});
  let spawnError;
  child.on('error', error => {spawnError = error;});
  child.stderr.on('data', data => fs.appendFile(path.join(output, 'browser.log'), data).catch(() => {}));
  let version;
  for (let attempt = 0; attempt < 200; ++attempt) {
    if (spawnError) throw spawnError;
    assert.equal(child.exitCode, null, '검사 앱 시작 실패');
    try {version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); break;} catch {}
    await pause(100);
  }
  assert.ok(version, '명시적 디버깅 포트의 준비 시간 초과');
  report.version = version.Browser;
  browser = await connectCDP(version.webSocketDebuggerUrl);
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = targets.find(item => item.type === 'page' && item.url === 'about:blank');
  assert.ok(target, '검사 프로필의 빈 탭만 선택한다');
  tab = await connectCDP(target.webSocketDebuggerUrl, undefined, 45000);
  await tab.call('Page.enable');
  await tab.call('Media.enable');
  for (const method of ['Media.playerPropertiesChanged', 'Media.playerEventsAdded',
    'Media.playerErrorsRaised', 'Media.playerMessagesLogged']) {
    tab.on(method, data => mediaEvents.push({method, data}));
  }
  const capabilities = await evaluate(`(() => {
    const video = document.createElement('video');
    return {webdriver:navigator.webdriver, domAutomationController:'domAutomationController' in window,
      codecs:['video/mp4; codecs="avc1.64001E"','audio/mp4; codecs="mp4a.40.2"'].map(type =>
        ({type, canPlayType:video.canPlayType(type), mse:MediaSource.isTypeSupported(type)}))};
  })()`);
  report.capabilities = capabilities;
  assert.equal(capabilities.webdriver, false, '자동화 표시 없는 일반 앱으로 검사한다');
  assert.equal(capabilities.domAutomationController, false);
  for (const codec of capabilities.codecs) {
    assert.ok(codec.canPlayType, `직접 재생 코덱 지원: ${codec.type}`);
    assert.equal(codec.mse, true, `MSE 코덱 지원: ${codec.type}`);
  }
  for (const mode of ['direct', 'mse']) {
    await tab.call('Page.navigate', {url: origin});
    let ready = false;
    for (let attempt = 0; attempt < 100; ++attempt) {
      try {
        ready = await evaluate(`location.origin === ${JSON.stringify(origin)} && document.readyState === 'complete'`);
      } catch (error) {
        if (!/context.*destroyed|Cannot find context/i.test(String(error))) throw error;
      }
      if (ready) break;
      await pause(100);
    }
    assert.ok(ready, '검사 페이지 준비 시간 초과');
    await tab.call('Page.bringToFront');
    const firstEvent = mediaEvents.length;
    const state = await evaluate(`(async () => {
      const mode=${JSON.stringify(mode)}, video=document.createElement('video');
      video.controls=true; document.body.append(video);
      let objectURL, timer;
      try {
        const decode=(async () => {
          const ended=new Promise((resolve,reject) => {
            video.addEventListener('ended',resolve,{once:true});
            video.addEventListener('error',()=>reject(new Error(video.error?.message||'미디어 오류')),{once:true});
          });
          if (mode==='direct') video.src='/direct.mp4';
          else {
            const source=new MediaSource();
            const opened=new Promise(resolve=>source.addEventListener('sourceopen',resolve,{once:true}));
            objectURL=URL.createObjectURL(source); video.src=objectURL; await opened;
            const buffer=source.addSourceBuffer('video/mp4; codecs="avc1.64001E,mp4a.40.2"');
            const updated=new Promise((resolve,reject)=>{
              buffer.addEventListener('updateend',resolve,{once:true});
              buffer.addEventListener('error',()=>reject(new Error('MSE append 오류')),{once:true});
            });
            buffer.appendBuffer(await (await fetch('/mse.mp4')).arrayBuffer());
            await updated; source.endOfStream();
          }
          await video.play(); await ended;
          return {ended:video.ended,time:video.currentTime,duration:video.duration,
            width:video.videoWidth,height:video.videoHeight,error:video.error?.message??null,
            audioDecodedBytes:video.webkitAudioDecodedByteCount,videoDecodedBytes:video.webkitVideoDecodedByteCount,
            totalVideoFrames:video.getVideoPlaybackQuality().totalVideoFrames};
        })();
        return await Promise.race([decode,new Promise((_,reject)=>{
          timer=setTimeout(()=>reject(new Error('실제 디코딩 시간 초과')),30000);
        })]);
      } catch (error) {throw new Error(video.error?.message || error.message || String(error));}
      finally {clearTimeout(timer); video.remove(); if(objectURL) URL.revokeObjectURL(objectURL);}
    })()`);
    assert.equal(state.ended, true, `${mode}: 재생 종료 도달`);
    assert.equal(state.error, null, `${mode}: 미디어 오류 없음`);
    assert.ok(state.time > 1 && state.width > 0 && state.height > 0, `${mode}: 영상 재생`);
    assert.ok(state.audioDecodedBytes > 0, `${mode}: AAC 실제 디코딩`);
    assert.ok(state.videoDecodedBytes > 0 && state.totalVideoFrames > 0, `${mode}: H.264 실제 디코딩`);
    await pause(100);
    const events = mediaEvents.slice(firstEvent);
    assert.ok(events.some(event => event.data.properties?.some(property =>
      property.name === 'kAudioTracks' && /"codec":"aac"/.test(property.value))), `${mode}: AAC 트랙 확인`);
    assert.ok(events.some(event => event.data.properties?.some(property =>
      property.name === 'kVideoTracks' && /"codec":"h264"/.test(property.value))), `${mode}: H.264 트랙 확인`);
    report.cases.push({mode, state, mediaEvents: events});
  }
  report.passed = true;
} catch (error) {
  // 묶음으로 전달되는 미디어 진단 이벤트도 실패 보고서에 남긴다.
  await pause(1000);
  report.mediaEvents = mediaEvents;
  report.failure = String(error.stack ?? error);
  throw error;
} finally {
  try {
    await shutdown();
    const resolvedProfile = await fs.realpath(profile);
    const resolvedTemp = await fs.realpath(os.tmpdir());
    assert.equal(path.dirname(resolvedProfile).toLowerCase(), resolvedTemp.toLowerCase());
    assert.ok(path.basename(resolvedProfile).startsWith('yee-media-'));
    await fs.rm(resolvedProfile, {recursive: true});
    report.profileRemoved = true;
  } catch (error) {
    report.passed = false;
    report.failure = String(error.stack ?? error);
    throw error;
  } finally {
    await new Promise(resolve => server.close(resolve));
    await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
  }
}
console.log(JSON.stringify({passed: true, output, capabilities:report.capabilities,
  cases: report.cases.map(item => ({mode:item.mode,...item.state}))}));
