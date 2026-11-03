const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

function profileArgument(argv) {
  let profile;
  for (let index = 1; index < argv.length; ++index) {
    // Chromium의 POSIX 스위치는 앞뒤 ASCII 공백을 제거하고 '=' 뒤만 값으로 읽는다.
    const argument = argv[index].replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, '');
    if (argument === '--') break;
    const match = argument.match(/^--?user-data-dir(?:=([\s\S]*))?$/);
    if (match) profile = match[1] ?? '';
  }
  return profile;
}

// 식별된 번들 프로세스 중 검사 앱 내부 경로나 정확한 전용 프로필만 종료 대상으로 삼는다.
export function filterValidationProcesses(processes, {application, profile}) {
  const directory = escape(profile);
  const profilePattern = new RegExp(
    `(?:^|\\s)(?:"--user-data-dir(?:=|\\s+)${directory}"|` +
    `--user-data-dir(?:=|\\s+)(?:"${directory}"|${directory}))(?=\\s|$)`);
  const owned = [], unrelated = [];
  for (const {command = '', arguments: argv, ...entry} of processes) {
    // 관찰 검사는 실제 argv를 넘긴다. 설치 검사의 기존 ps 문자열 입력도 유지한다.
    const profileMatched = argv !== undefined ? profileArgument(argv) === profile : profilePattern.test(command);
    const target = entry.executable.startsWith(application + '/Contents/') || profileMatched;
    (target ? owned : unrelated).push(entry);
  }
  return {owned, unrelated};
}

// ps 목록을 검사 앱·프로필과 무관한 제품 프로세스로 나눈다. 프로세스에는 접근하지 않는다.
export function filterInstallerProcesses(listing, {productName, application, profile}) {
  const name = escape(productName);
  const executablePattern = new RegExp(
    `^"?((?:/[^\\n]*?)?/${name}(?: Helper[^/]*?)?\\.app/Contents/` +
    `(?:.*?/)?MacOS/${name}(?: Helper(?: \\([^)]*\\))?)?)"?(?=\\s|$)`);
  const bundleExecutablePattern = new RegExp(
    `^"?((?:/[^\\n]*?)?/${name}(?: Helper[^/]*?)?\\.app/Contents/(?:.*/)?[^\\s/"]+)"?$`);
  const processes = [];
  for (const line of listing.split(/\r?\n/)) {
    const process = line.match(/^\s*(\d+)\s+(.+)$/);
    if (!process) continue;
    // 먼저 플래그를 떼어 argv 안에 언급된 다른 앱 경로를 실행 파일로 오인하지 않는다.
    const commandPath = process[2].split(/\s+--/, 1)[0];
    const executable = commandPath.match(executablePattern)?.[1]
      ?? commandPath.match(bundleExecutablePattern)?.[1];
    if (!executable) continue;
    processes.push({pid: Number(process[1]), executable, command: process[2]});
  }
  return filterValidationProcesses(processes, {application, profile});
}
