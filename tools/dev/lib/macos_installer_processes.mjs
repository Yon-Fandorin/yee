// ps 목록을 검사 앱·프로필과 무관한 제품 프로세스로 나눈다. 프로세스에는 접근하지 않는다.
export function filterInstallerProcesses(listing, {productName, application, profile}) {
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const name = escape(productName);
  const executablePattern = new RegExp(
    `^"?((?:/[^\\n]*?)?/${name}(?: Helper[^/]*?)?\\.app/Contents/` +
    `(?:.*?/)?MacOS/${name}(?: Helper(?: \\([^)]*\\))?)?)"?(?=\\s|$)`);
  const bundleExecutablePattern = new RegExp(
    `^"?((?:/[^\\n]*?)?/${name}(?: Helper[^/]*?)?\\.app/Contents/(?:.*/)?[^\\s/"]+)"?$`);
  const directory = escape(profile);
  const profilePattern = new RegExp(
    `(?:^|\\s)(?:"--user-data-dir(?:=|\\s+)${directory}"|` +
    `--user-data-dir(?:=|\\s+)(?:"${directory}"|${directory}))(?=\\s|$)`);
  const owned = [], unrelated = [];
  for (const line of listing.split(/\r?\n/)) {
    const process = line.match(/^\s*(\d+)\s+(.+)$/);
    if (!process) continue;
    // 먼저 플래그를 떼어 argv 안에 언급된 다른 앱 경로를 실행 파일로 오인하지 않는다.
    const commandPath = process[2].split(/\s+--/, 1)[0];
    const executable = commandPath.match(executablePattern)?.[1]
      ?? commandPath.match(bundleExecutablePattern)?.[1];
    if (!executable) continue;
    const entry = {pid: Number(process[1]), executable};
    const target = executable.startsWith(application + '/Contents/') || profilePattern.test(process[2]);
    (target ? owned : unrelated).push(entry);
  }
  return {owned, unrelated};
}
