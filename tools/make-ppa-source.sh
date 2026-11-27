#!/bin/bash
# Makes the unsigned Ubuntu source packages of piccolo for the PPA, one per
# series, from a tarball of tools/make-source-tarball.sh and the debian/
# folder of this checkout (as it is on disk, uncommitted changes included).
# Needs dpkg-dev only (dpkg-buildpackage, dpkg-source); nothing is built.
#
#   tools/make-ppa-source.sh TARBALL [SERIES...]
#
# SERIES defaults to noble resolute stonking (24.04, 26.04, 26.10). The
# results go next to TARBALL: per series a .dsc, a .debian.tar.xz and a
# _source.changes, all sharing the one .orig.tar.xz. Signing and uploading are
# Andrés's (his GPG key, his Launchpad account):
#   debsign -k <KEY> <dir>/piccolo_<version>_source.changes
#   dput ppa:<launchpad user>/piccolo <dir>/piccolo_<version>_source.changes
#
# The version of each upload is <upstream>-<revision>~<series>1, e.g.
# 0.3~git20261122.7e0ec5f-1~noble1: "~series" sorts noble < resolute <
# stonking, so upgrading Ubuntu upgrades piccolo too.
#
# Environment, all optional:
#   PPA_REVISION  the Debian revision (default 1); raise it to upload the same
#                 tarball again with packaging changes
#   PPA_CHANGES   the changelog line of this upload (default "New upstream snapshot.")
#   DEBFULLNAME, DEBEMAIL  who signs (default the Maintainer of debian/control);
#                 must be a name and address of the GPG key Launchpad knows

set -eu

if [ $# -lt 1 ]; then
    echo "make-ppa-source.sh: usage: tools/make-ppa-source.sh TARBALL [SERIES...]" >&2
    exit 1
fi
tarball=$1
shift
series_list="$*"
if [ -z "$series_list" ]; then
    series_list="noble resolute stonking"
fi

for tool in dpkg-buildpackage dpkg-source tar; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "make-ppa-source.sh: $tool not found (apt install dpkg-dev)" >&2
        exit 1
    fi
done

case "$tarball" in
    */piccolo_*.orig.tar.xz) ;;
    piccolo_*.orig.tar.xz) tarball="./$tarball" ;;
    *)
        echo "make-ppa-source.sh: $tarball isn't a piccolo_<version>.orig.tar.xz (tools/make-source-tarball.sh)" >&2
        exit 1
        ;;
esac
if [ ! -f "$tarball" ]; then
    echo "make-ppa-source.sh: $tarball not found" >&2
    exit 1
fi
out_dir=$(cd "$(dirname "$tarball")" && pwd)
version=$(basename "$tarball")
version=${version#piccolo_}
version=${version%.orig.tar.xz}

cd "$(dirname "$0")/.."
src_dir=$(pwd)

revision=${PPA_REVISION:-1}
changes_line=${PPA_CHANGES:-New upstream snapshot.}
maintainer=$(sed -n 's/^Maintainer: //p' debian/control)
signer_name=${DEBFULLNAME:-${maintainer% <*}}
signer_email=${DEBEMAIL:-$(echo "$maintainer" | sed 's/.*<\(.*\)>/\1/')}
signer="$signer_name <$signer_email>"
date_rfc=$(date -R)

for series in $series_list; do
    deb_version="$version-$revision~${series}1"
    echo "== $series: piccolo $deb_version"
    tree="$out_dir/piccolo-$version"
    rm -rf "$tree"
    tar -C "$out_dir" -xf "$tarball"
    cp -r "$src_dir/debian" "$tree/"

    # 24.04's default Rust is 1.75; librashader's crates need 1.88. Its
    # versioned toolchain (in noble-updates) instead; debian/rules finds it.
    if [ "$series" = noble ]; then
        sed -i -e 's/^ cargo (>= 1.88),$/ cargo-1.91,/' -e 's/^ rustc (>= 1.88),$/ rustc-1.91,/' "$tree/debian/control"
        if ! grep -q '^ cargo-1.91,$' "$tree/debian/control"; then
            echo "make-ppa-source.sh: debian/control: the cargo line didn't change for noble" >&2
            exit 1
        fi
    fi

    # The changelog of an upload is its own entry only: the uploads are made
    # from git, whose history is the real changelog (debian/changelog in git
    # is a placeholder that dpkg tools can read).
    {
        echo "piccolo ($deb_version) $series; urgency=medium"
        echo ""
        echo "  * $changes_line"
        echo ""
        echo " -- $signer  $date_rfc"
    } > "$tree/debian/changelog"

    # -S source only, -d no build dependencies needed here, -nc don't run
    # debian/rules clean (nothing built), -sa the orig tarball goes in every
    # .changes (Launchpad takes it again when it's the same file), unsigned.
    (cd "$tree" && dpkg-buildpackage -S -d -nc -sa -us -uc > "$out_dir/piccolo_${deb_version}_source.log" 2>&1) || {
        echo "make-ppa-source.sh: dpkg-buildpackage failed, see $out_dir/piccolo_${deb_version}_source.log" >&2
        exit 1
    }
    rm -rf "$tree"
    echo "   $out_dir/piccolo_${deb_version}_source.changes"
done
