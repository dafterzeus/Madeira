#!/bin/bash
# Stage the pinned FEX sources and apply Madeira's allocator patch. Used by
# both CPU modules because they allocate in the same iOS host address band.
set -eu
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
S="${1:?usage: prepare-source.sh destination}"
mkdir -p "$S"
# Exclude build trees (including the destination), and submodule gitfiles.
rsync -a --delete --exclude='.git' --exclude='/[Bb]uild*/' "$R/FEX/" "$S/"
patch -d "$S" -p1 < "$R/build/fex-arm64ec/small-spans.patch"
