#!/usr/bin/env bash
# Cross-compile exhaustd-matter for the Raspberry Pi 3 (arm64) on an x86-64
# Linux host. Run setup.sh once first; then, from anywhere:
#   linux/matter/build-arm64.sh              # → linux/matter/out/arm64/exhaustd-matter
#
# Same recipe as the SDK's own linux-arm64 builds: clang, target_cpu=arm64,
# and the Ubuntu 24.04 aarch64 sysroot the SDK's cross-compile image uses
# (fetched from CIPD, pinned below). The binary needs glibc >= 2.38 on the
# Pi, i.e. Raspberry Pi OS Trixie (64-bit), not Bookworm.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
chip="$here/third_party/connectedhomeip"

sysroot_pkg='experimental/matter/sysroot/ubuntu-24.04-aarch64 version:build-2025.07.10'
sysroot_dir="$here/out/sysroot"
sysroot="${SYSROOT_AARCH64:-$sysroot_dir/ubuntu-24.04-aarch64-sysroot}"

# The SDK's activate.sh must be sourced from its root, and isn't set -u clean.
cd "$chip"
set +u
# shellcheck disable=SC1091
source scripts/activate.sh >/dev/null
set -u

if [ ! -d "$sysroot" ]; then
    mkdir -p "$sysroot_dir"
    echo "$sysroot_pkg" >"$sysroot_dir/ensure.txt"
    cipd ensure -ensure-file "$sysroot_dir/ensure.txt" -root "$sysroot_dir"
fi

cd "$here"
export PKG_CONFIG_PATH="$sysroot/lib/aarch64-linux-gnu/pkgconfig"
gn gen out/arm64 --args="target_cpu=\"arm64\" is_clang=true sysroot=\"$sysroot\" ${GN_ARGS:-}"
ninja -C out/arm64 exhaustd-matter
