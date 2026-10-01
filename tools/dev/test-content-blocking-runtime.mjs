#!/usr/bin/env node
import assert from 'node:assert/strict';
import {closeOwnedBrowser, connectCDP, requireShutdown} from './content_blocking_test_runtime.mjs';
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
assert.throws(() => requireShutdown([{pid: 42, executable: '/tmp/Renamed.app/Contents/MacOS/Renamed'}]), /Gracefully/);
requireShutdown([]);
class FakeSocket extends EventTarget {
  static OPEN = 1;
  static mode = 'open';
  constructor() {
    super(); FakeSocket.last = this; this.readyState = 0;
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
