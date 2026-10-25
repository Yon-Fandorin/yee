#!/usr/bin/env node
// 새 사용자별 설치에서 기본 프로필·수리·재설치·두 삭제 모드를 실제로 확인한다.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {connectCDP, pause, requireShutdown} from './content_blocking_test_runtime.mjs';

assert.equal(process.platform, 'win32', 'Windows에서만 설치를 검증한다.');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const [packageDirectory] = process.argv.slice(2);
assert.ok(packageDirectory, 'Usage: node tools/dev/test-windows-installer.mjs PACKAGE_DIRECTORY');
const release = JSON.parse(await fs.readFile(path.join(packageDirectory, 'release.json'), 'utf8'));
const identity = JSON.parse(await fs.readFile(path.join(root, 'branding/identity.json'), 'utf8'));
assert.equal(release.schema_version, 1);
assert.equal(release.product_id, 'yee');
assert.equal(release.channel, 'test');
assert.equal(release.platform, 'windows');
assert.ok(['x64','arm64'].includes(release.architecture));
assert.equal(release.signature, 'NotSigned');
assert.match(release.version, /^\d+\.\d+\.\d+\.\d+$/);
assert.deepEqual(Object.keys(release.payload_sha256).sort(),[
  'YeeCommunityFilterManifest.json','YeeCommunityFilterNotices.txt','YeeCommunityFilters.txt',
  'YeeCommunityFilterSources.tar.xz','YeeCommunityResources.json','YeeCompiledFilterManifest.json',
  'YeeCompiledFilters.dat','YeeContentBlockingNotices.txt','YeeContentBlockingSources.tar.xz'
].sort());
assert.equal(release.profile_directory, identity.profile_directory);
assert.equal(release.application_name, identity.windows.application_name);
assert.match(identity.profile_directory, /^[A-Za-z][A-Za-z0-9_-]*$/);
assert.match(identity.windows.application_name, /^[A-Za-z][A-Za-z0-9.]*$/);
assert.match(release.installer, /^Yee-[\d.]+-windows-(x64|arm64)-test-unsigned\.exe$/);
assert.equal(release.installer,`Yee-${release.version}-windows-${release.architecture}-test-unsigned.exe`);
const installer = path.resolve(packageDirectory, release.installer);
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
assert.equal(hash(await fs.readFile(installer)), release.sha256, '실행 전 설치 파일 무결성');
const localAppData = process.env.LOCALAPPDATA;
assert.ok(localAppData && path.isAbsolute(localAppData));
const productRoot = path.resolve(localAppData, identity.profile_directory);
const application = path.join(productRoot, 'Application');
const profile = path.join(productRoot, 'User Data');
const executable = path.join(application, 'chrome.exe');
const uninstallKey = `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${identity.windows.application_name}`;
const output = await fs.mkdtemp(path.join(root, '.local-build/windows-install-test-'));
const token = crypto.randomUUID();
const marker = path.join(profile, 'YeeInstallerTestOwner');
const chromiumRoot = process.env.YEE_CHROMIUM_ROOT || path.join(root,'.local-build/chromium');
const buildOutputs = path.resolve(chromiumRoot,'src/out').toLowerCase()+path.sep;
const reports = [];
let installed = false;
let unrelatedBefore;

