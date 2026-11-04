# 영역별 gate의 모드·tier 선택과 필터 출력을 담당한다.

configure_test_tiers() {
  MODE="$1"
  local allowed_modes="$2" argument mode_explicit=false
  shift 2
  SKIP_BUILD=false
  PRINT_FILTERS=false
  YEE_TEST_TIER=background
  for argument in "$@"; do
    case "$argument" in
      --no-build)
        if [[ "$SKIP_BUILD" == true ]]; then
          print -u2 "Usage: $0 [$allowed_modes] [--no-build] [--tier=background|foreground|all] [--print-filters]"
          return 2
        fi
        SKIP_BUILD=true
        ;;
      --print-filters) PRINT_FILTERS=true ;;
      --tier=background|--tier=foreground|--tier=all)
        YEE_TEST_TIER="${argument#--tier=}"
        ;;
      *)
        if [[ "$mode_explicit" == false && "$SKIP_BUILD" == false && -n "$argument" && "$argument" != *'|'* &&
              "|$allowed_modes|" == *"|$argument|"* ]]; then
          MODE="$argument"
          mode_explicit=true
        else
          print -u2 "Usage: $0 [$allowed_modes] [--no-build] [--tier=background|foreground|all] [--print-filters]"
          return 2
        fi
        ;;
    esac
  done
  [[ "$PRINT_FILTERS" == true ]] && SKIP_BUILD=true
  return 0
}

run_tiered_native_test_gate() {
  local use_display=false
  if [[ "$1" == --display ]]; then
    use_display=true
    shift
  fi
  local test_binary="$1" argument original_filter='' platform
  shift
  local -a test_arguments=() resolution=()
  for argument in "$@"; do
    if [[ "$argument" == --gtest_filter=* ]]; then
      original_filter="${argument#--gtest_filter=}"
    else
      test_arguments+=("$argument")
    fi
  done
  [[ -n "$original_filter" ]] || return 2
  platform="$(uname -s)"
  case "$platform" in MINGW*|MSYS*|CYGWIN*) platform=Windows ;; esac
  local resolved
  resolved="$(python3 "$COMMON_DIR/lib/test_tiers.py" \
    --manifest "$YEE_ROOT/tests/tooling/test_tiers.json" \
    --binary="${test_binary:t}" \
    --platform "$platform" --tier "$YEE_TEST_TIER" --filter="$original_filter")" || return $?
  resolution=("${(@f)resolved}")
  if [[ "${resolution[1]}" == none ]]; then
    print -r -- "tier=$YEE_TEST_TIER cases=none binary=${test_binary:t}"
    return 0
  fi
  if [[ "$PRINT_FILTERS" == true ]]; then
    print -r -- "tier=$YEE_TEST_TIER cases=${resolution[1]} binary=${test_binary:t} ${(j: :)test_arguments} --gtest_filter=${resolution[2]}"
    return 0
  fi
  if [[ "$use_display" == true && "${resolution[1]}" == *[BC]* ]]; then
    prepare_ui_test_display
    gracefully_quit_yee
    test_arguments+=("${YEE_UI_TEST_WINDOW_ARGS[@]}")
  fi
  run_native_test_gate "$test_binary" "--gtest_filter=${resolution[2]}" "${test_arguments[@]}"
}
