# Paths helpers; loaded by common.zsh.

YEE_ROOT="${COMMON_DIR:h:h}"
LOCAL_BUILD_ROOT="${YEE_LOCAL_BUILD_ROOT:-$YEE_ROOT/.local-build}"
DEPOT_TOOLS_DIR="$LOCAL_BUILD_ROOT/depot_tools"
CHROMIUM_ROOT="$LOCAL_BUILD_ROOT/chromium"
CHROMIUM_SRC="$CHROMIUM_ROOT/src"
YEE_OUT_NAME="YeePilot"
YEE_OUT_DIR="$CHROMIUM_SRC/out/$YEE_OUT_NAME"
YEE_PRODUCT_NAME="$(python3 "$YEE_ROOT/tools/overlay/brand_config.py" show --get name)"
YEE_APP_DIR="$YEE_OUT_DIR/$YEE_PRODUCT_NAME.app"
YEE_BROWSER_BIN="$YEE_APP_DIR/Contents/MacOS/$YEE_PRODUCT_NAME"
YEE_BROWSER_PROCESS_PATTERN="$(python3 -c 'import re, sys; print(re.escape(sys.argv[1]))' "$YEE_BROWSER_BIN")"
YEE_UNBUNDLED_FRAMEWORK_BIN="$YEE_OUT_DIR/$YEE_PRODUCT_NAME Framework.framework/$YEE_PRODUCT_NAME Framework"
YEE_BUNDLED_FRAMEWORK_BIN="$YEE_APP_DIR/Contents/Frameworks/$YEE_PRODUCT_NAME Framework.framework/$YEE_PRODUCT_NAME Framework"
YEE_ARGS_FILE="$YEE_ROOT/build/args.gn"
METAL_TOOLCHAIN_CACHE="$LOCAL_BUILD_ROOT/metal-toolchain-path"
YEE_BUILD_JOBS="${YEE_BUILD_JOBS:-3}"

print_paths() {
  print "project root:   $YEE_ROOT"
  print "depot_tools:    $DEPOT_TOOLS_DIR"
  print "Chromium src:   $CHROMIUM_SRC"
  print "build output:   $YEE_OUT_DIR"
}
