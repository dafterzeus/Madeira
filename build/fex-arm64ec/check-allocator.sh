#!/bin/bash
# Linux host checks. Executes the actual iOS allocator code as x64 PE under
# Wine, including the Windows-specific paths. Uses an isolated Wine prefix.
# LLVM_MINGW=/path/to/toolchain/bin bash build/fex-arm64ec/check-allocator.sh
# --stress-only skips the longer pinned upstream unit tests.
set -eu
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIR="$R/build/fex-arm64ec"
MINGW="${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw bin directory}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/rpmalloc" "$TMP/wine"
cp -R "$R/FEX/External/rpmalloc/rpmalloc" "$TMP/rpmalloc/"
patch -d "$TMP" -p2 < "$DIR/small-spans.patch"
export WINEPREFIX="$TMP/wine" WINEDEBUG=-all DISPLAY=
export WINEDLLOVERRIDES=mscoree,mshtml,winemenubuilder.exe=d
# Existing upstream diagnostics have one unused variable. Dynamic TLS is also
# how the shipped modules are built, avoiding compiler TLS on the iOS host.
FLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable
       -DFEX_IOS_HOST=1 -DENABLE_ASSERTS=1 -DENABLE_OVERRIDE=0
       -DRPMALLOC_FIRST_CLASS_HEAPS=1 -DDYNAMIC_TLS_NO_FLS=1)
for variant in before after; do
    SRC="$R/FEX/External/rpmalloc/rpmalloc"
    if [ "$variant" = after ]; then SRC="$TMP/rpmalloc/rpmalloc"; fi
    "$MINGW/x86_64-w64-mingw32-clang" "${FLAGS[@]}" -I"$SRC" \
        "$DIR/allocator-check.c" -o "$TMP/$variant.exe"
    wine "$TMP/$variant.exe"
done
wine "$TMP/after.exe" boundaries
if [ "${1:-}" = --stress-only ]; then exit 0; fi
"$MINGW/x86_64-w64-mingw32-clang" "${FLAGS[@]}" -ffunction-sections \
    -I"$TMP/rpmalloc/rpmalloc" -I"$R/FEX/External/rpmalloc/test" \
    "$DIR/rpmalloc-unit-check.c" "$R/FEX/External/rpmalloc/test/thread.c" \
    -Wl,--gc-sections -o "$TMP/unit.exe"
for test in alloc realloc aligned cross-thread threaded thread-spam heaps; do
    wine "$TMP/unit.exe" "$test"
done
