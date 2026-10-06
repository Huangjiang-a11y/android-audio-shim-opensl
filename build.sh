#!/usr/bin/env bash
#
# Build script for Android Audio Shim (OpenSL ES fork)
#
# Requirements:
#   - JDK 8+ (javac, jar)
#   - Android NDK r25+ with the arm64 toolchain
#
# Usage:
#   ANDROID_NDK_HOME=/path/to/ndk ./build.sh
#   # or place the NDK at the default location
#
set -euo pipefail

# ---------------- Configuration ----------------
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-/usr/lib/android-sdk/ndk/25.2.9519653}}"
API="${API:-24}"        # Android API level (OpenSL ES needs >= 9; 24 is a safe floor)
ABI="aarch64"           # arm64 only

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$SCRIPT_DIR/out"
JAR="$SCRIPT_DIR/audio-shim.jar"
SO="$SCRIPT_DIR/libaudioshim.so"

# ---------------- Toolchain ----------------
HOST_TAG="linux-x86_64"
case "$(uname -s)" in
    Darwin) HOST_TAG="darwin-x86_64" ;;
esac
PREBUILT="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
CC="$PREBUILT/bin/aarch64-linux-android${API}-clang"
SYSROOT="$PREBUILT/sysroot"

if [ ! -x "$CC" ]; then
    echo "error: compiler not found: $CC" >&2
    echo "       set ANDROID_NDK_HOME to a valid NDK path" >&2
    exit 1
fi

# ---------------- Native ----------------
echo "[*] compiling native library (OpenSL ES, $ABI, API $API)"
"$CC" -shared -fPIC -O2 \
    -I"$SYSROOT/usr/include" \
    -o "$SO" \
    "$SCRIPT_DIR/native/audio_bridge.c" \
    -ldl -lOpenSLES
echo "    -> $SO"

# ---------------- Java ----------------
echo "[*] compiling java sources"
rm -rf "$OUT"
mkdir -p "$OUT"
javac -source 8 -target 8 -d "$OUT" "$SCRIPT_DIR"/src/de/maxhenkel/shim/*.java 2>/dev/null

# ---------------- Package ----------------
echo "[*] packaging jar"
cp -r "$SCRIPT_DIR/META-INF" "$OUT/"
cp "$SCRIPT_DIR/resources/mcmod.info" "$SCRIPT_DIR/resources/fabric.mod.json" "$OUT/"
mkdir -p "$OUT/natives"
cp "$SO" "$OUT/natives/"
( cd "$OUT" && jar cf "$JAR" . )
echo "    -> $JAR"
echo
echo "done."
