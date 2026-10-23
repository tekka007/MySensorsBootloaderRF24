# Changelog

## Unreleased

- Sources moved to `src/` and split into `.c`/`.h` units (`hw`, `rf24`, `stk500_loader`, `mys_loader`); unused MySensors
  sensor/data enums and the shadowed `string.h` removed, state names and globals cleaned up.
- Build moved from the hand-edited `Makefile` to CMake. All settings (clock, baud rate, channel, PA level, data rate,
  wiring, options) are cache variables, `src/config.h` holds the defaults. The build fails if the image exceeds the
  2048 byte boot section. `tools/build_variants.sh` builds every channel/power/clock combination.
- Fixed `multicompile.sh` never changing the PA level, so every `compiled/*` MIN/LOW/HIGH image was a MAX image.
  The `compiled/` directory is gone; build the variant you need or let CI build all of them.
- A firmware config with `blocks == 0` is no longer accepted as an update (it requested block `0xFFFF`).
- Image size reduced from 2000-2034 to 1654-1730 bytes (the default configurations dropped by about 300 bytes, 15%):
  no `.data`/`.bss` startup code, all RAM in one struct addressed through a reserved Y register, EEPROM routines and the
  SPM sequence in assembly, a serial loader that continues instead of returning, own small routines instead of libc,
  and tuned compiler options. The build now also verifies that `main` sits at the start of the boot section. See
  README for the full list.
- The CRC stored after a serial (STK500) upload now covers whole firmware blocks, like the CRC checked at every start.
  Before, an upload whose size was not a multiple of 16 bytes stored a CRC the bootloader could never reproduce.
- Added `tests/sim`, simavr regression tests (OTA update, bad CRC, bootloader commands, serial upload) that also compare
  the radio traffic byte for byte with the original bootloader.
- CI builds the default images for all supported clocks and wirings and runs the simulator tests.

## v1.3.0-rc.1

- Add message length verification
- Stripped away RF24 communication driver redundancies and generics
- General maintenance, .hex update

## v1.3.0-beta.4

- Code optimization => reintroduction of bootloader commands: Akubi

## v1.3.0-beta.3

- Optimization / Refactorings

## v1.3pre

- redesign update process
- implement STK500 protocol / serial upload
- preferred parent with fallback option
- save MCUSR, can be retrieved by application

## v1.2

- internal version, PoC

## v1.1

- use eeprom_update instead of eeprom_write to reduce wear out
- bootloader commands: erase eeprom, set node id
- verify incoming FW blocks for type & address
- communicate over static parent (if set and found) else broadcast to find nearest node
- adjusted timings

## v1.0

- initial release
