# Contributing

The code looks unusual in places because every byte of the 2048 byte boot section counts. These rules are what keeps it
working; the reasons are in [SIZE.md](SIZE.md).

## The five hard rules

1. **The image must fit in 2048 bytes and `main` must be at 0x7800.** `cmake --build` enforces both (`SIZE_CHECK`). Never
   put data in front of `main`: a `PROGMEM` table is linked before it and the reset vector would run the table.
2. **`src/ram.h` is the first include of every `.c` file.** It declares the global register variable `R` (Y register).
   gcc rejects such a declaration after a function definition, and it must be identical in every translation unit.
3. **No stack frame, no address-of-local.** `r28:r29` are reserved (`-ffixed-r28 -ffixed-r29`), so gcc cannot build a
   frame pointer. A local whose address is taken or an array local fails the build with "frame pointer required, but
   reserved". Put such state in `bl_ram_t` and access it through `R->`.
4. **No initialised and no zero-initialised globals.** All variables are `NOINIT` (`.noinit`), because startup code
   (`__do_copy_data`, `__do_clear_bss`) is not linked in. Write every variable before reading it.
5. **The radio behaviour must stay byte-identical.** `tests/sim/run.sh` compares the complete SPI transcript with
   `tests/sim/golden.txt`, which comes from the original bootloader. A size optimisation that changes it is a bug; a
   deliberate protocol change needs a re-recorded golden file and a CHANGELOG entry, see
   [golden.README.md](../tests/sim/golden.README.md).

## Compiler options that are part of the design

- **`-ffixed-r28 -ffixed-r29`** reserve Y for `R`. Every field access becomes a one-word `ldd`/`std`.
- **`-mstrict-X`** only lets gcc use the X register as a plain pointer (`ld`/`st`, post-increment, pre-decrement), never
  with an offset that has to be added and subtracted again. With Y gone, X and Z are the only pointer registers left;
  measured on this code the option makes the image smaller, so re-measure before dropping it.
- **`-flto`** lets gcc inline across the split translation units; it is worth about 120 bytes. It is the reason
  avr-gcc 14 does not work yet (`-DLTO=OFF` builds, 230 bytes larger).
- Do not "clean up" the other `-fno-*` flags without measuring: each is worth 2 to 26 bytes.

## Before sending a change

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/avr-gcc.cmake -DF_CPU=16000000L
cmake --build build                       # prints the flash use
tests/sim/run.sh build/MYSBootloader.*.elf 16000000
```

Repeat for 8000000 and 1000000 (CI does). Report the size change in the commit message. Measure with avr-gcc 7.3; other
versions give different numbers. The simulator only covers the default wiring (`SPI_PINS_CE9_CSN10`) and has never been
checked against real hardware, so say how a change was tested.
