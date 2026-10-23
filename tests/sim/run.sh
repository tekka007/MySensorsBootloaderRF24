#!/usr/bin/env bash
# Runs the simavr regression scenarios against a bootloader ELF.
#
#   tests/sim/run.sh <bootloader.elf> <F_CPU in Hz>
#
# Needs libsimavr-dev and libelf-dev. Exits non-zero if any expectation fails.
set -uo pipefail

elf="${1:?usage: run.sh <bootloader.elf> <F_CPU in Hz>}"
freq="${2:?usage: run.sh <bootloader.elf> <F_CPU in Hz>}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

gcc -O1 -Wall -I/usr/include/simavr "$here/harness.c" -o "$work/harness" -lsimavr -lelf || exit 2

failed=0
check() {	# check <scenario> <description> <grep pattern on the output>
	if grep -Eq "$3" "$work/$1.out"; then
		echo "  ok   $1: $2"
	else
		echo "  FAIL $1: $2   (expected /$3/)"
		failed=1
	fi
}
run() {		# run <scenario> <simulated seconds>
	FREQ="$freq" LIMIT_S="$2" "$work/harness" "$elf" "$1" "$work/$1" >"$work/$1.out" 2>&1
}

golden() {	# golden <scenario>: the complete SPI transcript must be identical to the one of the original bootloader
	local expected actual
	expected="$(awk -v f="$freq" -v s="$1" '$1 == f && $2 == s { print $3 }' "$here/golden.txt")"
	[ -n "$expected" ] || return 0
	actual="$(sha256sum "$work/$1.log" | cut -d' ' -f1)"
	if [ "$expected" = "$actual" ]; then
		echo "  ok   $1: radio transcript identical to the original bootloader"
	else
		echo "  FAIL $1: radio transcript differs from the original bootloader"
		failed=1
	fi
}

fwcfg_ok='fwcfg=01 00 02 00 40 00 fe 1a'

echo "OTA update of a fresh node"
run ota 200
check ota "application started" 'started=1'
check ota "all 64 blocks requested" 'blocks_requested=64'
check ota "downloaded image is in flash" 'flash_crc_1k=1afe image_crc=1afe'
check ota "firmware config saved" "$fwcfg_ok"
check ota "node id from controller stored" 'eeprom\[0\.\.2\]=05 '
check ota "reset flags handed over in r2" 'r2=01'
golden ota

echo "node already up to date"
run rerun 30
check rerun "application started" 'started=1'
check rerun "no block requested" 'blocks_requested=0 '
check rerun "firmware config unchanged" "$fwcfg_ok"
golden rerun

echo "controller announces a CRC the firmware does not match"
run badcrc 20
check badcrc "application is not started" 'started=0'

echo "controller announces blocks == 0"
run zero 30
check zero "no block requested" 'blocks_requested=0 '
golden zero

echo "bootloader commands"
run cmd_id 10
check cmd_id "node id set to 42" 'eeprom\[0\.\.2\]=2a '
run cmd_clear 10
check cmd_clear "EEPROM cleared" 'ee_ff_count=10(23|24)'

echo "serial STK500 session"
run stk 10
check stk "sync, version, signature, page read back" 'uart: 14 10 14 03 10 14 65 10 .* 14 1e 95 0f 10 .* 01 08 0f 16 1d 24 2b 32 39 40 47 4e 55 5c 63 6a 10'
check stk "firmware config saved (16 blocks, CRC 469a)" 'fwcfg=ff ff ff ff 10 00 9a 46'

[ "$failed" -eq 0 ] && echo "all simulator checks passed" || echo "SIMULATOR CHECKS FAILED"
exit "$failed"
