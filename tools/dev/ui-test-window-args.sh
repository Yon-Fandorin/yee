#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

# 다른 언어의 검사도 같은 모니터 선택과 실패 처리를 사용한다.
prepare_ui_test_display
if (( ${#YEE_UI_TEST_WINDOW_ARGS[@]} )); then
  print -rl -- "${YEE_UI_TEST_WINDOW_ARGS[@]}"
fi
