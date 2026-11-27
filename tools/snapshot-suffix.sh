#!/bin/sh
# The version suffix of a build of main, which the releases server signs as a
# snapshot (release.yml): snapshot.<commit date, UTC, YYYYMMDD>.<commit's
# first 7>. The same in every job of a run. CI writes it into
# .multitap-version-suffix, which hosts-common's multitap_version reads.
set -eu
date=$(TZ=UTC git log -1 --format=%cd --date=format-local:%Y%m%d)
commit=$(git rev-parse HEAD | cut -c 1-7)
echo "snapshot.$date.$commit"
