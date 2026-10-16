#!/bin/bash
# Requires LLVM 15 development headers/libraries and a C++20 compiler.
# LLVM15_ROOT may name an extracted LLVM 15 installation (no system install).
set -eu
R="$(cd "$(dirname "$0")/../.." && pwd)"
B="${AIRCONV_TEST_BUILD:-$(mktemp -d)}"
if [ -z "${AIRCONV_TEST_BUILD:-}" ]; then trap 'rm -rf "$B"' EXIT; fi
mkdir -p "$B/obj"
bash "$R/build/dxmt-ios/prepare-source.sh" "$B/src"
S="$B/src"
# On Linux uint64_t is unsigned long; upstream's 0ull is ambiguous with LLVM's
# pointer overload. Normalize that spelling only in this disposable test copy.
python3 - "$S/src/airconv" <<'PY'
import pathlib, re, sys
for p in pathlib.Path(sys.argv[1]).rglob('*.cpp'):
    s = p.read_text()
    if '0ull' in s:
        p.write_text(re.sub(r'\b0ull\b', 'uint64_t(0)', s))
PY
LLVM15_ROOT="${LLVM15_ROOT:-/usr/lib/llvm-15}"
CXX="${CXX:-clang++}"
FLAGS=(-std=c++20 -O0 -g -fno-rtti -ffunction-sections -fdata-sections
    -Wno-ignored-attributes -Wno-microsoft-exception-spec -Wno-nontrivial-memcall -Wno-extern-c-compat
    -include optional -include string
    -I"$LLVM15_ROOT/include" -I"$S/include" -I"$S/libs"
    -I"$S/include/native/directx" -I"$S/include/native/windows" -I"$S/src/airconv")
# Discard unused public compile entry points: this test directly exercises
# initialization and GS IR conversion, without embedded Metal helper bitcode.
for name in dxbc_converter dxbc_signature dxbc_converter_cfg dxbc_instructions \
            air_signature air_type air_operations dxbc_converter_gs \
            dxbc_converter_basicblock dxbc_converter_ts nt/air_builder nt/dxbc_converter_base; do
    "$CXX" "${FLAGS[@]}" -c "$S/src/airconv/$name.cpp" -o "$B/obj/${name##*/}.o"
done
for name in BlobContainer DXBCUtils ShaderBinary; do
    "$CXX" "${FLAGS[@]}" -c "$S/libs/DXBCParser/$name.cpp" -o "$B/obj/$name.o"
done
"$CXX" "${FLAGS[@]}" "$R/tests/host/airconv-gs.cpp" "$B/obj/"*.o \
    -Wl,--gc-sections -L"$LLVM15_ROOT/lib" -Wl,-rpath,"$LLVM15_ROOT/lib" -lLLVM-15 -o "$B/airconv-gs"
"$B/airconv-gs"
