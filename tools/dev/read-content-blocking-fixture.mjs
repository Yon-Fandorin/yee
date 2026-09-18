// Read only the task's temporary Chrome fixture, never an existing browser.
import fs from 'node:fs/promises';
import {setTimeout as delay} from 'node:timers/promises';
const [portFile, fixtureURL] = process.argv.slice(2);
const deadline = Date.now() + 30000;
let target;
while (Date.now() < deadline) {
  try {
    const port = (await fs.readFile(portFile, 'utf8')).split('\n')[0];
    const pages = await (await fetch(`http://127.0.0.1:${port}/json/list`, {signal: AbortSignal.timeout(2000)})).json();
    target = pages.find(page => page.url === fixtureURL && page.type === 'page');
    if (target) break;
  } catch {}
  await delay(100);
}
if (!target) throw Error('Temporary fixture target not found');
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  const timer = setTimeout(() => reject(Error('Fixture socket connection timed out')), 5000);
  socket.addEventListener('open', () => {clearTimeout(timer); resolve();}, {once: true});
  socket.addEventListener('error', () => {clearTimeout(timer); reject(Error('Fixture socket connection failed'));}, {once: true});
});
let id = 0;
const pending = new Map();
socket.addEventListener('message', event => {
  const result = JSON.parse(event.data);
  if (result.id) {
    const request = pending.get(result.id);
    if (request) {clearTimeout(request.timer); request.resolve(result); pending.delete(result.id);}
  }
});
socket.addEventListener('close', () => {
  for (const request of pending.values()) {clearTimeout(request.timer); request.reject(Error('Fixture socket closed early'));}
  pending.clear();
});
function call(method, params) {
  return new Promise((resolve, reject) => {
    const key = ++id;
    const timer = setTimeout(() => {pending.delete(key); reject(Error('Fixture request timed out'));}, 5000);
    pending.set(key, {resolve, reject, timer}); socket.send(JSON.stringify({id: key, method, params}));
  });
}
let text;
while (Date.now() < deadline) {
  const result = await call('Runtime.evaluate', {expression: "document.getElementById('result')?.textContent", returnByValue: true});
  text = result.result?.result?.value;
  if (typeof text === 'string' && text.startsWith('{')) break;
  await delay(100);
}
socket.close();
if (!text?.startsWith('{')) throw Error('Fixture did not finish: ' + text);
console.log(text);
