#!/usr/bin/env bash
# Cross-compiles the game engine for Android TV (arm64-v8a) and copies the
# libraries into the app's jniLibs so Gradle packages them into the APK.
#
# Usage: android/native/build.sh <flavor>      (flavor: nomercy | ecw)
# Needs: ANDROID_NDK_HOME (NDK r27+), cmake 3.20+, ninja, a host C compiler.
# The game code (RecompiledFuncs/) must already be generated from the ROM
# (see BUILDING.md); pass --engine-only to build everything except the game.
set -euo pipefail

FLAVOR=${1:?usage: build.sh <nomercy|ecw> [--engine-only]}
ENGINE_ONLY=${2:-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
NDK=${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to the Android NDK}
ABI=arm64-v8a
OUT=$ROOT/build-android-$FLAVOR
HOST=$ROOT/build-android-host

case $FLAVOR in
  nomercy) LIB=NoMercyRecompiled ;;
  ecw)     LIB=ECWRecompiled ;;
  *) echo "unknown flavor $FLAVOR"; exit 1 ;;
esac

# 1. Apply the Android patches to the runtime submodules (idempotent).
for p in "$ROOT"/android/native/patches/*.patch; do
  [ -e "$p" ] || continue
  sub=$(basename "$p" .patch); sub=${sub//__//}
  if git -C "$ROOT/$sub" apply --check "$p" 2>/dev/null; then
    git -C "$ROOT/$sub" apply "$p"; echo "applied $(basename "$p")"
  elif git -C "$ROOT/$sub" apply --reverse --check "$p" 2>/dev/null; then
    echo "already applied $(basename "$p")"
  else
    echo "patch $(basename "$p") does not apply to $sub"; exit 1
  fi
done

# 2. Host tools: file_to_c runs on the build machine during the build.
cmake -S "$ROOT/lib/rt64/src/tools/file_to_c" -B "$HOST" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$HOST"

# 3. Cross-compile the engine.
cmake -S "$ROOT" -B "$OUT" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DNMR_HOST_FILE_TO_C="$HOST/file_to_c" \
  -DNMR_ANDROID_LIB_NAME=$LIB \
  -DNMR_ENGINE_ONLY=$([ "$ENGINE_ONLY" = "--engine-only" ] && echo ON || echo OFF)

if [ "$ENGINE_ONLY" = "--engine-only" ]; then
  cmake --build "$OUT" --target recompui recompinput librecomp ultramodern rt64 nfd SDL2 NoMercyGameSources
  echo "engine-only build done (no game code linked)"
  exit 0
fi

cmake --build "$OUT" --target NoMercyRecompiled

# 4. Package into the APK's native libs and assets.
JNI=$ROOT/android/app/src/$FLAVOR/jniLibs/$ABI
mkdir -p "$JNI"
cp "$OUT/lib$LIB.so" "$OUT/libSDL2.so" "$JNI/"
cp "$NDK/toolchains/llvm/prebuilt/"*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so "$JNI/"
echo "native libraries copied to $JNI"
