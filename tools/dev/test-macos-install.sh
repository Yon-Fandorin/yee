#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
configure_test_tiers "" "" "$@"
if [[ "$(uname -s)" != Darwin ]]; then
  print -u2 'macOS 전용 설치 fixture gate다.'
  exit 2
fi
if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 15 "macOS install fixtures"
  sync_yee_ui_sources
  build_regression_targets "macOS install" chrome/browser/mac:install_sh_test
fi
# 실제 설치를 바꾸는 upstream Install은 선택하지 않는다.
run_tiered_native_test_gate "$YEE_OUT_DIR/install_sh_test" \
  --gtest_filter='YeeDiskImageInstallTest.*:YeeInstallShTest.UpdaterBuildFlagControlsElevatedSteps' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
