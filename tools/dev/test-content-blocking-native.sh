#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
if [[ "${1:-}" != "" && "${1:-}" != "--no-build" ]]; then
  print -u2 "Usage: $0 [--no-build]"
  exit 2
fi
require_depot_tools
require_chromium_src
require_yee_output_selection
if [[ "${1:-}" != "--no-build" ]]; then
  require_free_gib 15 "content blocking native tests"
  prepare_yee_build_inputs
  build_regression_targets "content blocking" \
    components/yee_content_blocking:yee_content_blocking_unittests \
    chrome/browser/yee_content_blocking:yee_filtering_factory_unittests \
    chrome/browser/yee_content_blocking:yee_service_worker_unittests
fi
run_native_test_gate "$YEE_OUT_DIR/yee_content_blocking_unittests" --gtest_filter='*' --test-launcher-jobs=1
run_native_test_gate "$YEE_OUT_DIR/yee_filtering_factory_unittests" --gtest_filter='*' --test-launcher-jobs=1
run_native_test_gate "$YEE_OUT_DIR/yee_service_worker_unittests" \
  --gtest_filter='ServiceWorkerContainerHostTest.WrapSubresourceLoaderFactory_*' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
