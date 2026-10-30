#!/usr/bin/env node
import assert from 'node:assert/strict';
import {closeOwnedBrowser, connectCDP, requireCdpPeerSocket, requireShutdown,
  requireWindowsBrowserOwner, requireWindowsCdpOwner,
  waitForShutdown} from './content_blocking_test_runtime.mjs';
const active = () => ({pid: 123, exitCode: null, signalCode: null});
{
  const browser = active(), calls = [];
  await closeOwnedBrowser(browser, undefined, async pid => {calls.push(pid); browser.exitCode = 0;}, async () => {});
  assert.deepEqual(calls, [123], 'Startup/CDP failure still requests an owned PID shutdown');
}
{
  const browser = active(); let fallback = 0;
  await closeOwnedBrowser(browser, {async call() {browser.exitCode = 0;}, close() {}},
    async () => {++fallback;}, async () => {});
  assert.equal(fallback, 0, 'Successful CDP shutdown needs no fallback');
}
{
  const browser = active(); let pid;
  await closeOwnedBrowser(browser, {async call() {throw Error('disconnected');}, close() {}},
    async value => {pid = value; browser.exitCode = 0;}, async () => {});
  assert.equal(pid, 123, 'CDP errors use the same owned PID');
}
await closeOwnedBrowser({pid: undefined}, undefined, () => {throw Error('No process was spawned');});
await assert.rejects(closeOwnedBrowser(active(), undefined, async () => {}, async () => {}), /did not exit gracefully/);
{
  const browser = active(); let closeCommands = 0, disconnects = 0;
  const ownedPids = [];
  await assert.rejects(closeOwnedBrowser(browser, {
    verifyOwner() {throw Error('endpoint ownership changed');},
    async call() {++closeCommands;}, close() {++disconnects;}
  }, async pid => {ownedPids.push(pid); browser.exitCode = 0;}, async () => {}), /ownership changed/);
  assert.equal(closeCommands, 0, '소유권이 달라진 연결에는 Browser.close를 보내지 않는다');
  assert.deepEqual(ownedPids, [123], '검수가 실행한 PID의 정상 종료만 요청한다');
  assert.equal(disconnects, 1, '잘못된 CDP 연결은 로컬에서 해제한다');
}
{
  const executable = String.raw`F:\Yee\chrome.exe`, profile = String.raw`C:\Temp\yee-(owned)`;
  let processes = [{ProcessId: 123, ExecutablePath: executable,
    CommandLine: `"${executable}" --user-data-dir="${profile}" --remote-debugging-port=9222`}];
  let listeners = [{OwningProcess: 123, LocalAddress: '127.0.0.1', LocalPort: 9222}];
  const run = (command, args, options) => {
    assert.equal(command, 'powershell.exe');
    if (args.at(-1).includes('Get-CimInstance')) {
      assert.equal(options.env.YEE_OWNER_PID, '123');
      return JSON.stringify(processes);
    }
    assert.ok(args.at(-1).includes('Get-NetTCPConnection'));
    assert.equal(options.env.YEE_OWNER_PORT, '9222');
    return JSON.stringify(listeners);
  };
  assert.equal(requireWindowsBrowserOwner(executable, profile, 123, run).ProcessId, 123);
  assert.equal(requireWindowsCdpOwner(executable, profile, 123, 9222, run).pid, 123);
  processes[0].ExecutablePath = String.raw`C:\Installed\chrome.exe`;
  assert.throws(() => requireWindowsCdpOwner(executable, profile, 123, 9222, run));
  processes[0].ExecutablePath = executable;
  processes[0].CommandLine = `"${executable}" --user-data-dir="${profile}-other"`;
  assert.throws(() => requireWindowsCdpOwner(executable, profile, 123, 9222, run));
  processes[0].CommandLine = `"${executable}" --user-data-dir="${profile}"`;
  listeners[0].OwningProcess = 456;
  assert.throws(() => requireWindowsCdpOwner(executable, profile, 123, 9222, run));
  listeners = [];
  assert.throws(() => requireWindowsCdpOwner(executable, profile, 123, 9222, run));
  listeners = [{OwningProcess: 123, LocalAddress: '127.0.0.1', LocalPort: 9223}];
  assert.throws(() => requireWindowsCdpOwner(executable, profile, 123, 9222, run));
  assert.throws(() => requireWindowsCdpOwner(executable, profile, 123, 0, run));
  assert.throws(() => requireWindowsBrowserOwner(executable, profile, 0, run));
}
{
  const valid = 'ws://127.0.0.1:9222/devtools/browser/abc-123';
  assert.equal(requireCdpPeerSocket(valid, 9222, 'browser'), valid);
  assert.equal(requireCdpPeerSocket('ws://127.0.0.1:9222/devtools/page/def', 9222, 'page', 'def'),
    'ws://127.0.0.1:9222/devtools/page/def');
  for (const invalid of [valid.replace('ws:', 'wss:'), valid.replace('127.0.0.1', 'example.test'),
    valid.replace('9222', '9223'), valid + '?token=other', valid + '#other',
    valid.replace('127.0.0.1', 'user@127.0.0.1'), valid.replace('/browser/', '/page/')]) {
    assert.throws(() => requireCdpPeerSocket(invalid, 9222, 'browser'));
  }
  assert.throws(() => requireCdpPeerSocket(valid, 9222, 'page', 'other'));
}
assert.throws(() => requireShutdown([{pid: 42, executable: '/tmp/Renamed.app/Contents/MacOS/Renamed'}]), /Gracefully/);
requireShutdown([]);
{
  // 본체 종료 후 renderer만 남은 구간도 지나야 다음 실행을 허용한다.
  const inventories = [[{pid: 1}], [{pid: 2}], []];
  let waits = 0;
  await waitForShutdown(() => inventories.shift(), async () => { ++waits; });
  assert.equal(waits, 2);
  assert.equal(inventories.length, 0);
}
await assert.rejects(waitForShutdown(() => [{pid: 42}], async () => {}), /Gracefully/);
await assert.rejects(waitForShutdown(() => { throw Error('inventory unavailable'); }), /inventory unavailable/);
class FakeSocket extends EventTarget {
  static OPEN = 1;
  static mode = 'open';
  constructor(url) {
    super(); FakeSocket.last = this; this.readyState = 0; this.url = url;
    queueMicrotask(() => {
      if (FakeSocket.mode === 'open') {this.readyState = 1; this.dispatchEvent(new Event('open'));}
      else if (FakeSocket.mode === 'closed') this.close();
    });
  }
  send() {}
  close() {this.readyState = 3; this.dispatchEvent(new Event('close'));}
}
FakeSocket.mode = 'closed';
await assert.rejects(connectCDP('fixture', FakeSocket, 100), /connection failed/);
FakeSocket.mode = 'hang';
await assert.rejects(connectCDP('fixture', FakeSocket, 1), /connection timeout/);
assert.equal(FakeSocket.last.readyState, 3, 'Timed-out connection is closed');
FakeSocket.mode = 'open';
{
  const previous = FakeSocket.last;
  await assert.rejects(connectCDP('fixture', FakeSocket, 100,
    () => {throw Error('owner rejected before connect');}), /owner rejected/);
  assert.equal(FakeSocket.last, previous, '소유권 검사 실패 시 연결을 만들지 않는다');
  let checks = 0;
  await assert.rejects(connectCDP('fixture', FakeSocket, 100,
    () => {if (++checks === 2) throw Error('owner changed after connect');}), /owner changed/);
  assert.equal(FakeSocket.last.readyState, 3, '연결 직후 소유권이 달라져도 소켓을 닫는다');
  class RedirectedSocket extends FakeSocket {
    constructor(url) {super(url); this.url = url.replace('9222', '9223');}
  }
  await assert.rejects(connectCDP('ws://127.0.0.1:9222/devtools/browser/owned',
    RedirectedSocket, 100, peer => requireCdpPeerSocket(peer, 9222, 'browser')));
  assert.equal(FakeSocket.last.readyState, 3, '연결 후 실제 피어 주소가 달라지면 닫는다');
}
const connection = await connectCDP('fixture', FakeSocket, 100);
const request = connection.call('Page.fixture');
FakeSocket.last.close();
await assert.rejects(request, /disconnected/, 'Disconnect rejects outstanding requests immediately');
await assert.rejects(connection.call('Page.fixture'), /disconnected/, 'Calls after disconnect fail without a timer');
const connected = await connectCDP('fixture', FakeSocket, 100);
const response = connected.call('Page.fixture');
FakeSocket.last.dispatchEvent(new MessageEvent('message', {data: JSON.stringify({id: 1, result: {value: 42}})}));
assert.deepEqual(await response, {value: 42});
const events = [];
const unsubscribe = connected.on('Tracing.tracingComplete', params => events.push(params));
FakeSocket.last.dispatchEvent(new MessageEvent('message', {
  data: JSON.stringify({method: 'Tracing.tracingComplete', params: {stream: 'trace-stream'}})}));
assert.deepEqual(events, [{stream: 'trace-stream'}], 'Unsolicited trace completion is delivered');
unsubscribe();
FakeSocket.last.dispatchEvent(new MessageEvent('message', {
  data: JSON.stringify({method: 'Tracing.tracingComplete', params: {stream: 'unused'}})}));
assert.equal(events.length, 1, 'Removed trace listeners do not receive events');
connected.close();
const malformed = await connectCDP('fixture', FakeSocket, 100);
const malformedRequest = malformed.call('Page.fixture');
FakeSocket.last.dispatchEvent(new MessageEvent('message', {data: 'null'}));
await assert.rejects(malformedRequest, /disconnected/);
assert.equal(FakeSocket.last.readyState, 3, 'Invalid protocol input closes the connection');
console.log('Content blocking runtime passed: owned shutdown/startup failures, rename guard, CDP startup timeout/early close, pending request disconnect and response delivery.');
