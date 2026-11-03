// Lifecycle helpers for the native fixture; dependencies are injectable in tests.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {realpathSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {filterValidationProcesses} from './lib/macos_installer_processes.mjs';

// Windows의 검수용 본체를 실행 파일과 전용 프로필까지 함께 확인한다.
export function requireWindowsBrowserOwner(executable, profile, pid, run = execFileSync) {
  assert.ok(typeof executable === 'string' && path.win32.isAbsolute(executable));
  assert.ok(typeof profile === 'string' && path.win32.isAbsolute(profile));
  assert.ok(Number.isInteger(pid) && pid > 0, '검수용 본체 PID가 필요하다');
  const script = "$ErrorActionPreference='Stop'; @(Get-CimInstance Win32_Process -Filter ('ProcessId = ' + [int]$env:YEE_OWNER_PID) -Property ProcessId,ExecutablePath,CommandLine | Select-Object ProcessId,ExecutablePath,CommandLine) | ConvertTo-Json -Compress";
  const raw = run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script],
    {encoding: 'utf8', windowsHide: true, env: {...process.env, YEE_OWNER_PID: String(pid)}});
  const value = JSON.parse(raw.trim() || '[]');
  const processes = Array.isArray(value) ? value : [value];
  const normalize = value => path.win32.resolve(value).toLowerCase();
  const escaped = path.win32.resolve(profile).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const argument = new RegExp(`(?:^|\\s)"?--user-data-dir(?:=|\\s+)"?${escaped}"?(?:\\s|$)`, 'i');
  const owners = processes.filter(item => item?.ProcessId === pid && item.ExecutablePath &&
    normalize(item.ExecutablePath) === normalize(executable) && argument.test(item.CommandLine ?? ''));
  assert.equal(owners.length, 1, '같은 실행 파일·전용 프로필의 본체만 허용한다');
  return owners[0];
}

// 포트 번호만 같다는 이유로 다른 브라우저의 CDP에 연결하지 않는다.
export function requireWindowsCdpOwner(executable, profile, pid, port, run = execFileSync) {
  assert.ok(Number.isInteger(port) && port > 0 && port <= 65535, '유효한 CDP 포트가 필요하다');
  requireWindowsBrowserOwner(executable, profile, pid, run);
  const script = "$ErrorActionPreference='Stop'; @(Get-NetTCPConnection -LocalPort ([int]$env:YEE_OWNER_PORT) -State Listen -ErrorAction Stop | Select-Object OwningProcess,LocalAddress,LocalPort) | ConvertTo-Json -Compress";
  const raw = run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script],
    {encoding: 'utf8', windowsHide: true, env: {...process.env, YEE_OWNER_PORT: String(port)}});
  const value = JSON.parse(raw.trim() || '[]');
  const listeners = (Array.isArray(value) ? value : [value])
    .filter(item => item?.LocalAddress === '127.0.0.1' && item.LocalPort === port);
  assert.ok(listeners.length > 0 && listeners.every(item => item.OwningProcess === pid),
    'CDP 리스너가 검수용 본체 PID에 속해야 한다');
  return {pid, executableMatched: true, profileMatched: true, listeners};
}

export function requireCdpPeerSocket(value, port, type, id) {
  assert.ok(Number.isInteger(port) && port > 0 && port <= 65535);
  assert.ok(['browser', 'page'].includes(type));
  const url = new URL(value);
  assert.equal(url.protocol, 'ws:');
  assert.equal(url.hostname, '127.0.0.1');
  assert.equal(Number(url.port), port);
  assert.equal(url.username + url.password + url.search + url.hash, '');
  assert.match(url.pathname, new RegExp(`^/devtools/${type}/[A-Za-z0-9-]+$`));
  if (id !== undefined) assert.equal(url.pathname, `/devtools/${type}/${id}`);
  return url.href;
}

export function uiTestWindowArgs() {
  // macOS 밖에서는 셸이나 모니터 도구를 요구하지 않는다.
  if (process.platform !== 'darwin') return [];
  const script = fileURLToPath(new URL('./ui-test-window-args.sh', import.meta.url));
  return execFileSync('/bin/zsh', [script], {
    encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit']
  }).split(/\r?\n/).filter(Boolean);
}

