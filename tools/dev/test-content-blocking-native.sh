#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
configure_test_tiers "" "" "$@"
if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 15 "content blocking native tests"
  prepare_yee_build_inputs
  build_regression_targets "content blocking" \
    components/yee_content_blocking:yee_content_blocking_unittests \
    chrome/browser/yee_content_blocking:yee_filtering_factory_unittests \
    chrome/browser/yee_content_blocking:yee_service_worker_unittests
fi
run_tiered_native_test_gate "$YEE_OUT_DIR/yee_content_blocking_unittests" --gtest_filter='*' --test-launcher-jobs=1
run_tiered_native_test_gate "$YEE_OUT_DIR/yee_filtering_factory_unittests" --gtest_filter='*' --test-launcher-jobs=1
run_tiered_native_test_gate "$YEE_OUT_DIR/yee_service_worker_unittests" \
  --gtest_filter='YeeServiceWorkerContainerHostTest.WrapSubresourceLoaderFactory_*' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
