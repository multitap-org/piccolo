#!/bin/bash
# Makes a source tarball of piccolo that builds offline: the committed state
# of this checkout (HEAD), every submodule at the commit HEAD pins, and the
# Rust crates librashader needs (cargo vendor). Launchpad (the PPA) builds
# without network; so does Flathub. Needs the network itself (submodules,
# crates), git, cargo and tar.
#
#   tools/make-source-tarball.sh [OUT_DIR]
#
# Writes OUT_DIR/piccolo_<version>.orig.tar.xz (default OUT_DIR: build-source)
# and prints its path on the last line. <version> is:
#   X.Y.Z                     HEAD is exactly the tag vX.Y.Z
#   <base>~git<date>.<commit> anything else (<base>: PICCOLO_VERSION_BASE in
#                             CMakeLists.txt, <date>: the commit's date)
# "~" sorts before anything, so 0.3~git20261122.7e0ec5f < 0.3 < 0.3.1 for apt.
#
# Uncommitted changes are not in the tarball (a warning says so). Submodules
# not checked out yet are fetched first, the same ones as build-linux.sh plus
# librashader and Test libretro; one already there is left where it is: its
# pinned commit is what goes in (it has to be in its history).
#
# The crates go to vendor/librashader/ in the tarball, only the ones that
# librashader's C API with the Vulkan runtime needs on Linux; every other
# crate of Cargo.lock (Windows, Metal, wgpu, the CLI) stays as an empty stub
# (its Cargo.toml only), so the lock file still matches and the tarball is
# ~25 MB smaller instead of ~900 MB bigger. debian/rules shows how cargo is
# pointed at them (--offline --locked plus two --config options).

set -eu

