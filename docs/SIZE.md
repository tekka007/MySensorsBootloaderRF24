# Image size

The bootloader has to fit in the 2048 byte boot section of the ATmega328p. The original code needed 2000 to 2034 bytes,
this version needs 1664 to 1740 bytes depending on the configuration (1728 for the 16 MHz default, 1732 at 8 MHz, 1716 at
1 MHz). Check the `bootloader flash use: N of 2048 bytes` line of `cmake --build` after every change. The build also fails
if `main` is not the first code in the boot section (the reset vector jumps there), which happens if data such as a
`PROGMEM` table is placed in front of it.

## Toolchain

The sizes are for avr-gcc 7.3. Other versions were measured on the same sources:

| Compiler | Size at 16 MHz | Remark |
|----------|----------------|--------|
| 7.3 (reference) | 1728 B | with `-flto` |
| 5.4, 4.9 | about +30 B | |
| 4.8 | much larger | |
| 14.x with `-flto` | does not link | "global register variable follows a function definition" |
| 14.x with `-DLTO=OFF` | 1958 B (+230 B) | fits, and passes all simulator checks including the radio transcripts |
| 7.3 with `-DLTO=OFF` | 1848 B (+120 B) | LTO is worth 120 B |

A single translation unit with gcc 14 is about 100 B larger than gcc 7.3 built the same way, so most of the gap is code
generation, not LTO. The `newer-toolchain` CI job reports the current delta for every push.

## Techniques

Most of these are specific to the AVR and to this code, so look at the commit history before undoing one:

- **No startup code.** Every variable lives in `.noinit` (or in the one RAM struct), so `__do_clear_bss` and
  `__do_copy_data` are not linked in. Each variable is written before it is read. A `PROGMEM` table is not an option
  because the linker puts it in front of `main`.
- **One RAM struct behind a reserved pointer.** `src/ram.h` keeps the node config, the firmware config, the transmit
  buffer and a few scalars in a struct that the Y register points to for the whole run (`-ffixed-r28 -ffixed-r29`). The
  AVR reaches `Y+0..63` with one-word `ldd`/`std`, an absolute address needs the two-word `lds`/`sts`. As a consequence
  there is no stack frame: nothing may take the address of a local variable.
- **Assembly where gcc is clearly worse.** The EEPROM routines (`eeprom_put`, `eeprom_put_block`, `eeprom_get_block`) and
  `flash_page_program`, which shares one command-and-wait tail for erase, write and RWW enable, and the CRC bit loop
  are written in assembly; gcc needed about twice the bytes.
- **Never-returning functions.** The serial loader continues into the MySensors loader instead of returning, which
  removes its register save and restore code.
- **Cheap constants and types.** `LED_TOGGLE` writes to PINx (one instruction), `jmp 0` starts the application, the
  length comparison in `process_rx` stays 8 bit, `STK_GET_PARAMETER` assigns a reply variable instead of a folded
  ternary (2 bytes smaller).
- **Own small routines instead of libc.** Buffer compare, EEPROM access and CRC. `memcpy` is cheaper than a
  hand-written copy loop because gcc turns such a loop into a `memcpy` call.
- **Compiler options.** `-flto -Os -mrelax -fno-gcse -fno-move-loop-invariants -fno-tree-fre -fno-reorder-blocks
  -fno-tree-dominator-opts -fira-algorithm=priority -mstrict-X`. Each was measured on this code and removes between 2
  and 26 bytes. Three helpers are marked `NOINLINE` because inlining them into every caller is bigger than a call.

## What did not help

Measured and rejected, so nobody has to try them again (16 MHz, avr-gcc 7.3):

| Experiment | Result |
|------------|--------|
| `goto` instead of the state `switch` | +12 B |
| table-driven radio init | +4 B |
| libc's CRC helper | +12 B |
| a `copy()` helper (gcc turns it into `memcpy`) | +12 B |
| a CSN settle helper | +32 B |
| `requested_block` as a local instead of a struct field | +60 B |
| counter-based LED blink instead of `_delay_ms` | no gain |
| `-mcall-prologues` | +110 B |
| `-mbranch-cost=1` / `2` / `3` (`0` is the default) | +8 / +58 / +58 B |
| `-mno-interrupts`, `-maccumulate-args`, `-fno-split-wide-types` | no change (no ISRs, no stack arguments) |
| loader state in registers instead of the RAM struct | larger |
| avr-gcc 14 with `-fno-split-wide-types` | -12 B, still about 100 B above 7.3 |

`--relax` does reach the linker: `-mrelax` on the link line expands to `--relax`.
