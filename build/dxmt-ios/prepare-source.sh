#!/bin/bash
# Apply Madeira's GS fix to a copy; keep the DXMT pin and worktree intact.
set -eu
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
S="${1:?usage: prepare-source.sh destination}"
mkdir -p "$S"
rsync -a --delete --exclude='.git' --exclude='/build*/' "$R/dxmt/" "$S/"
patch -d "$S" -p1 < "$R/build/dxmt-ios/gs-input-siv.patch"
