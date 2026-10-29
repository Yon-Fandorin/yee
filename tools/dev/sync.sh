#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
require_yee_sync_jobs

require_depot_tools
require_chromium_src
require_free_gib 35 "a Chromium dependency sync"

cd "$CHROMIUM_ROOT"
# 기준 검사를 통과한 src의 제품 수정은 유지하고 의존 소스만 갱신한다.
caffeinate -dimsu gclient sync --no-history --revision src@unmanaged --jobs "$YEE_SYNC_JOBS"

print "Sync complete. Free space: $(available_gib) GiB"
