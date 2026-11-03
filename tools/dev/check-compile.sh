#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

LIST_ONLY=false
plan_options=()
for argument in "$@"; do
  case "$argument" in
    --list) LIST_ONLY=true ;;
    --include-unbuilt) plan_options=(--include-unbuilt) ;;
    *)
      print -u2 '사용법: ./tools/dev/check-compile.sh [--list] [--include-unbuilt]'
      exit 2
      ;;
  esac
done

require_depot_tools
require_chromium_src
require_yee_output_selection
require_yee_build_jobs
if [[ ! -f "$YEE_OUT_DIR/build.ninja" ]]; then
  print -u2 "기존 build.ninja가 필요하다: $YEE_OUT_DIR/build.ninja"
  print -u2 '먼저 configure.sh로 빌드 설정을 준비하라.'
  exit 6
fi

PREFLIGHT="$SCRIPT_DIR/lib/compile_preflight.py"
if [[ "$LIST_ONLY" == true ]]; then
  exec python3 "$PREFLIGHT" plan "$CHROMIUM_SRC" "$YEE_OUT_DIR" --list "${plan_options[@]}"
fi

require_free_gib 15 'Chromium 이식 사전 컴파일'
# sync_yee_ui_sources가 패치 적용으로 전환하지 않도록 기존 overlay 설치를 요구한다.
if [[ ! -f "$CHROMIUM_SRC/chrome/browser/ui/views/yee/BUILD.gn" ]]; then
  print -u2 '설치된 Yee owned 소스가 없다. 먼저 configure.sh를 실행하라.'
  exit 6
fi
sync_yee_ui_sources
python3 "$PREFLIGHT" check-graph "$YEE_OUT_DIR"

mkdir -p "$LOCAL_BUILD_ROOT/logs"
LOG_DIR="$(mktemp -d "$LOCAL_BUILD_ROOT/logs/compile-preflight.XXXXXX")"
print "사전 컴파일 기록: $LOG_DIR"
python3 "$PREFLIGHT" plan "$CHROMIUM_SRC" "$YEE_OUT_DIR" "${plan_options[@]}" --save-to "$LOG_DIR"
targets=("${(@f)$(cat -- "$LOG_DIR/targets.txt")}")
if [[ ! -s "$LOG_DIR/targets.txt" ]]; then
  print -u2 '이 graph에서 컴파일할 대상이 없다. 미컴파일 목록을 다른 플랫폼·구성에서 점검하라.'
  exit 16
fi

build_result=0
if build_regression_targets 'compile preflight' -k 0 "${targets[@]}" 2>&1 | tee "$LOG_DIR/build.log"; then
  print '선택한 대상의 사전 컴파일 완료.'
  python3 "$PREFLIGHT" summary "$LOG_DIR"
  print "사전 컴파일 기록: $LOG_DIR"
else
  results=("${pipestatus[@]}")
  build_result="${results[1]}"
  (( build_result == 0 )) && build_result="${results[2]}"
  python3 "$PREFLIGHT" failures "$LOG_DIR" || true
  print -u2 "사전 컴파일 실패 (종료 코드 $build_result). 기록: $LOG_DIR"
fi
exit "$build_result"
