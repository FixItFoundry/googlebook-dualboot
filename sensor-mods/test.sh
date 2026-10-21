#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
sha256sum -c <<'SUM'
538c869adbe49b546041cc9e27b9ca898681dfa02c3f3728f87013554c99ce9c  firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin
SUM
KVER=${1:-$(uname -r)}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT
cp Makefile cros_ec.h cros_ec_ishtp.c "$BUILD/"
make -C "/lib/modules/$KVER/build" M="$BUILD" modules
[[ $(modinfo -F vermagic "$BUILD/cros_ec_ishtp.ko") == "$KVER "* ]]
[[ $(modinfo -F alias "$BUILD/cros_ec_ishtp.ko") == 'ishtp:{7B7154D0-56F4-4BDC-B0D8-9E7CDAE0D6A0}' ]]
echo 'PASS: firmware identity, bridge build, kernel version, ISHTP client alias'
