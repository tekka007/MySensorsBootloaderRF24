#!/usr/bin/env bash
# Builds the default images that are attached to a release (channel 110, PA max, default wiring):
# 16 MHz, 8 MHz, 1 MHz and NRFduino (8 MHz, CE=D7, CSN=D10, LED on PB1). The file names match arduino/boards.txt.
#
#   tools/build_release.sh [OUTDIR]       (default: ./release)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-$root/release}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
mkdir -p "$out"

image() {	# image <name> <F_CPU> [cmake arguments...]
	local name="$1" f_cpu="$2"
	shift 2
	rm -rf "$build"/*
	cmake -S "$root" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$root/cmake/avr-gcc.cmake" -DF_CPU="$f_cpu" "$@" >/dev/null
	cmake --build "$build" 2>&1 | grep "flash use"
	cp "$build"/MYSBootloader.*.hex "$out/MYSBootloader_$name.hex"
}

image 16MHz 16000000L
image 8MHz 8000000L
image 1MHz 1000000L
image NRFduino_8MHz 8000000L -DSPI_PINS=SPI_PINS_CE7_CSN10 -DCMAKE_C_FLAGS=-DLED_PIN=PINB1
(cd "$out" && sha256sum MYSBootloader_*.hex > SHA256SUMS)
