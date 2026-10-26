#!/usr/bin/env node
// 실제 앱을 정상 종료·재시작해 그룹과 사용자 시작 설정의 복원을 검증한다.
import assert from 'node:assert/strict';
import {execFileSync, spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {closeOwnedBrowser, connectCDP, pause, requireShutdown, waitForShutdown} from './content_blocking_test_runtime.mjs';

const [executableArg] = process.argv.slice(2);
assert.ok(executableArg, 'Usage: node tools/dev/test-group-session-restore.mjs YEE_EXECUTABLE');
assert.equal(process.platform, 'win32', '이 실행 검증은 Windows용이다');
const executable = path.resolve(executableArg);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
await fs.mkdir(path.join(root, '.local-build'), {recursive: true});
const output = await fs.mkdtemp(path.join(root, '.local-build/group-session-'));
const profile = path.join(output, 'profile');
const extension = path.join(output, 'extension');
await fs.mkdir(extension);
await fs.writeFile(path.join(extension, 'manifest.json'), JSON.stringify({
  manifest_version: 3, name: 'Yee 그룹 복원 검사', version: '1.0',
  permissions: ['tabs', 'tabGroups'], background: {service_worker: 'worker.js'}
}));
await fs.writeFile(path.join(extension, 'worker.js'),
  'chrome.runtime.onInstalled.addListener(() => {});\nchrome.runtime.onStartup.addListener(() => {});\nglobalThis.yeeGroupSessionReady = true;\n');

function runningProcesses() {
  const raw = execFileSync('powershell.exe', ['-NoProfile', '-Command',
    '@(Get-Process chrome,yee -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path) | ConvertTo-Json -Compress'],
    {encoding: 'utf8', windowsHide: true}).trim();
  const processes = JSON.parse(raw || '[]');
  return (Array.isArray(processes) ? processes : [processes])
    .filter(p => p?.toLowerCase() === executable.toLowerCase());
}

const server = http.createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8');
  response.end(`<!doctype html><meta charset="utf-8"><title>그룹 복원 ${request.url}</title><p>Yee 그룹 복원 검사</p>`);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let child, browser, worker, debuggingPort;
let launchCount = 0;

async function evaluate(expression) {
  const result = await worker.call('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
  assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}

async function launch() {
  requireShutdown(runningProcesses());
  await fs.rm(path.join(profile, 'DevToolsActivePort'), {force: true});
  // 복원 강제 플래그나 시작 URL 없이 제품의 실제 기본값으로 시작한다.
  child = spawn(executable, [`--user-data-dir=${profile}`, '--remote-debugging-port=0',
    '--no-first-run', '--no-default-browser-check', '--disable-sync',
    '--disable-background-networking', '--hide-crash-restore-bubble',
    `--load-extension=${extension}`, `--disable-extensions-except=${extension}`],
    {windowsHide: true, stdio: ['ignore', 'ignore', 'pipe']});
  const log = path.join(output, `launch-${++launchCount}.log`);
  child.stderr.on('data', data => fs.appendFile(log, data).catch(() => {}));
  child.on('error', error => fs.appendFile(log, String(error)).catch(() => {}));
  let endpoint;
  for (let attempt = 0; attempt < 200; ++attempt) {
    assert.equal(child.exitCode, null, 'Yee 시작 실패');
    try {endpoint = (await fs.readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).trim().split(/\r?\n/); break;} catch {}
    await pause(100);
  }
  assert.ok(endpoint, '검사 연결 준비 시간 초과');
  debuggingPort = endpoint[0];
  browser = await connectCDP(`ws://127.0.0.1:${endpoint[0]}${endpoint[1]}`);
  for (let attempt = 0; attempt < 200; ++attempt) {
    const targets = await (await fetch(`http://127.0.0.1:${endpoint[0]}/json/list`)).json();
    const target = targets.find(item => item.type === 'service_worker' &&
      item.url.startsWith('chrome-extension://') && item.url.endsWith('/worker.js'));
    if (target) {
      worker = await connectCDP(target.webSocketDebuggerUrl);
      // target 공개는 worker 초기화보다 빠르므로 API 호출 전 본문 실행을 확인한다.
      for (let readyAttempt = 0; readyAttempt < 100; ++readyAttempt) {
        const ready = await worker.call('Runtime.evaluate', {
          expression: 'globalThis.yeeGroupSessionReady === true', returnByValue: true
        });
        if (ready.result?.value === true) return;
        await pause(100);
      }
      throw new Error('그룹 검사 확장 프로그램 초기화 시간 초과');
    }
    await pause(100);
  }
  throw new Error('그룹 검사 확장 프로그램 시작 시간 초과');
}

async function startupSetting(value) {
  // 보호된 설정 파일을 직접 고치지 않고 실제 설정 화면의 native API를 사용한다.
  const {targetId} = await browser.call('Target.createTarget', {url: 'chrome://settings/onStartup'});
  let settings;
  try {
    for (let attempt = 0; attempt < 100; ++attempt) {
      const targets = await (await fetch(`http://127.0.0.1:${debuggingPort}/json/list`)).json();
      const target = targets.find(item => item.id === targetId);
      if (target) {settings = await connectCDP(target.webSocketDebuggerUrl); break;}
      await pause(100);
    }
    assert.ok(settings, '시작 설정 검사 연결');
    let ready = false;
    for (let attempt = 0; attempt < 100; ++attempt) {
      try {
        ready = (await settings.call('Runtime.evaluate', {
          expression: 'document.readyState === "complete" && typeof globalThis.chrome?.settingsPrivate?.getPref === "function"',
          returnByValue: true
        })).result.value === true;
      } catch (error) {
        if (!/context/i.test(error.message)) throw error;
      }
      if (ready) break;
      await pause(100);
    }
    assert.ok(ready, 'native 시작 설정 준비');
    const result = await settings.call('Runtime.evaluate', {
      expression: `new Promise((resolve, reject) => {
        const read = () => chrome.settingsPrivate.getPref('session.restore_on_startup', pref => resolve(pref.value));
        ${value === undefined ? 'read();' : `chrome.settingsPrivate.setPref('session.restore_on_startup', ${JSON.stringify(value)}, '',
          success => success ? read() : reject(new Error('시작 설정 변경 실패')));`}
      })`, awaitPromise: true, returnByValue: true
    });
    assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  } finally {
    settings?.close();
    await browser.call('Target.closeTarget', {targetId});
  }
}

async function shutdown() {
  worker?.close(); worker = undefined;
  if (child) {
    await closeOwnedBrowser(child, browser, pid => {
      execFileSync('powershell.exe', ['-NoProfile', '-Command',
        `$taskBrowser = Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if ($taskBrowser) { [void]$taskBrowser.CloseMainWindow() }`],
      {windowsHide: true});
    });
    await waitForShutdown(runningProcesses);
  }
  child = undefined; browser = undefined;
}

// 재실행마다 바뀔 수 있는 그룹 ID 대신 이름·색상·접힘과 실제 탭 순서를 비교한다.
const stateExpression = `(async () => {
  const tabs = (await chrome.tabs.query({})).sort((a, b) => a.index - b.index);
  const groups = await chrome.tabGroups.query({});
  return {
    groups: groups.map(group => ({title: group.title, color: group.color, collapsed: group.collapsed,
      tabs: tabs.filter(tab => tab.groupId === group.id).map(tab => tab.url || tab.pendingUrl)}))
      .sort((a, b) => a.title.localeCompare(b.title)),
    tabs: tabs.map(tab => ({url: tab.url || tab.pendingUrl,
      group: groups.find(group => group.id === tab.groupId)?.title ?? null}))
  };
})()`;

try {
  await launch();
  await evaluate(`(async () => {
    const initialTabs = await chrome.tabs.query({});
    for (const [title, color, pages] of [['업무', 'blue', ['a', 'b']], ['참고', 'green', ['c', 'd']]]) {
      const ids = [];
      for (const page of pages) ids.push((await chrome.tabs.create({url: ${JSON.stringify(origin)} + '/' + page, active: false})).id);
      const id = await chrome.tabs.group({tabIds: ids});
      await chrome.tabGroups.update(id, {title, color});
    }
    await chrome.tabs.create({url: ${JSON.stringify(origin + '/ungrouped')}, active: true});
    await chrome.tabs.remove(initialTabs.map(tab => tab.id));
    const collapsed = (await chrome.tabGroups.query({title: '참고'}))[0];
    await chrome.tabGroups.update(collapsed.id, {collapsed: true});
  })()`);
  for (let attempt = 0; attempt < 100; ++attempt) {
    if (await evaluate('(async () => (await chrome.tabs.query({})).every(tab => tab.status === "complete"))()')) break;
    assert.ok(attempt < 99, '검사 탭 로드 시간 초과');
    await pause(100);
  }
  const before = await evaluate(stateExpression);
  assert.equal(before.groups.length, 2, '검사 그룹 생성');
  assert.equal(before.tabs.length, 5, '검사 탭 생성');
  assert.equal(before.groups.find(group => group.title === '참고').collapsed, true);
  await fs.writeFile(path.join(output, 'before.json'), JSON.stringify(before, null, 2));
  await shutdown();
  await launch();
  let restored;
  for (let attempt = 0; attempt < 100; ++attempt) {
    restored = await evaluate(stateExpression);
    if (JSON.stringify(restored) === JSON.stringify(before)) break;
    await pause(100);
  }
  await fs.writeFile(path.join(output, 'restored.json'), JSON.stringify(restored, null, 2));
  assert.deepEqual(restored, before, '정상 종료 후 그룹 이름·색상·접힘·탭 구성과 순서 복원');
  assert.equal(await startupSetting(5), 5, '실제 설정 화면에서 새 탭 시작 선택');
  await shutdown();

  // 사용자가 명시한 시작 설정을 제품 기본값이 덮어쓰지 않아야 한다.
  await launch();
  await pause(1000);
  const explicitNewTab = await evaluate(stateExpression);
  await fs.writeFile(path.join(output, 'explicit-new-tab.json'), JSON.stringify(explicitNewTab, null, 2));
  assert.equal(explicitNewTab.groups.length, 0, '명시적 새 탭 시작 설정 유지');
  assert.ok(explicitNewTab.tabs.length > 0 && explicitNewTab.tabs.every(tab => !tab.url.startsWith(origin)),
    '새 탭 시작을 선택한 프로필에 이전 검사 탭을 강제 복원하지 않음');
  assert.equal(await startupSetting(), 5, '재실행 뒤 사용자 시작 설정 보존');
  await shutdown();
  console.log(JSON.stringify({passed: true, output, groups: before.groups, explicitNewTab}, null, 2));
} finally {
  try {await shutdown();} finally {await new Promise(resolve => server.close(resolve));}
}
