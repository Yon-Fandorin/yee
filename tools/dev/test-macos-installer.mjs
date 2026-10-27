#!/usr/bin/env node
// 실제 DMG의 앱 복사·재설치와 격리 프로필의 데이터 보존을 검증한다.
import assert from 'node:assert/strict';
import {execFileSync, spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {createReadStream} from 'node:fs';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {closeOwnedBrowser, connectCDP, pause, requireShutdown, uiTestWindowArgs, waitForShutdown}
  from './content_blocking_test_runtime.mjs';

assert.equal(process.platform, 'darwin', '이 실행 검증은 macOS용이다');
assert.equal(process.argv.length, 3, 'Usage: node tools/dev/test-macos-installer.mjs PACKAGE_DIRECTORY');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const packageDirectory = path.resolve(process.argv[2]);
const metadata = JSON.parse(await fs.readFile(path.join(packageDirectory, 'release.json'), 'utf8'));
const identity = JSON.parse(await fs.readFile(path.join(root, 'branding/identity.json'), 'utf8'));
const brand = JSON.parse(await fs.readFile(path.join(root, 'branding/brand.json'), 'utf8'));
assert.equal(metadata.platform, 'macos');
assert.equal(metadata.application_name, brand.name);
assert.equal(metadata.macos_identity.bundle_id, identity.macos.bundle_id);
assert.equal(metadata.profile_directory, identity.profile_directory);
assert.equal(path.basename(metadata.installer), metadata.installer);
assert.ok(metadata.installer.endsWith('.dmg'));

async function hash(file) {
  const digest = createHash('sha256');
  for await (const chunk of createReadStream(file)) digest.update(chunk);
  return digest.digest('hex');
}
const image = path.join(packageDirectory, metadata.installer);
assert.equal(await hash(image), metadata.sha256, 'DMG SHA-256');
const buildRoot = process.env.YEE_LOCAL_BUILD_ROOT || path.join(root, '.local-build');
await fs.mkdir(buildRoot, {recursive: true});
const output = await fs.mkdtemp(path.join(buildRoot, 'macos-install-test-'));
const mount = path.join(output, 'volume');
const application = path.join(output, 'Applications', brand.name + '.app');
const executable = path.join(application, 'Contents/MacOS', brand.name);
const profile = path.join(output, 'profile');
const extension = path.join(output, 'extension');
const escapedName = brand.name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const productProcess = new RegExp(`/${escapedName}(?: Helper[^/]*)?\\.app/Contents/`);
function processes(ownedOnly = false) {
  return execFileSync('/bin/ps', ['-axo', 'pid=,command='], {encoding: 'utf8'})
    .split('\n').filter(line => productProcess.test(line)
      && (!ownedOnly || line.includes(application + '/Contents/')));
}
requireShutdown(processes());
const server = http.createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8');
  response.end(`<!doctype html><meta charset="utf-8"><title>Yee 설치 검사 ${request.url}</title><p>Yee 설치 검사</p>`);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let mounted = false, child, browser, worker, port, launchCount = 0;

async function eventually(read, predicate, description) {
  for (let attempt = 0; attempt < 200; ++attempt) {
    const value = await read();
    if (predicate(value)) return value;
    await pause(100);
  }
  throw new Error(description);
}

async function evaluate(expression) {
  const result = await worker.call('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
  assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}

async function launch() {
  requireShutdown(processes());
  await fs.rm(path.join(profile, 'DevToolsActivePort'), {force: true});
  const windowArgs = uiTestWindowArgs();
  child = spawn('/usr/bin/open', ['-n', '-W', application, '--args',
    `--user-data-dir=${profile}`, '--remote-debugging-port=0', '--no-first-run',
    '--no-default-browser-check', '--disable-background-networking', '--disable-sync',
    '--use-mock-keychain', '--window-size=1100,800', ...windowArgs,
    `--load-extension=${extension}`, `--disable-extensions-except=${extension}`],
    {stdio: ['ignore', 'ignore', 'pipe']});
  const log = path.join(output, `launch-${++launchCount}.log`);
  child.stderr.on('data', data => fs.appendFile(log, data).catch(() => {}));
  child.on('error', error => fs.appendFile(log, String(error)).catch(() => {}));
  const endpoint = await eventually(async () => {
    assert.equal(child.exitCode, null, '설치된 Yee 시작 실패');
    try {return (await fs.readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).trim().split(/\r?\n/);}
    catch {return null;}
  }, value => value?.length === 2, '설치 앱의 DevTools 준비 시간 초과');
  port = endpoint[0];
  browser = await connectCDP(`ws://127.0.0.1:${port}${endpoint[1]}`);
  const target = await eventually(async () => {
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    return targets.find(item => item.type === 'service_worker'
      && item.url.startsWith('chrome-extension://') && item.url.endsWith('/worker.js'));
  }, Boolean, '검사 확장 프로그램 준비 시간 초과');
  worker = await connectCDP(target.webSocketDebuggerUrl);
  await eventually(() => evaluate('globalThis.yeeInstallTestReady === true'), Boolean,
    '검사 확장 프로그램 초기화 시간 초과');
}

async function shutdown() {
  worker?.close(); worker = undefined;
  if (child) {
    // open -W는 브라우저 종료보다 늦게 반환할 수 있어 기존 추가 대기를 유지한다.
    await closeOwnedBrowser(child, browser, () => {});
    await waitForShutdown(() => processes(true));
  }
  child = undefined; browser = undefined;
}

async function webStorage(expression) {
  await eventually(() => evaluate('(async () => (await chrome.tabs.query({})).every(tab => tab.status === "complete"))()'),
    Boolean, '복원된 탭 로드 시간 초과');
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = targets.find(item => item.type === 'page' && item.url === origin + '/standalone');
  assert.ok(target, '독립 탭의 웹 저장 데이터 검사 연결');
  const page = await connectCDP(target.webSocketDebuggerUrl);
  try {
    const result = await page.call('Runtime.evaluate', {expression, returnByValue: true});
    assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  } finally {page.close();}
}

const stateExpression = `(async () => {
  const tabs = (await chrome.tabs.query({})).sort((a, b) => a.index - b.index);
  const groups = await chrome.tabGroups.query({});
  const bookmarks = await chrome.bookmarks.search({title: 'Yee 설치 검사'});
  return {
    groups: groups.map(group => ({title: group.title, color: group.color, collapsed: group.collapsed,
      tabs: tabs.filter(tab => tab.groupId === group.id).map(tab => tab.url || tab.pendingUrl)}))
      .sort((a, b) => a.title.localeCompare(b.title)),
    tabs: tabs.map(tab => ({url: tab.url || tab.pendingUrl,
      group: groups.find(group => group.id === tab.groupId)?.title ?? null})),
    bookmarks: bookmarks.map(bookmark => ({title: bookmark.title, url: bookmark.url}))
  };
})()`;

try {
  await fs.mkdir(mount);
  execFileSync('/usr/bin/hdiutil', ['attach', '-readonly', '-nobrowse', '-noautoopen',
    '-mountpoint', mount, image], {stdio: 'inherit'});
  mounted = true;
  assert.equal(await fs.readlink(path.join(mount, 'Applications')), '/Applications');
  const sourceApp = path.join(mount, brand.name + '.app');
  const info = JSON.parse(execFileSync('/usr/bin/plutil', ['-convert', 'json', '-o', '-',
    path.join(sourceApp, 'Contents/Info.plist')], {encoding: 'utf8'}));
  assert.equal(info.CFBundleIdentifier, identity.macos.bundle_id);
  assert.equal(info.CrProductDirName, identity.profile_directory);
  assert.equal(info.CFBundleShortVersionString, metadata.version);
  execFileSync('/usr/bin/codesign', ['--verify', '--deep', '--strict', sourceApp], {stdio: 'inherit'});
  const resources = path.join(sourceApp, 'Contents/Frameworks', brand.name + ' Framework.framework',
    'Versions', metadata.version, 'Resources');
  for (const [filename, expected] of Object.entries(metadata.payload_sha256)) {
    assert.equal(path.basename(filename), filename);
    assert.equal(await hash(path.join(resources, filename)), expected, filename);
  }
  await fs.mkdir(path.dirname(application));
  execFileSync('/usr/bin/ditto', [sourceApp, application]);
  execFileSync('/usr/bin/codesign', ['--verify', '--deep', '--strict', application], {stdio: 'inherit'});
  assert.equal(execFileSync(executable, ['--version'], {encoding: 'utf8'}).trim(),
    `${brand.name} ${metadata.version}`);
  await fs.mkdir(extension);
  await fs.writeFile(path.join(extension, 'manifest.json'), JSON.stringify({
    manifest_version: 3, name: 'Yee 설치 검사', version: '1.0',
    permissions: ['tabs', 'tabGroups', 'bookmarks'], background: {service_worker: 'worker.js'}
  }));
  await fs.writeFile(path.join(extension, 'worker.js'),
    'chrome.runtime.onStartup.addListener(() => {});\nglobalThis.yeeInstallTestReady = true;\n');
  await launch();
  await evaluate(`(async () => {
    const initial = await chrome.tabs.query({});
    for (const [title, color, pages] of [['업무', 'blue', ['a', 'b']], ['참고', 'green', ['c', 'd']]]) {
      const ids = [];
      for (const page of pages) ids.push((await chrome.tabs.create({url: ${JSON.stringify(origin)} + '/' + page, active: false})).id);
      const groupId = await chrome.tabs.group({tabIds: ids});
      await chrome.tabGroups.update(groupId, {title, color});
    }
    await chrome.tabs.create({url: ${JSON.stringify(origin + '/standalone')}, active: true});
    await chrome.tabs.remove(initial.map(tab => tab.id));
    const collapsed = (await chrome.tabGroups.query({title: '참고'}))[0];
    await chrome.tabGroups.update(collapsed.id, {collapsed: true});
    await chrome.bookmarks.create({title: 'Yee 설치 검사', url: ${JSON.stringify(origin + '/bookmark')}});
  })()`);
  await eventually(() => evaluate('(async () => (await chrome.tabs.query({})).every(tab => tab.status === "complete"))()'),
    Boolean, '검사 탭 로드 시간 초과');
  assert.equal(await webStorage('localStorage.setItem("yee-install", "preserved"); localStorage.getItem("yee-install")'), 'preserved');
  const before = await evaluate(stateExpression);
  assert.equal(before.groups.length, 2);
  assert.equal(before.tabs.length, 5);
  assert.equal(before.bookmarks.length, 1);
  assert.equal(before.groups.find(group => group.title === '참고').collapsed, true);
  await fs.writeFile(path.join(output, 'before.json'), JSON.stringify(before, null, 2));
  await shutdown();

  // Finder 대치와 같이 앱만 교체하며 프로필은 건드리지 않는다.
  const previousApp = path.join(output, 'previous-Yee.app');
  await fs.rename(application, previousApp);
  execFileSync('/usr/bin/ditto', [sourceApp, application]);
  execFileSync('/usr/bin/codesign', ['--verify', '--deep', '--strict', application], {stdio: 'inherit'});
  await fs.rm(previousApp, {recursive: true});
  await launch();
  const restored = await eventually(() => evaluate(stateExpression), value => JSON.stringify(value) === JSON.stringify(before),
    '재설치 후 그룹·북마크 복원 시간 초과');
  assert.deepEqual(restored, before, '같은 버전 재설치 후 그룹·탭 순서·북마크 보존');
  assert.equal(await webStorage('localStorage.getItem("yee-install")'), 'preserved', '웹 저장 데이터 보존');
  await shutdown();

  await fs.rm(application, {recursive: true});
  assert.ok((await fs.stat(path.join(profile, 'Default/Preferences'))).isFile(), '앱 제거 후 프로필 보존');
  execFileSync('/usr/bin/ditto', [sourceApp, application]);
  await launch();
  await eventually(() => evaluate(stateExpression), value => JSON.stringify(value) === JSON.stringify(before),
    '앱 제거·재설치 후 그룹·북마크 복원 시간 초과');
  assert.equal(await webStorage('localStorage.getItem("yee-install")'), 'preserved');
  await shutdown();
  const result = {passed: true, version: metadata.version, output, restored,
    checks: ['DMG 해시와 앱 서명', '독립 번들·프로필 메타데이터',
      '설치 앱과 렌더러 실행', '같은 버전 대치',
      '그룹·북마크·웹 저장 데이터', '앱 제거·재설치 후 프로필 보존'],
    limits: ['Developer ID·공증 검증 제외', '키체인 선택은 별도의 native 단위 검사로 검증']};
  await fs.writeFile(path.join(output, 'result.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result, null, 2));
} finally {
  try {await shutdown();} finally {
    if (mounted) execFileSync('/usr/bin/hdiutil', ['detach', mount], {stdio: 'inherit'});
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
  }
}
