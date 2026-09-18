// Lifecycle helpers for the native fixture; dependencies are injectable in tests.
import assert from 'node:assert/strict';
export const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
export async function connectCDP(url, Socket = globalThis.WebSocket, timeout = 15000) {
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
  let next = 0;
  let closed = false;
  const pending = new Map();
  const disconnected = () => {
    closed = true;
    for (const request of pending.values()) {
      clearTimeout(request.timer); request.reject(new Error('CDP disconnected'));
    }
    pending.clear();
  };
  socket.addEventListener('close', disconnected);
  socket.addEventListener('error', disconnected);
  socket.addEventListener('message', event => {
    let message;
    try {message = JSON.parse(event.data);} catch {disconnected(); socket.close(); return;}
    if (!message || typeof message !== 'object') {disconnected(); socket.close(); return;}
    const request = pending.get(message.id);
    if (!request) return;
    pending.delete(message.id); clearTimeout(request.timer);
    message.error ? request.reject(new Error(JSON.stringify(message.error))) : request.resolve(message.result);
  });
  return {
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
export async function closeOwnedBrowser(browser, cdp, gracefulQuit, wait = pause) {
  if (exited(browser)) return;
  try {await cdp?.call('Browser.close');} catch {}
  cdp?.close();
  for (let attempt = 0; !exited(browser) && attempt < 10; ++attempt) await wait(100);
  if (!exited(browser)) {
    try {await gracefulQuit(browser.pid);} catch (error) {if (!exited(browser)) throw error;}
  }
  for (let attempt = 0; !exited(browser) && attempt < 100; ++attempt) await wait(100);
  assert.ok(exited(browser), `Test-owned browser did not exit gracefully; PID ${browser.pid}`);
}
