#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
if (( $# > 1 )) || { (( $# == 1 )) && [[ "$1" != --no-build ]]; }; then
  print -u2 "Usage: $0 [--no-build]"
  exit 2
fi
if [[ "$(uname -s)" != Darwin ]]; then
  print -u2 'macOS 전용 설치 fixture gate다.'
  exit 2
fi
require_depot_tools
require_chromium_src
require_yee_output_selection
if [[ "${1:-}" != --no-build ]]; then
  require_free_gib 15 "macOS install fixtures"
  sync_yee_ui_sources
  build_regression_targets "macOS install" chrome/browser/mac:install_sh_test
fi
# 실제 설치를 바꾸는 upstream Install은 선택하지 않는다.
run_native_test_gate "$YEE_OUT_DIR/install_sh_test" \
  --gtest_filter='DiskImageInstallTest.*:InstallShTest.UpdaterBuildFlagControlsElevatedSteps' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
