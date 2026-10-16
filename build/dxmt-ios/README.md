# DXMT on iOS

Builds the iOS side of DXMT — the D3D11→Metal translation layer — as a
static library that links into Madeira.app, plus the aarch64-windows PE
DLLs the game loads via Wine.

The upstream DXMT source lives at `dxmt/` as a git submodule
pointing at our fork [willfaust/dxmt](https://github.com/willfaust/dxmt),
`ios-port` branch. The base iOS port is committed there. Fork-specific build
patches here are applied to `build/dxmt-ios/src/` by `prepare-source.sh`, without
changing the submodule revision or worktree.

## GS inherited system values

`gs-input-siv.patch` ports the clip/cull input cases from upstream DXMT
[767d4627ca7c](https://github.com/3Shain/dxmt/commit/767d4627ca7c32bd0c739502aaa6303133c50157)
and also accepts render-target/viewport array indices. Together with position,
these are the five inherited per-vertex system values accepted by
`dcl_input_siv` in a geometry shader. Microsoft's
[signature interpretation table](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#shader-signatures)
lists these at GSVIn; inherited instance IDs are arbitrary inputs. Primitive ID
uses the separate `INPUT_PRIMITIVEID` operand (`dcl_input vPrim`), already
provided by `resource_map.patch_id`, rather than a vertex payload register.

The VS writes raw registers into the object payload; the GS's `load_vertex`
copies complete int4 registers. No new Metal input attribute or repacking is
needed: float distances, integer array indices, and packed component masks
retain their register positions and bits. Even iOS's dropped cull rasterizer
output still reserves its VS register through the common max-output update.

Accepted semantic IDs are logged once each per process, with register and mask.
Unsupported GS input SIV/SGV declarations stop parsing and return an SM50
initialization error containing opcode, semantic ID, register, and mask. The
existing pipeline-cache error log and `AddGeometryShader` propagate `E_FAIL`.

Airconv lives in the main Mach-O `Madeira` executable via
`libdxmt_combined.a`. The shipped PE `winemetal.dll` contains thunks; changing
`d3d11.dll` alone cannot fix this assertion. Run the fork's `build-ipa.yml` to
rebuild and sign the app (the PE farms remain unchanged). CI verifies the new
diagnostics are linked and the old GS SIV assertion is absent.

Native regression (Linux, LLVM 15 headers/libs, C++20 compiler):

```sh
LLVM15_ROOT=/usr/lib/llvm-15 bash tests/host/check-airconv-gs.sh
```

This uses actual DXBCParser/airconv code: all five semantics and component
masks, packed clip `.w`/cull `.z` in register 7, primitive ID, unsupported values
with and without an error receiver, and LLVM verification of GS payload IR.

## What this produces

- `libdxmt_combined.a` (~79MB) → `app/Madeira/libdxmt_combined.a`.
  DXMT's unix/Metal side + airconv shader translator + LLVM 15 static
  libs, combined into one archive Xcode links.
- `d3d11.dll`, `dxgi.dll`, `winemetal.dll`, `d3d10core.dll` →
  `app/Madeira/aarch64-windows/`. Aarch64 Windows PE, built via
  llvm-mingw, loaded by Wine inside Madeira.

Both are gitignored — rebuild via the steps below.

## Prerequisites

1. Clone with submodules: `git clone --recurse-submodules ...`, or run
   `git submodule update --init --recursive` after cloning.

2. **llvm-mingw** (aarch64-w64-mingw32) at
   `toolchains/llvm-mingw-20260421-ucrt-macos-universal/`:
   ```
   mkdir -p toolchains && \
   curl -L https://github.com/mstorsjo/llvm-mingw/releases/download/20260421/llvm-mingw-20260421-ucrt-macos-universal.tar.xz \
       | tar -xJ -C toolchains/
   ```

3. **LLVM 15.0.7 cross-built for iOS-aarch64** at `toolchains/llvm-ios-build/`.
   Two-stage build — first llvm-tblgen for macOS host, then iOS target libs
   reusing it. Flags used (summary):
   - Host stage: vanilla Release build of `llvm-tblgen`.
   - iOS stage: `CMAKE_SYSTEM_NAME=iOS`, `CMAKE_OSX_SYSROOT=iphoneos`,
     `LLVM_TABLEGEN=<host tblgen>`, `LLVM_BUILD_UTILS=Off`, no targets.
     Needs a one-line edit to `llvm/cmake/modules/AddLLVM.cmake` changing
     `MATCHES "Darwin"` to `MATCHES "Darwin|iOS"` so `-dead_strip` is used
     instead of `--gc-sections` (which Apple ld doesn't accept).

4. **Metal Toolchain**: `xcodebuild -downloadComponent MetalToolchain`

5. **Wine aarch64-windows static libs** already built in
   `wine/build-macos/` — see `build/ntdll-unix/build.sh` and the Wine
   submodule for the original configure.

6. **DXMT's own nested submodules**: nvapi + mingw-directx-headers.
   Covered by the top-level recursive submodule update in step 1.

## Build steps

### PE side (d3d11 / dxgi / winemetal / d3d10core)

The meson cross file uses `@GLOBAL_SOURCE_ROOT@/toolchains/...` paths, so
symlink our toolchains dir into the DXMT submodule once:

```
ln -s ../toolchains dxmt/toolchains
```

Then:

```
cd dxmt
SDKROOT=$(xcrun --sdk macosx --show-sdk-path) \
    PATH="$(pwd)/toolchains/llvm-mingw-20260421-ucrt-macos-universal/bin:/usr/bin:/opt/homebrew/bin:$PATH" \
    meson setup --cross-file build-aarch64-win.txt --native-file build-osx.txt \
                -Dwine_build_path=../../wine/build-macos build-pe
SDKROOT=$(xcrun --sdk macosx --show-sdk-path) \
    PATH="$(pwd)/toolchains/llvm-mingw-20260421-ucrt-macos-universal/bin:/usr/bin:/opt/homebrew/bin:$PATH" \
    meson compile -C build-pe
cp build-pe/src/d3d11/d3d11.dll \
   build-pe/src/dxgi/dxgi.dll \
   build-pe/src/winemetal/winemetal.dll \
   build-pe/src/d3d10/d3d10core.dll \
   ../../app/Madeira/aarch64-windows/
```

### Unix side (libdxmt_combined.a)

```
./build.sh                    # builds libdxmt_unix.a (patched DXMT unix objs + airconv)

# Combine DXMT objs + LLVM iOS static libs into one archive Xcode links.
xcrun -sdk iphoneos libtool -static -o libdxmt_combined.a \
    obj/*.o ../../toolchains/llvm-ios-build/lib/*.a
cp libdxmt_combined.a ../../app/Madeira/
```

## What the fork changes vs upstream

See `git log --oneline upstream/main..ios-port` on the `willfaust/dxmt`
repo. Summary: Cocoa→UIKit conditionals, NSScreen/ColorSync/CGDirectDisplay
stubs, bootstrap.h fallback, symbol rename to avoid collision with ntdll's
own `__wine_unix_call_funcs`, skip cross-process swapchain check, and the
`build-aarch64-win.txt` meson cross file.
