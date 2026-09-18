#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
require_depot_tools
require_chromium_src
require_free_gib 15 "content blocking native tests"
prepare_yee_build_inputs
build_regression_targets "content blocking" \
  yee_content_blocking_unittests yee_filtering_factory_unittests
"$YEE_OUT_DIR/yee_content_blocking_unittests" --test-launcher-jobs=1
"$YEE_OUT_DIR/yee_filtering_factory_unittests" --test-launcher-jobs=1
