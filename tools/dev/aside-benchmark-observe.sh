#!/bin/zsh
# Quote-safe adapter for Aside's documented one-shot JavaScript CLI. No model
# call, page mutation, personal tab discovery or observation rewriting occurs.
set -euo pipefail
if (( $# != 1 )) || [[ ! "$1" =~ '^[A-F0-9]{32}$' ]]; then
  print -u2 'Usage: aside-benchmark-observe.sh TEST_TAB_TARGET_ID'
  exit 2
fi
exec /Users/yongjunkim/.local/bin/aside repl \
  "var bench = await attachBrowserTab('$1'); if (await bench.evaluate(() => location.href) !== 'http://127.0.0.1:8766/agent-browser-prototype.html') throw new Error('not synthetic fixture'); console.log((await snapshot(bench)).tree)"
