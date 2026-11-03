# Preflight helpers; loaded by common.zsh.

require_yee_build_root_selection() {
  [[ "$YEE_LOCAL_BUILD_ROOT_EXPLICIT" == true ]] && return 0
  [[ -x "$DEPOT_TOOLS_DIR/gclient" && -f "$CHROMIUM_SRC/BUILD.gn" ]] && return 0

  local candidate
  local -a candidates=()
  # 링크로 연결한 루트도 내부 파일 표식으로 확인한다.
  for candidate in "$YEE_ROOT"/.local-build/*(N); do
    if [[ -f "$candidate/chromium/src/BUILD.gn" || -f "$candidate/depot_tools/gclient" ]]; then
      candidates+=("$candidate")
    fi
  done
  (( ${#candidates[@]} == 0 )) && return 0

  print -u2 '환경 미설정: YEE_LOCAL_BUILD_ROOT가 없고 기본 루트의 개발 입력이 누락되었다.'
  print -u2 '후보 빌드 루트:'
  for candidate in "${candidates[@]}"; do
    print -u2 -r -- "  $candidate"
  done
  print -u2 '사용할 루트와 출력 폴더 이름을 확인해 두 환경 변수를 명시하라:'
  print -u2 -r -- "  export YEE_LOCAL_BUILD_ROOT=${(q)candidates[1]}"
  print -u2 '  export YEE_OUT_NAME="사용할 출력 폴더 이름"'
  print -u2 '기본 루트를 쓰려면 YEE_LOCAL_BUILD_ROOT="$YEE_ROOT/.local-build"를 명시하라.'
  print -u2 -r -- "  export YEE_LOCAL_BUILD_ROOT=${(q)YEE_ROOT}/.local-build"
  return 17
}

require_yee_output_selection() {
  [[ "$YEE_OUT_NAME_EXPLICIT" == true || -f "$YEE_OUT_DIR/build.ninja" ]] && return 0

  local manifest candidate
  local -a candidates=()
  for manifest in "$CHROMIUM_SRC"/out/*/build.ninja(N); do
    [[ -f "$manifest" ]] && candidates+=("${manifest:h}")
  done
  (( ${#candidates[@]} == 0 )) && return 0

  print -u2 -r -- "환경 미설정: YEE_OUT_NAME이 없고 기본 출력에 build.ninja가 없다: $YEE_OUT_DIR"
  print -u2 '후보 출력:'
  for candidate in "${candidates[@]}"; do
    print -u2 -r -- "  $candidate"
  done
  candidate="${candidates[1]:t}"
  print -u2 '사용할 출력 폴더를 선택해 환경 변수를 명시하라:'
  print -u2 -r -- "  export YEE_LOCAL_BUILD_ROOT=${(q)LOCAL_BUILD_ROOT}"
  print -u2 -r -- "  export YEE_OUT_NAME=${(q)candidate}"
  return 18
}

available_gib() {
  local available_kib
  available_kib="$(df -Pk "$YEE_ROOT" | awk 'NR == 2 { print $4 }')"
  print $((available_kib / 1024 / 1024))
}

require_free_gib() {
  local required_gib="$1"
  local purpose="$2"
  # Preserve the approved workspace reserve even for small/test targets.
  # Callers may require more space, but must not lower this floor.
  (( required_gib < 15 )) && required_gib=15
  local current_gib
  current_gib="$(available_gib)"

  if (( current_gib < required_gib )); then
    print -u2 "Need at least ${required_gib} GiB free for ${purpose}; ${current_gib} GiB is available."
    exit 4
  fi

  print "Disk guard: ${current_gib} GiB free (${required_gib} GiB required for ${purpose})."
}

require_depot_tools() {
  if [[ ! -x "$DEPOT_TOOLS_DIR/gclient" ]]; then
    require_yee_build_root_selection || return $?
    print -u2 "depot_tools is missing. Run ./tools/dev/checkout.sh first."
    exit 5
  fi

  export PATH="$PATH:$DEPOT_TOOLS_DIR"
}

require_yee_sync_jobs() {
  if [[ "$YEE_SYNC_JOBS" != <-> || "$YEE_SYNC_JOBS" -lt 1 ]]; then
    print -u2 "YEE_SYNC_JOBS는 양의 정수여야 한다: $YEE_SYNC_JOBS"
    return 14
  fi
}

require_chromium_src() {
  if [[ ! -f "$CHROMIUM_SRC/BUILD.gn" ]]; then
    require_yee_build_root_selection || return $?
    print -u2 "Chromium source is missing. Run ./tools/dev/checkout.sh first."
    exit 6
  fi
  python3 "$COMMON_DIR/chromium-version.py" verify "$CHROMIUM_SRC" --platform macos
}
