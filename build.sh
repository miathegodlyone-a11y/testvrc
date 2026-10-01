#!/usr/bin/env bash
set -e

# ── Config ────────────────────────────────────────────────────────────────────
NDK=${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/$(ls $HOME/Android/Sdk/ndk/ 2>/dev/null | tail -1)}
ABI="arm64-v8a"          # Quest 3 is arm64 only
API=29                   # Android 10 — minimum for Quest OS
BUILD_TYPE="Release"
OUT="release"
MODULE_NAME="zygisk-vrchat-test"
# ─────────────────────────────────────────────────────────────────────────────

if [[ ! -d "$NDK" ]]; then
    echo "ERROR: NDK not found at $NDK"
    echo "Set ANDROID_NDK_HOME or install NDK via Android Studio SDK Manager"
    exit 1
fi

BUILD_DIR="build/${ABI}"
mkdir -p "$BUILD_DIR" "$OUT/zygisk"

cmake \
    -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DANDROID_STL="c++_static"

cmake --build "$BUILD_DIR" -j$(nproc)

# Copy .so into module zygisk folder (filename = ABI)
cp "$BUILD_DIR/lib${MODULE_NAME}.so" "module/zygisk/${ABI}.so"

# Package zip
ZIP="${OUT}/${MODULE_NAME}.zip"
cd module
zip -r9 "../$ZIP" . >&/dev/null
cd ..

echo "Built: $ZIP"