async function exists(name) {
  try {await fs.lstat(name); return true;} catch (error) {if (error.code === 'ENOENT') return false; throw error;}
}
function queryRegistry(key, view = '64') {
  const result = spawnSyncRegistry(key, view);
  return result.status === 0 ? result.stdout : null;
}
function spawnSyncRegistry(key, view) {
  // reg query의 없음(1)만 허용하며 접근 거부 등은 진단에 남긴다.
  try {return {status: 0, stdout: execFileSync('reg.exe', ['query', key, '/s', `/reg:${view}`], {encoding:'utf8', windowsHide:true, stdio:['ignore','pipe','pipe']})};}
  catch (error) {
    if (error.status === 1) {
      assert.match(key,/^HK(CU|LM)\\/);
      const keyLiteral = key.slice(5).replaceAll("'","''");
      const registryView = view === '32' ? 'Registry32' : 'Registry64';
      const hive = key.startsWith('HKCU') ? 'CurrentUser' : 'LocalMachine';
      const check = `$ErrorActionPreference='Stop'; $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::${hive},[Microsoft.Win32.RegistryView]::${registryView}); try{$item=$base.OpenSubKey('${keyLiteral}'); if($null -eq $item){exit 2}; $item.Dispose()}finally{$base.Dispose()}`;
      try {execFileSync('powershell.exe',['-NoProfile','-Command',check],{windowsHide:true,stdio:'pipe'});}
      catch (probe) {if(probe.status===2)return {status:1};throw probe;}
    }
    throw error;
  }
}
function unrelatedRegistry() {
  const keys = ['HKCU\\Software\\Chromium', 'HKCU\\Software\\Google\\Chrome',
    'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Chromium',
    'HKCU\\Software\\Chromium Binaries',
    'HKCU\\Software\\Google\\Common\\Rlz',
    'HKCU\\Software\\Classes\\ChromiumExt',
    'HKCU\\Software\\Classes\\.crx',
    'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe',
    'HKCU\\Software\\Classes\\Applications\\chrome.exe',
    ...['.htm','.html','.shtml','.xht','.xhtml','.pdf','.webp','.svg'].map(ext=>`HKCU\\Software\\Classes\\${ext}\\OpenWithList\\chrome.exe`),
    'HKCU\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice',
    'HKCU\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice'];
  return Object.fromEntries(keys.flatMap(key => ['32','64'].map(view => {
    const value = queryRegistry(key, view);
    return [`${view}:${key}`, value === null ? null : hash(value)];
  })));
}
function assertNoYee() {
  const command = '$paths=@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path); $paths | ConvertTo-Json -Compress';
  const raw = execFileSync('powershell.exe',['-NoProfile','-Command',command],{encoding:'utf8',windowsHide:true}).trim();
  const processes = JSON.parse(raw || '[]');
  const values = Array.isArray(processes) ? processes : [processes];
  const normalize = value => path.resolve(value).toLowerCase();
  requireShutdown(values.filter(value => value && (normalize(value) === normalize(executable) ||
    /\\out\\[^\\]+\\chrome\.exe$/i.test(value) && value.toLowerCase().startsWith(buildOutputs))));
}
async function runSetup(file, args, expected, label) {
  assertNoYee();
  const child = spawn(file, args, {windowsHide:true,stdio:'ignore'});
  const code = await new Promise((resolve,reject) => {
    const timer=setTimeout(()=>reject(Error(`${label}: 설치 프로그램 응답 제한 초과`)),180000);
    child.on('error',error=>{clearTimeout(timer);reject(error)});
    child.on('exit',code=>{clearTimeout(timer);resolve(code)});
  });
  reports.push({label, exitCode:code});
  await fs.writeFile(path.join(output,'progress.json'),JSON.stringify(reports,null,2));
  assert.ok(expected.includes(code), `${label}: native installer status ${code}`);
}
async function verifyPayload() {
  for (const [name, expected] of Object.entries(release.payload_sha256)) {
    assert.match(name, /^Yee[A-Za-z]+\.(json|txt|dat|tar\.xz)$/);
    assert.equal(hash(await fs.readFile(path.join(application, release.version, name))), expected, `설치된 ${name}`);
  }
  const registration = queryRegistry(uninstallKey);
  assert.ok(registration?.includes('Yee'), 'Yee 전용 제거 항목 등록');
  const uninstallCommand = registration.split(/\r?\n/).find(line=>/^\s*UninstallString\s+/i.test(line));
  assert.ok(uninstallCommand?.toLowerCase().includes(`\\${identity.profile_directory}\\application\\`.toLowerCase()),
    '제거 명령이 Yee 설치 경로를 가리킴');
  assert.deepEqual(unrelatedRegistry(),unrelatedBefore,'설치·수리가 다른 브라우저 등록을 덮지 않음');
}
async function verifyInstalledFiltering() {
  assertNoYee();
  const child=spawn(process.execPath,[path.join(root,'tools/dev/test-content-blocking-rendering.mjs'),executable],
    {windowsHide:true,stdio:['ignore','pipe','pipe']});
  for(const stream of [child.stdout,child.stderr])stream.on('data',data=>
    fs.appendFile(path.join(output,'installed-content-blocking.log'),data).catch(()=>{}));
  const code=await new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',resolve)});
  assert.equal(code,0,'설치된 앱의 실제 요청·문서·대체 응답 필터');
  reports.push({label:'installed-content-blocking',exitCode:code});
}
async function evaluateReady(page, expression) {
  for(let attempt=0;attempt<200;attempt++) {
    try {
      const evaluated=await page.call('Runtime.evaluate',{expression,returnByValue:true});
      if(evaluated.result?.value !== undefined)return evaluated.result.value;
    } catch(error) {
      if(!/Execution context was destroyed|Cannot find context with specified id/.test(error.message))throw error;
    }
    await pause(100);
  }
  throw Error('화면 전환 후 검사 문서 준비 지연');
}