if [ $# -gt 1 ]; then
    echo "make-source-tarball.sh: usage: tools/make-source-tarball.sh [OUT_DIR]" >&2
    exit 1
fi

for tool in git cargo tar xz; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "make-source-tarball.sh: $tool not found" >&2
        exit 1
    fi
done

cd "$(dirname "$0")/.."
src_dir=$(pwd)
out_dir=${1:-build-source}
case "$out_dir" in
    /*) ;;
    *) out_dir="$src_dir/$out_dir" ;;
esac

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "make-source-tarball.sh: warning: uncommitted changes are not in the tarball (it is HEAD)" >&2
fi

# The version.
version_base=$(sed -n 's/^set(PICCOLO_VERSION_BASE \([0-9.]*\))$/\1/p' CMakeLists.txt)
if [ -z "$version_base" ]; then
    echo "make-source-tarball.sh: PICCOLO_VERSION_BASE not found in CMakeLists.txt" >&2
    exit 1
fi
tag=$(git describe --tags --exact-match HEAD 2>/dev/null || true)
case "$tag" in
    v[0-9]*)
        version=${tag#v}
        ;;
    *)
        commit_date=$(git log -1 --format=%cd --date=format:%Y%m%d HEAD)
        commit_short=$(git rev-parse --short=7 HEAD)
        version="$version_base~git$commit_date.$commit_short"
        ;;
esac
# Every file in the tarball gets the commit's time: the same HEAD gives the
# same tarball (Launchpad refuses a second, different orig tarball with the
# same version).
commit_time=$(git log -1 --format=%ct HEAD)

# The submodules the build needs, fetched when missing (see build-linux.sh).
submodules="deps/SDL deps/imgui deps/libretro-common deps/libsamplerate
    deps/gamemode deps/miniz deps/7zip deps/enet deps/monocypher deps/qrcodegen deps/beacon
    deps/piccolo-shaders deps/piccolo-shaders-legacy
    deps/common/hosts-common deps/common/frontends-common
    deps/common/retroui deps/libretro/host-libretro deps/libretro/frontend-libretro
    deps/libdatachannel"
for submodule in $submodules; do
    if [ ! -e "$submodule/.git" ]; then
        echo "== submodule: $submodule"
        git submodule update --init --recursive "$submodule"
    fi
done
# Without --recursive: librashader's own submodules are only its tests.
if [ ! -e deps/librashader/.git ]; then
    echo "== submodule: deps/librashader"
    git submodule update --init deps/librashader
fi
# Test libretro's nested submodules have SSH URLs: over HTTPS instead.
if [ ! -e deps/test-libretro/.git ]; then
    echo "== submodule: deps/test-libretro"
    git -c url."https://github.com/".insteadOf="git@github.com:" \
        submodule update --init --recursive --checkout deps/test-libretro
fi

name="piccolo-$version"
work_dir="$out_dir/work"
stage_dir="$work_dir/$name"
rm -rf "$work_dir"
mkdir -p "$stage_dir"

# HEAD of piccolo itself, then every checked-out submodule (nested ones too)
# at the commit its parent pins. deps/cores is never in a package (cores are
# downloaded); a submodule checked out by hand that isn't in the lists above
# goes in too, harmless.
echo "== piccolo $version: git archive"
git archive --format=tar HEAD | tar -x -C "$stage_dir"
# The packaging comes with each upload (tools/make-ppa-source.sh), never
# inside the upstream tarball.
rm -rf "$stage_dir/debian"
# shellcheck disable=SC2016 # $displaypath and $sha1 are git's, expanded by foreach
git submodule foreach --quiet --recursive 'echo "$displaypath $sha1"' > "$work_dir/submodules.txt"
while read -r path commit; do
    case "$path" in
        deps/cores|deps/cores/*) continue ;;
        deps/librashader/*) continue ;;
    esac
    echo "== submodule archive: $path"
    if ! git -C "$path" cat-file -e "$commit^{commit}" 2>/dev/null; then
        echo "make-source-tarball.sh: $path doesn't have commit $commit (git -C $path fetch)" >&2
        exit 1
    fi
    mkdir -p "$stage_dir/$path"
    git -C "$path" archive --format=tar "$commit" | tar -x -C "$stage_dir/$path"
done < "$work_dir/submodules.txt"

# The crates. cargo vendor copies every crate of Cargo.lock; cargo tree lists
# the ones a Linux build of the C API with the Vulkan runtime uses, for every
# Linux architecture Launchpad builds (a crate can be per architecture).
echo "== librashader: cargo vendor"
vendor_dir="$stage_dir/vendor/librashader"
mkdir -p "$stage_dir/vendor"
(cd "$stage_dir/deps/librashader" &&
    cargo vendor --locked --versioned-dirs --quiet "$vendor_dir" > /dev/null)
used_list="$work_dir/crates-used.txt"
: > "$used_list"
for target in x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu armv7-unknown-linux-gnueabihf \
    riscv64gc-unknown-linux-gnu powerpc64le-unknown-linux-gnu s390x-unknown-linux-gnu; do
    (cd "$stage_dir/deps/librashader" &&
        cargo tree --locked --quiet -p librashader-capi --no-default-features --features runtime-vulkan \
            --target "$target" -e normal,build --prefix none --format '{p}') >> "$used_list"
done
# "name vX.Y.Z" (path crates have " (/path)" after it, "(*)" marks a repeat)
# becomes the folder name "name-X.Y.Z".
sed -e 's/ (.*//' -e 's/ v/-/' "$used_list" | sort -u > "$used_list.dirs"
crates_kept=0
crates_stubbed=0
for crate_dir in "$vendor_dir"/*/; do
    crate_dir=${crate_dir%/}
    crate=$(basename "$crate_dir")
    if grep -qxF "$crate" "$used_list.dirs"; then
        crates_kept=$((crates_kept + 1))
        continue
    fi
    # A stub: the same Cargo.toml (the lock file names its dependencies), an
    # empty lib.rs, no files to check. The package checksum stays: cargo
    # compares it with Cargo.lock.
    package_sum=$(grep -o '"package":"[0-9a-f]*"' "$crate_dir/.cargo-checksum.json" || true)
    find "$crate_dir" -mindepth 1 -maxdepth 1 ! -name Cargo.toml -exec rm -rf {} +
    mkdir -p "$crate_dir/src"
    : > "$crate_dir/src/lib.rs"
    printf '{"files":{},%s}\n' "${package_sum:-\"package\":null}" > "$crate_dir/.cargo-checksum.json"
    crates_stubbed=$((crates_stubbed + 1))
done
echo "   $crates_kept crates, $crates_stubbed stubs"
cat > "$stage_dir/vendor/README.md" <<'EOF'
# vendor/

Added by tools/make-source-tarball.sh; not in git. `librashader/` holds the
Rust crates of deps/librashader/Cargo.lock that a Linux build of librashader's
C API with the Vulkan runtime needs, from `cargo vendor`. The others are
stubs (Cargo.toml only), so the lock file still matches. Each crate keeps its
own license files. To build with them:

    cargo build --offline --locked \
        --config 'source.crates-io.replace-with="vendored-sources"' \
        --config 'source.vendored-sources.directory="<absolute path>/vendor/librashader"' ...
EOF

echo "== tarball"
tarball="$out_dir/piccolo_$version.orig.tar.xz"
tar -C "$work_dir" --sort=name --mtime="@$commit_time" --owner=0 --group=0 --numeric-owner \
    -cf - "$name" | xz -T0 -6 > "$tarball"
rm -rf "$work_dir"
echo "   $(du -h "$tarball" | cut -f1)"
echo "$tarball"
