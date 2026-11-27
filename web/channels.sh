#!/bin/sh
# Puts two browser builds into one site (the piccolo-web image that serves
# get-piccolo.app/play/): the latest release at the top, so its addresses
# stay (play/sameboy/), and main's build under dev/. channels.json at the top
# names both; the start pages read it and show RELEASE | DEV (web/page/index.html).
#
#   web/channels.sh <dev folder> <site folder> [<release zip>]
#
# <dev folder>: web/package.sh's folder (web/build/dist/piccolo-<version>-web).
# <release zip>: a release's piccolo-<version>-web.zip. Without one (no
# release yet) the dev build is the top and RELEASE is greyed out.

set -eu

if [ $# -lt 2 ]; then
    echo "usage: web/channels.sh <dev folder> <site folder> [<release zip>]" >&2
    exit 1
fi
dev=$1
site=$2
release_zip=${3:-}

# DEV's label: the commit web/package.sh wrote into the page.
commit=$(sed -n 's/.*id="channel" hidden>DEV \([0-9a-z]*\)<.*/\1/p' "$dev/index.html")

rm -rf "$site"
mkdir -p "$site"
if [ -n "$release_zip" ]; then
    unpacked=$(mktemp -d)
    unzip -q "$release_zip" -d "$unpacked"
    cp -r "$unpacked"/piccolo-*-web/. "$site/"
    rm -rf "$unpacked"
    cp -r "$dev" "$site/dev"
    version=$(basename "$release_zip" | sed 's/^piccolo-\(.*\)-web\.zip$/\1/')
    cat > "$site/channels.json" <<JSON
{"release": {"path": "./", "label": "$version"}, "dev": {"path": "dev/", "label": "$commit"}}
JSON
else
    cp -r "$dev/." "$site/"
    cat > "$site/channels.json" <<JSON
{"dev": {"path": "./", "label": "$commit"}}
JSON
fi
echo "web/channels.sh: $site ($(cat "$site/channels.json"))"
