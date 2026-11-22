#!/bin/bash
# Builds piccolo on Windows with MSYS2 (UCRT64, or CLANGARM64 on ARM64), plus
# librashader, and deploys it to %USERPROFILE%\piccolo with every DLL it needs
# from MSYS2. The steps of docs/compilation.md ("Windows (MSYS2)") in one go.
# Run from the MSYS2 UCRT64 shell, anywhere in the checkout:
#
#   tools/build-windows.sh                  Release, deployed
#   tools/build-windows.sh --debug          Debug with warnings as errors (build-debug/)
#   tools/build-windows.sh --test           also run the tests (ctest)
#   tools/build-windows.sh --no-librashader skip librashader (no Rust needed)
#
# Environment, all optional:
#   PICCOLO_DIR  where to deploy (default $USERPROFILE/piccolo)
#   BUILD_DIR    CMake build folder (default build, or build-debug with --debug)
#
# The build copies piccolo.exe, lang/, layouts/, themes/, shaders/ and
# cores/test_libretro.dll to PICCOLO_DIR (also from a git worktree: running
# this script is asking for it). Saves, states, settings and cores already
# there are left alone. Then librashader.dll (MSYS2's Rust, the GNU target)
# goes next to it, and every DLL that ntldd resolves to MSYS2's bin folder
# (OpenSSL for piccolo.exe, the GCC runtime for librashader.dll) is copied
# too. vulkan-1.dll is not: the GPU driver's loader must win, and Windows
# looks in piccolo's folder first.

set -eu

build_type=Release
run_tests=0
with_librashader=1
for arg in "$@"; do
    case "$arg" in
        --debug) build_type=Debug ;;
        --test) run_tests=1 ;;
        --no-librashader) with_librashader=0 ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-windows.sh: unknown option $arg (--help)" >&2; exit 1 ;;
    esac
done

case "${MSYSTEM:-}" in
    UCRT64|CLANGARM64) ;;
    *) echo "build-windows.sh: run it from the MSYS2 UCRT64 (or CLANGARM64) shell, not '${MSYSTEM:-none}'" >&2; exit 1 ;;
esac

for tool in cmake ninja ntldd cygpath; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build-windows.sh: $tool not found (packages: docs/compilation.md, step 1)" >&2
        exit 1
    fi
done

cd "$(dirname "$0")/.."
src_dir=$(pwd)

if [ ! -f deps/SDL/CMakeLists.txt ]; then
    echo "build-windows.sh: submodules missing (docs/compilation.md, step 2)" >&2
    exit 1
fi
if [ "$with_librashader" = 1 ]; then
    if [ ! -f deps/librashader/Cargo.toml ]; then
        echo "build-windows.sh: deps/librashader missing: git submodule update --init deps/librashader (or --no-librashader)" >&2
        exit 1
    fi
    if ! command -v cargo >/dev/null 2>&1; then
        echo "build-windows.sh: cargo not found: pacman -S --needed \${MINGW_PACKAGE_PREFIX}-rust (or --no-librashader)" >&2
        exit 1
    fi
fi

if [ -z "${PICCOLO_DIR:-}" ] && [ -z "${USERPROFILE:-}" ]; then
    echo "build-windows.sh: USERPROFILE isn't set: set PICCOLO_DIR to the folder to deploy to" >&2
    exit 1
fi
piccolo_dir=${PICCOLO_DIR:-${USERPROFILE:-}/piccolo}
piccolo_dir=$(cygpath -u "$piccolo_dir")
if [ "$build_type" = Debug ]; then
    build_dir=${BUILD_DIR:-build-debug}
    warnings_as_errors=ON
else
    build_dir=${BUILD_DIR:-build}
    # GCC -O2 gives two harmless warnings.
    warnings_as_errors=OFF
fi
# The static C/C++ runtime (CI passes the same flags on both environments).
static_flags="-static -static-libgcc -static-libstdc++"

echo "== piccolo: $build_type in $build_dir, deployed to $piccolo_dir"
cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DMULTITAP_WARNINGS_AS_ERRORS="$warnings_as_errors" \
    -DMULTITAP_PICCOLO_DIR="$(cygpath -m "$piccolo_dir")" \
    -DMULTITAP_COPY_FROM_WORKTREE=ON \
    -DCMAKE_EXE_LINKER_FLAGS="$static_flags" \
    -DCMAKE_SHARED_LINKER_FLAGS="$static_flags" \
    -DCMAKE_MODULE_LINKER_FLAGS="$static_flags"
cmake --build "$build_dir"

if [ "$run_tests" = 1 ]; then
    echo "== tests"
    ctest --test-dir "$build_dir" --output-on-failure
fi

if [ "$with_librashader" = 1 ]; then
    echo "== librashader (the first build takes a few minutes)"
    RUSTFLAGS="-C target-feature=+crt-static" CARGO_TARGET_DIR="$src_dir/build-librashader" \
        cargo build --manifest-path deps/librashader/Cargo.toml -p librashader-capi \
        --release --locked --no-default-features --features runtime-vulkan
    cp -u build-librashader/release/librashader_capi.dll "$piccolo_dir/librashader.dll"
    cp -u deps/librashader/LICENSE.md "$piccolo_dir/librashader-LICENSE.md"
fi

# Every DLL resolved to MSYS2's bin folder, three passes so the DLLs just
# copied are followed too.
echo "== MSYS2 DLLs"
msys_bin=$(cygpath -w "$MSYSTEM_PREFIX/bin")
cd "$piccolo_dir"
binaries=piccolo.exe
if [ -f librashader.dll ]; then
    binaries="$binaries librashader.dll"
fi
for pass in 1 2 3; do
    # shellcheck disable=SC2086 # binaries is a list
    ntldd -R $binaries | grep -iF "$msys_bin" | grep -iv 'vulkan-1\.dll' |
        sed -n 's/.*=> \(.*\) (0x[0-9a-fA-F]*)$/\1/p' |
        while IFS= read -r dll; do
            cp -vu "$(cygpath -u "$dll")" .
        done
done

# Run from piccolo's folder its own DLLs win: anything still resolved to
# MSYS2's bin folder is missing.
# shellcheck disable=SC2086
missing=$(ntldd -R $binaries | grep -iF "$msys_bin" | grep -iv 'vulkan-1\.dll' || true)
if [ -n "$missing" ]; then
    echo "build-windows.sh: still loaded from MSYS2, not copied:" >&2
    echo "$missing" >&2
    exit 1
fi

echo "== done: $(cygpath -w "$piccolo_dir")\\piccolo.exe"
