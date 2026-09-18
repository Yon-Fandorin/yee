#!/bin/zsh

set -euo pipefail

# Keep this entry point stable for build, validation, and research scripts.
COMMON_DIR="${${(%):-%N}:A:h}"
source "$COMMON_DIR/lib/paths.zsh"
source "$COMMON_DIR/lib/preflight.zsh"
source "$COMMON_DIR/lib/build.zsh"
source "$COMMON_DIR/lib/runtime.zsh"
source "$COMMON_DIR/lib/metal.zsh"
