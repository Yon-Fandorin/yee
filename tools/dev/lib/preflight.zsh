# Preflight helpers; loaded by common.zsh.

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
    print -u2 "depot_tools is missing. Run ./tools/dev/checkout.sh first."
    exit 5
  fi

  export PATH="$PATH:$DEPOT_TOOLS_DIR"
}

require_chromium_src() {
  if [[ ! -f "$CHROMIUM_SRC/BUILD.gn" ]]; then
    print -u2 "Chromium source is missing. Run ./tools/dev/checkout.sh first."
    exit 6
  fi
}
