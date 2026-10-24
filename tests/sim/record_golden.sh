#!/usr/bin/env bash
# Re-records tests/sim/golden.txt from the ORIGINAL (pre-optimisation) bootloader sources in the git history.
#
#   tests/sim/record_golden.sh [--write]
#
# Prints the golden lines ("<F_CPU> <scenario> <sha256 of the radio transcript>"). Without --write they are compared with
# tests/sim/golden.txt, with --write the file is replaced. Needs the full git history, avr-gcc, simavr and libelf.
# See tests/sim/golden.README.md for what the reference is and when it may be re-recorded.
set -euo pipefail

original=651407a			# last commit with the unmodified code layout (MYSBootloader.c + headers)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git -C "$root" cat-file -e "$original^{commit}" 2>/dev/null || { echo "commit $original not in this clone (shallow?)"; exit 2; }
mkdir "$work/src"
git -C "$root" archive "$original" | tar -x -C "$work/src"
# the only deliberate difference to the original: the version number follows src/bootloader.h
minver="$(sed -n 's/^#define MYSBOOTLOADER_MINVER[[:space:]]*\([0-9]*\).*/\1/p' "$root/src/bootloader.h")"
sed -i "s/^#define MYSBOOTLOADER_MINVER .*/#define MYSBOOTLOADER_MINVER $minver/" "$work/src/Core.h"

gcc -O1 -I/usr/include/simavr "$here/harness.c" -o "$work/harness" -lsimavr -lelf

declare -A baud=([16000000]=115200 [8000000]=57600 [1000000]=9600)
: > "$work/golden.txt"
for f in 1000000 8000000 16000000; do
	# same flags as the original Makefile
	(cd "$work/src" && avr-gcc -funsigned-char -funsigned-bitfields -DF_CPU=${f}L -DBAUD_RATE=${baud[$f]} -Os \
		-ffunction-sections -fdata-sections -fpack-struct -fshort-enums -mrelax -mmcu=atmega328p -std=gnu99 \
		-c MYSBootloader.c -o MYSBootloader.o &&
	 avr-gcc -nostartfiles -Wl,-static -Wl,--gc-sections -mrelax -Wl,-section-start=.text=0x7800 -mmcu=atmega328p \
		-o orig.elf MYSBootloader.o -lm)
	for s in ota rerun zero; do
		FREQ=$f LIMIT_S=200 "$work/harness" "$work/src/orig.elf" $s "$work/$s" >/dev/null
		echo "$f $s $(sha256sum "$work/$s.log" | cut -d' ' -f1)" >> "$work/golden.txt"
	done
done

if [ "${1:-}" = "--write" ]; then
	cp "$work/golden.txt" "$here/golden.txt"; echo "wrote $here/golden.txt"
elif diff -u "$here/golden.txt" "$work/golden.txt"; then
	echo "golden.txt reproduced from $original (MINVER=$minver)"
else
	echo "golden.txt differs from the original bootloader's transcripts"; exit 1
fi
