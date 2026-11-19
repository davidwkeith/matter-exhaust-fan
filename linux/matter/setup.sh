#!/usr/bin/env bash
# One-time setup for building exhaustd-matter; run from anywhere. Then, in
# each shell that builds:
#   repo=$PWD; cd linux/matter/third_party/connectedhomeip && source scripts/activate.sh; cd "$repo"
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
chip="$here/third_party/connectedhomeip"

git -C "$here/../.." submodule update --init --depth 1 linux/matter/third_party/connectedhomeip
cd "$chip" # the SDK's scripts expect to run from its root
python3 scripts/checkout_submodules.py --shallow --platform linux

# See data-model/BUILD.gn: the SDK only builds data models from examples/.
ln -sfn "$here" examples/exhaust-fan
exclude="$(git rev-parse --git-path info/exclude)"
grep -qx '/examples/exhaust-fan' "$exclude" 2>/dev/null || echo '/examples/exhaust-fan' >>"$exclude"

bash -c 'source scripts/bootstrap.sh -p linux'
