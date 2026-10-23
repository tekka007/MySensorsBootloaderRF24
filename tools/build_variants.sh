#!/usr/bin/env bash
# Builds bootloader images for every clock / channel / PA level combination into an output directory.
#
#   tools/build_variants.sh [OUTDIR]            all clocks, channels 1-110, all PA levels
#   CLOCKS="8000000L" CHANNELS="76 100" PA_LEVELS="RF24_PA_MAX" tools/build_variants.sh out
#   PINS=SPI_PINS_CE7_CSN8 tools/build_variants.sh out
#
# Images that do not fit the 2048 byte boot section are reported and skipped (the build itself fails on them).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-$root/out}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

read -ra clocks <<< "${CLOCKS:-16000000L 8000000L 1000000L}"
read -ra channels <<< "${CHANNELS:-$(seq -s ' ' 1 110)}"
read -ra levels <<< "${PA_LEVELS:-RF24_PA_MIN RF24_PA_LOW RF24_PA_HIGH RF24_PA_MAX}"
pins="${PINS:-SPI_PINS_CE9_CSN10}"

mkdir -p "$out"
failed=0
for clock in "${clocks[@]}"; do
	for channel in "${channels[@]}"; do
		for level in "${levels[@]}"; do
			echo "F_CPU=$clock channel=$channel PA=$level pins=$pins"
			cmake -S "$root" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$root/cmake/avr-gcc.cmake" -DCMAKE_BUILD_TYPE= \
				-DF_CPU="$clock" -DBAUD_RATE= -DCHANNEL="$channel" -DPA_LEVEL="$level" -DSPI_PINS="$pins" >/dev/null
			if cmake --build "$build" >"$build/log" 2>&1; then
				cp "$build"/MYSBootloader.*.hex "$out/"
			else
				echo "  FAILED:" >&2
				tail -n 5 "$build/log" >&2
				failed=$((failed + 1))
			fi
			rm -f "$build"/MYSBootloader.*
		done
	done
done
echo "images in $out, $failed failed"
[ "$failed" -eq 0 ]
