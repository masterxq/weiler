#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Renders the PNG icons from the two SVG sources (needs rsvg-convert, Debian package librsvg2-bin):
# 16 to 48 px from the simplified small icon, 64 px and up from the full icon.
set -eu
cd "$(dirname "$0")"
id=io.github.masterxq.Weiler
for s in 16 22 24 32 48; do mkdir -p $s; rsvg-convert -w $s -h $s -o $s/$id.png $id-small.svg; done
for s in 64 128 256 512; do mkdir -p $s; rsvg-convert -w $s -h $s -o $s/$id.png $id.svg; done
