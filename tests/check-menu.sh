#!/bin/sh
set -eu
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cc -O0 -Wall -Wextra "$ROOT/tests/menu_entries.c" -lm -lz -o "$WORK/menu-entries"
"$WORK/menu-entries" "$ROOT/tests/menu_entries.txt"
echo 'PASS: USB, Windows and Linux menu fields'
