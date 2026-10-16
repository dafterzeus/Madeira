#!/bin/bash
# Configure (first time) and build FEX's aarch64 WOW64 module (libwow64fex.dll,
# shipped as xtajit.dll), the CPU backend wow64.dll loads for 32-bit x86
# programs. See docs/WOW64.md, "Building". The options are the iOS host build's:
# FEX_IOS_HOST_BUILD turns on the 32-bit guest window (ENABLE_GUEST_WINDOW),
# which FEX refuses for the ARM64EC module.
set -eu
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MINGW="${LLVM_MINGW:-$R/toolchains/llvm-mingw-20260421-ucrt-macos-universal/bin}"
export PATH="$MINGW:$PATH"
# Build a patched source copy, leaving both submodule pins and worktrees intact.
B="$R/FEX/build-madeira-wow64"
S="$B/src"
bash "$R/build/fex-arm64ec/prepare-source.sh" "$S"
FEX_HASH="$(git -C "$R/FEX" rev-parse HEAD)"
FEX_VERSION="$(git -C "$R/FEX" describe --abbrev=7 2>/dev/null || true)"
if [ ! -f "$B/CMakeCache.txt" ]; then
    cmake -S "$S" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$S/Data/CMake/toolchain_mingw.cmake" \
        -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON \
        -DOVERRIDE_HASH="$FEX_HASH" -DOVERRIDE_VERSION="$FEX_VERSION" \
        -DMINGW_TRIPLE=aarch64-w64-mingw32 \
        -DFEX_IOS_HOST_BUILD=ON -DCMAKE_C_FLAGS=-DFEX_IOS_HOST \
        -DCMAKE_CXX_FLAGS=-DFEX_IOS_HOST -DCMAKE_ASM_FLAGS=-DFEX_IOS_HOST \
        -DENABLE_LTO=OFF -DENABLE_ASSERTIONS=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF \
        -DBUILD_TESTING=OFF -DBUILD_FEXCONFIG=OFF -DTUNE_ARCH=generic -DTUNE_CPU=none \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
fi
cmake --build "$B" --target wow64fex
cp "$B/Bin/libwow64fex.dll" "$R/app/Madeira/aarch64-windows/xtajit.dll" && ls -l "$R/app/Madeira/aarch64-windows/xtajit.dll"
