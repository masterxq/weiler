#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# debian/snapshot.sh [SUITE]: put a changelog entry for a snapshot build of the committed HEAD on top of
# debian/changelog (used by the CI workflow before dpkg-buildpackage; changes the working tree). The version is
# <version of the top entry>+git<commit date UTC, YYYYMMDD.HHMMSS>.g<commit>, so every later commit builds a
# higher version. SUITE defaults to testing (the suite of the moblix APT repository). Prints version=<version>.
set -eu
cd "$(dirname "$0")/.."
suite=${1:-testing}
base=$(dpkg-parsechangelog -S Version)
case $base in *+git*) echo "snapshot.sh: debian/changelog has a snapshot entry already ($base)" >&2; exit 1 ;; esac
stamp=$(TZ=UTC git log -1 --date=format-local:%Y%m%d.%H%M%S --format=%cd HEAD)
rev=$(git rev-parse --short=7 HEAD)
version=$base+git$stamp.g$rev
maintainer=$(sed -n 's/^Maintainer: //p' debian/control)
{
	printf 'weiler (%s) %s; urgency=medium\n\n' "$version" "$suite"
	printf '  * Snapshot build of commit %s.\n\n' "$(git rev-parse HEAD)"
	printf ' -- %s  %s\n\n' "$maintainer" "$(git log -1 --format=%cD HEAD)"
	cat debian/changelog
} > debian/changelog.snapshot
mv debian/changelog.snapshot debian/changelog
echo "version=$version"
