#!/bin/bash
# Builds piccolo on Linux (Debian / Ubuntu), plus librashader, and installs it
# to ~/.local/share/piccolo. The steps of docs/compilation.md ("Linux
# (Debian / Ubuntu)") in one go. Run it anywhere in the checkout:
#
#   tools/build-linux.sh                  Release, installed
#   tools/build-linux.sh --deps           first install the packages (apt, sudo)
#   tools/build-linux.sh --debug          Debug with warnings as errors (build-debug/)
#   tools/build-linux.sh --test           also run the tests (ctest)
#   tools/build-linux.sh --no-librashader skip librashader (no Rust needed)
#
# Environment, all optional:
#   PICCOLO_DIR  where to install (default $XDG_DATA_HOME/piccolo, that is
#                ~/.local/share/piccolo: piccolo's own data folder)
#   BUILD_DIR    CMake build folder (default build, or build-debug with --debug)
#
# Submodules not checked out yet are fetched (the ones of compilation.md step
# 2); the ones already there are left as they are. The build copies piccolo,
# lang/, layouts/, themes/, shaders/ and cores/test_libretro.so to
# PICCOLO_DIR (also from a git worktree: running this script is asking for
# it). Saves, states, settings and cores already there are left alone.
# librashader.so goes next to it. No menu entry: that's for the packages.

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
        -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-linux.sh: unknown option $arg (--help)" >&2; exit 1 ;;
    esac
done

# The packages, the same list as docs/compilation.md step 1. SDL is built from
# deps/SDL: its X11, Wayland, audio and input backends need their headers.
if [ "$install_deps" = 1 ]; then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "build-linux.sh: --deps needs apt-get (Debian / Ubuntu); install the packages of docs/compilation.md by hand" >&2
        exit 1
    fi
    packages="git build-essential pkg-config cmake ninja-build
        libvulkan-dev libssl-dev
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev
        libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev
        libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev
        libasound2-dev libpulse-dev libpipewire-0.3-dev
        libdbus-1-dev libudev-dev libibus-1.0-dev"
    if [ "$with_librashader" = 1 ]; then
        packages="$packages cargo"
    fi
    sudo_cmd=""
    if [ "$(id -u)" != 0 ]; then
        sudo_cmd=sudo
    fi
    echo "== packages (apt)"
    $sudo_cmd apt-get update
    # shellcheck disable=SC2086 # packages is a list
    $sudo_cmd apt-get install -y --no-install-recommends $packages
fi

for tool in git cmake ninja cc c++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build-linux.sh: $tool not found (tools/build-linux.sh --deps, or docs/compilation.md step 1)" >&2
        exit 1
    fi
done

cd "$(dirname "$0")/.."
src_dir=$(pwd)

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
        echo "build-linux.sh: cargo not found: tools/build-linux.sh --deps, or rustup (or --no-librashader)" >&2
        exit 1
    fi
fi

data_home=${XDG_DATA_HOME:-$HOME/.local/share}
piccolo_dir=${PICCOLO_DIR:-$data_home/piccolo}
case "$piccolo_dir" in
    /*) ;;
    *) piccolo_dir="$src_dir/$piccolo_dir" ;;
esac
if [ "$build_type" = Debug ]; then
    build_dir=${BUILD_DIR:-build-debug}
    warnings_as_errors=ON
else
    build_dir=${BUILD_DIR:-build}
    # GCC -O2 gives two harmless warnings.
    warnings_as_errors=OFF
fi

echo "== piccolo: $build_type in $build_dir, installed to $piccolo_dir"
cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DMULTITAP_WARNINGS_AS_ERRORS="$warnings_as_errors" \
    -DMULTITAP_PICCOLO_DIR="$piccolo_dir" \
    -DMULTITAP_COPY_FROM_WORKTREE=ON
cmake --build "$build_dir"

if [ "$run_tests" = 1 ]; then
    echo "== tests"
    ctest --test-dir "$build_dir" --output-on-failure
fi

if [ "$with_librashader" = 1 ]; then
    echo "== librashader (the first build takes a few minutes)"
    if ! CARGO_TARGET_DIR="$src_dir/build-librashader" \
        cargo build --manifest-path deps/librashader/Cargo.toml -p librashader-capi \
        --release --locked --no-default-features --features runtime-vulkan; then
        echo "build-linux.sh: librashader didn't build; an old distribution's Rust can be too old:" >&2
        echo "  install a current one with rustup (https://rustup.rs), or use --no-librashader" >&2
        exit 1
    fi
    install -m755 build-librashader/release/liblibrashader_capi.so "$piccolo_dir/librashader.so"
    install -m644 deps/librashader/LICENSE.md "$piccolo_dir/librashader-LICENSE.md"
fi

echo "== done: $piccolo_dir/piccolo"
