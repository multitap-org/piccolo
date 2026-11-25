#!/bin/bash
# Builds piccolo on Windows with MSYS2 (UCRT64, or CLANGARM64 on ARM64), plus
# librashader, and deploys it to %USERPROFILE%\piccolo with every DLL it needs
# from MSYS2. The steps of docs/compilation.md ("Windows (MSYS2)") in one go.
# Run from the MSYS2 UCRT64 shell, anywhere in the checkout:
#
#   tools/build-windows.sh                  Release, deployed
#   tools/build-windows.sh --deps           first install the packages (pacman)
#   tools/build-windows.sh --debug          Debug with warnings as errors (build-debug/)
#   tools/build-windows.sh --test           also run the tests (ctest)
#   tools/build-windows.sh --no-librashader skip librashader (no Rust needed)
#
# Submodules not checked out yet are fetched (the ones of compilation.md step
# 2); the ones already there are left as they are.
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
install_deps=0
run_tests=0
with_librashader=1
for arg in "$@"; do
    case "$arg" in
        --deps) install_deps=1 ;;
        --debug) build_type=Debug ;;
        --test) run_tests=1 ;;
        --no-librashader) with_librashader=0 ;;
        -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-windows.sh: unknown option $arg (--help)" >&2; exit 1 ;;
    esac
done

case "${MSYSTEM:-}" in
    UCRT64|CLANGARM64) ;;
    *) echo "build-windows.sh: run it from the MSYS2 UCRT64 (or CLANGARM64) shell, not '${MSYSTEM:-none}'" >&2; exit 1 ;;
esac

# The packages, the same list as docs/compilation.md step 1, for this
# environment (MINGW_PACKAGE_PREFIX: mingw-w64-ucrt-x86_64 or
# mingw-w64-clang-aarch64). Only -S --needed: never -Sy (MSYS2 doesn't
# support partial upgrades), and not -Syu either (an update of MSYS2 itself
# closes the shell): that one is left to the user.
if [ "$install_deps" = 1 ]; then
    if [ "$MSYSTEM" = CLANGARM64 ]; then
        compiler=clang
    else
        compiler=gcc
    fi
    packages="git"
    for package in $compiler cmake ninja vulkan-devel openssl ntldd; do
        packages="$packages $MINGW_PACKAGE_PREFIX-$package"
    done
    if [ "$with_librashader" = 1 ]; then
        packages="$packages $MINGW_PACKAGE_PREFIX-rust"
    fi
    echo "== packages (pacman)"
    # shellcheck disable=SC2086 # packages is a list
    if ! pacman -S --needed --noconfirm $packages; then
        echo "build-windows.sh: pacman failed; update MSYS2 first with pacman -Syu (until nothing is left), then run this again" >&2
        exit 1
    fi
fi

for tool in git cmake ninja ntldd cygpath; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build-windows.sh: $tool not found (tools/build-windows.sh --deps, or docs/compilation.md step 1)" >&2
        exit 1
    fi
done

cd "$(dirname "$0")/.."
src_dir=$(pwd)

# The URLs the submodules are fetched from, back from .gitmodules: a checkout
# whose .git/config points them at local folders can't fetch the missing
# ones (git refuses the file transport for submodules). Checked-out
# submodules keep their commit and branch.
git submodule sync -q

# The submodules of compilation.md step 2, only the ones not checked out yet
# (no .git in their folder): one already there may be on a branch being
# worked on, and `git submodule update` would move it back to the pinned
# commit.
submodules="deps/SDL deps/imgui deps/libretro-common deps/libsamplerate
    deps/gamemode deps/miniz deps/7zip deps/enet deps/monocypher deps/qrcodegen deps/beacon
    deps/piccolo-shaders deps/piccolo-shaders-legacy
    deps/common/hosts-common deps/common/multitap-net deps/common/frontends-common
    deps/common/retroui deps/libretro/host-libretro deps/libretro/frontend-libretro
    deps/libdatachannel"
missing=""
for submodule in $submodules; do
    if [ ! -e "$submodule/.git" ]; then
        missing="$missing $submodule"
    fi
done
if [ -n "$missing" ]; then
    echo "== submodules:$missing"
    # shellcheck disable=SC2086 # missing is a list
    git submodule update --init --recursive $missing
fi
# Their own submodules (libdatachannel's plog, usrsctp, libjuice...): when a
# pin moves, the new commit's submodules start out not checked out. Only the
# ones missing ('-' in git submodule status); nobody works inside those.
for submodule in $submodules; do
    if [ -f "$submodule/.gitmodules" ] && git -C "$submodule" submodule status --recursive | grep -q '^-'; then
        echo "== submodules of $submodule"
        git -C "$submodule" submodule update --init --recursive
    fi
done
# Without --recursive: its own submodules are only its tests.
if [ "$with_librashader" = 1 ] && [ ! -e deps/librashader/.git ]; then
    echo "== submodule: deps/librashader"
    git submodule update --init deps/librashader
fi
# The Test libretro core's nested submodules have SSH URLs: over HTTPS, so no
# GitHub SSH key is needed.
if [ ! -e deps/test-libretro/.git ]; then
    echo "== submodule: deps/test-libretro"
    git -c url."https://github.com/".insteadOf="git@github.com:" \
        submodule update --init --recursive --checkout deps/test-libretro
fi

if [ "$with_librashader" = 1 ]; then
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