export const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
export async function connectCDP(url, Socket = globalThis.WebSocket, timeout = 15000, verifyOwner) {
  await verifyOwner?.(url);
  const socket = new Socket(url);
  await new Promise((resolve, reject) => {
    const finish = error => {
      clearTimeout(timer);
      socket.removeEventListener('open', opened);
      socket.removeEventListener('error', failed);
      socket.removeEventListener('close', failed);
      if (error) {socket.close(); reject(error);} else resolve();
    };
    const opened = () => finish();
    const failed = () => finish(new Error('CDP connection failed'));
    const timer = setTimeout(() => finish(new Error('CDP connection timeout')), timeout);
    socket.addEventListener('open', opened);
    socket.addEventListener('error', failed);
    socket.addEventListener('close', failed);
  });
  try {await verifyOwner?.(socket.url ?? url);} catch (error) {socket.close(); throw error;}
  let next = 0;
  let closed = false;
  const pending = new Map();
  const listeners = new Map();
  const disconnected = () => {
    closed = true;
    for (const request of pending.values()) {
      clearTimeout(request.timer); request.reject(new Error('CDP disconnected'));
    }
    pending.clear();
    listeners.clear();
  };
  socket.addEventListener('close', disconnected);
  socket.addEventListener('error', disconnected);
  socket.addEventListener('message', event => {
    let message;
    try {message = JSON.parse(event.data);} catch {disconnected(); socket.close(); return;}
    if (!message || typeof message !== 'object') {disconnected(); socket.close(); return;}
    if (typeof message.method === 'string') {
      for (const listener of listeners.get(message.method) ?? []) listener(message.params ?? {});
      return;
    }
    const request = pending.get(message.id);
    if (!request) return;
    pending.delete(message.id); clearTimeout(request.timer);
    message.error ? request.reject(new Error(JSON.stringify(message.error))) : request.resolve(message.result);
  });
  return {
    verifyOwner() {return verifyOwner?.(socket.url ?? url);},
    on(method, listener) {
      if (!listeners.has(method)) listeners.set(method, new Set());
      const subscribers = listeners.get(method);
      subscribers.add(listener);
      return () => {subscribers.delete(listener); if (!subscribers.size) listeners.delete(method);};
    },
    call(method, params = {}) {
      if (closed || socket.readyState !== Socket.OPEN) return Promise.reject(new Error('CDP disconnected'));
      const id = ++next;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {pending.delete(id); reject(new Error(`CDP timeout: ${method}`));}, timeout);
        pending.set(id, {resolve, reject, timer});
        try {socket.send(JSON.stringify({id, method, params}));}
        catch (error) {clearTimeout(timer); pending.delete(id); reject(error);}
      });
    },
    close() {socket.close();}
  };
}
export function exited(browser) {
  return !browser.pid || browser.exitCode !== null || browser.signalCode !== null;
}
export function requireShutdown(processes) {
  assert.equal(processes.length, 0,
    `Gracefully shut down product browsers before validation: ${JSON.stringify(processes)}`);
}
// 설치판은 번들 ID 조회에서 제외하고, 선택한 개발 앱·검수 프로필의 경계만 적용한다.
export function inspectDevelopmentProcesses(executable, profile, run = execFileSync) {
  const script = fileURLToPath(new URL('./browser_bundle_executables.py', import.meta.url));
  const processes = JSON.parse(run('python3', [script, '--running', '--details'], {encoding: 'utf8'}));
  const application = path.resolve(realpathSync(executable), '../../..');
  return filterValidationProcesses(processes, {application, profile});
}
// 정상 종료를 요청한 뒤 본체보다 늦게 끝나는 보조 프로세스도 기다린다.
export async function waitForShutdown(inspectProcesses, wait = pause) {
  let processes;
  for (let attempt = 0; attempt < 20; ++attempt) {
    processes = await inspectProcesses();
    if (processes.length === 0) return;
    if (attempt < 19) await wait(100);
  }
  requireShutdown(processes);
}
export async function closeOwnedBrowser(browser, cdp, gracefulQuit, wait = pause) {
  if (exited(browser)) return;
  let ownershipFailure;
  try {await cdp?.verifyOwner?.();} catch (error) {ownershipFailure = error;}
  // 소유권이 달라진 연결에는 종료 명령을 보내지 않고, 확인된 PID로만
  // 정상 종료를 요청한 뒤 소유권 오류를 검사 실패로 전파한다.
  if (!ownershipFailure) {try {await cdp?.call('Browser.close');} catch {}}
  cdp?.close();
  for (let attempt = 0; !exited(browser) && attempt < 10; ++attempt) await wait(100);
  if (!exited(browser)) {
    try {await gracefulQuit(browser.pid);} catch (error) {if (!exited(browser)) throw error;}
  }
  for (let attempt = 0; !exited(browser) && attempt < 100; ++attempt) await wait(100);
  assert.ok(exited(browser), `Test-owned browser did not exit gracefully; PID ${browser.pid}`);
  if (ownershipFailure) throw ownershipFailure;
}