const server = http.createServer((request,response) => {
  response.setHeader('Content-Type','text/html; charset=utf-8');
  response.setHeader('Cache-Control','no-store');
  response.end(`<!doctype html><meta charset="utf-8"><title>Yee 설치 검증</title><main><h1>Yee 설치·프로필 검증</h1><p>정상 페이지와 설정 보존을 확인합니다.</p></main><script>window.previous=localStorage.getItem('yee-install-test');localStorage.setItem('yee-install-test',${JSON.stringify(token)});</script>`);
});
await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const testUrl = `http://127.0.0.1:${server.address().port}/`;
async function launchAndVerify(previous, name) {
  assertNoYee();
  await fs.unlink(path.join(profile,'DevToolsActivePort')).catch(error => {if(error.code!=='ENOENT')throw error;});
  const child = spawn(executable,['--remote-debugging-port=0','--no-first-run','--no-default-browser-check',testUrl],
    {windowsHide:true,stdio:['ignore','ignore','pipe']});
  child.stderr.on('data',data => fs.appendFile(path.join(output,`${name}.log`),data).catch(()=>{}));
  let browser,page;
  try {
    let endpoint;
    for(let attempt=0;attempt<300;attempt++) {
      assert.equal(child.exitCode,null,'설치한 Yee 시작 실패');
      try {endpoint=(await fs.readFile(path.join(profile,'DevToolsActivePort'),'utf8')).trim().split(/\r?\n/);break;}catch{}
      await pause(100);
    }
    assert.ok(endpoint,'별도 user-data-dir 없이 Yee 기본 프로필 사용');
    browser=await connectCDP(`ws://127.0.0.1:${endpoint[0]}${endpoint[1]}`);
    let target;
    for(let attempt=0;attempt<100;attempt++) {
      const tabs=await(await fetch(`http://127.0.0.1:${endpoint[0]}/json/list`)).json();
      target=tabs.find(tab=>tab.type==='page'&&tab.url===testUrl);
      if(target)break;
      await pause(100);
    }
    assert.ok(target,'설치한 브라우저에서 실제 정상 페이지 표시');
    page=await connectCDP(target.webSocketDebuggerUrl);
    const result=await evaluateReady(page,`document.readyState==='complete'&&Object.hasOwn(window,'previous')&&localStorage.getItem('yee-install-test')===${JSON.stringify(token)}?({previous:window.previous,current:localStorage.getItem('yee-install-test'),text:document.body.innerText}):undefined`);
    assert.equal(result.previous,previous,'수리·재설치 후 웹 저장 데이터 보존');
    assert.equal(result.current,token);
    assert.ok(result.text.includes('정상 페이지'));
    const screenshot=await page.call('Page.captureScreenshot',{format:'png'});
    await fs.writeFile(path.join(output,`${name}.png`),Buffer.from(screenshot.data,'base64'));
    await page.call('Page.navigate',{url:'chrome://version/'});
    const actualProfile=await evaluateReady(page,'document.getElementById("profile_path")?.textContent.trim() || undefined');
    assert.equal(path.resolve(actualProfile).toLowerCase(),path.join(profile,'Default').toLowerCase(),'native가 보고한 실제 전용 프로필 경로');
    await fs.writeFile(marker,token);
    reports.push({label:name,profile:actualProfile,visibleText:result.text});
  } finally {
    page?.close();try{await browser?.call('Browser.close');}catch{}browser?.close();
    for(let attempt=0;attempt<200&&child.exitCode===null;attempt++)await pause(100);
    assert.notEqual(child.exitCode,null,'검증 브라우저 정상 종료');
  }
}

try {
  assertNoYee();
  assert.equal(await exists(productRoot),false,'기존 Yee 설치·사용자 데이터가 있으면 검증을 중단한다.');
  for(const view of ['32','64']) {
    assert.equal(queryRegistry(uninstallKey,view),null,'기존 Yee 제거 항목 보호');
    for(const hive of ['HKCU','HKLM']) {
      assert.equal(queryRegistry(`${hive}\\Software\\Policies\\${identity.profile_directory}`,view),null,
        '다른 프로필로 경로를 바꿀 수 있는 기존 Yee 정책이 있으면 중단한다.');
    }
  }
  unrelatedBefore = unrelatedRegistry();
  await fs.writeFile(path.join(output,'before.json'),JSON.stringify({productRoot,token,installerSha256:release.sha256,unrelatedRegistry:unrelatedBefore},null,2));
  await runSetup(installer,['--do-not-launch-chrome'],[0],'install');installed=true;
  await verifyPayload();
  await launchAndVerify(null,'first-launch');
  await runSetup(installer,['--do-not-launch-chrome'],[0],'repair');
  assert.match(queryRegistry(`HKCU\\Software\\${identity.profile_directory}`)||'',/InstallerError\s+REG_DWORD\s+0x1\b/i,'native 수리 상태 기록');
  await verifyPayload();
  await launchAndVerify(token,'after-repair');
  await verifyInstalledFiltering();
  let setup=path.join(application,release.version,'Installer/setup.exe');
  await runSetup(setup,['--uninstall','--force-uninstall'],[19],'uninstall-keep-data');installed=false;
  assert.equal(await exists(executable),false);
  assert.equal(queryRegistry(uninstallKey),null);
  assert.equal(await fs.readFile(marker,'utf8'),token,'표준 제거는 프로필을 보존');
  await runSetup(installer,['--do-not-launch-chrome'],[0],'reinstall');installed=true;
  await verifyPayload();
  await launchAndVerify(token,'after-reinstall');
  setup=path.join(application,release.version,'Installer/setup.exe');
  assert.equal(await fs.readFile(marker,'utf8'),token,'삭제 대상이 이번 시험의 프로필임을 확인');
  await runSetup(setup,['--uninstall','--force-uninstall','--delete-profile'],[19],'uninstall-delete-test-data');installed=false;
  assert.equal(await exists(executable),false);
  assert.equal(await exists(profile),false,'명시적으로 선택한 시험 프로필 삭제');
  assert.equal(queryRegistry(uninstallKey),null);
  assert.deepEqual(unrelatedRegistry(),unrelatedBefore,'기존 Chromium·Chrome 등록과 기본 브라우저 유지');
  // 처음에 없던 시험 전용 루트의 빈 폴더만 정리한다. 남은 파일은 지우지 않는다.
  await fs.rmdir(productRoot).catch(error=>{if(!['ENOENT','ENOTEMPTY'].includes(error.code))throw error;});
  await fs.writeFile(path.join(output,'result.json'),JSON.stringify({passed:true,reports},null,2));
  console.log(`PASS: actual install, default profile, repair, reinstall and uninstall. Evidence: ${output}`);
} finally {
  server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
  if(installed)console.error(`검증이 완료되지 않았습니다. 설치 상태를 진단용으로 보존했습니다: ${application}`);
}
