# Golden radio transcripts

`golden.txt` holds one line per clock and scenario: `<F_CPU> <scenario> <sha256>`. The hash is taken over the `.log` file
the harness writes: every SPI command and data byte the bootloader exchanges with the emulated nRF24, in order. If the
hash matches, the optimised bootloader talks to the radio exactly like the reference, down to register writes and retries.

## Provenance

The reference is the **original, unoptimised bootloader**: the sources of commit `651407a` (`MYSBootloader.c` and its
headers, before the repository was reorganised), compiled with the flags of its own `Makefile` for 16 MHz, 8 MHz and
1 MHz. The only change is `MYSBOOTLOADER_MINVER`, which is raised to the current version (4) because the bootloader
reports it to the controller; with the older value a single byte of the firmware config request would differ.

Scenarios covered: `ota` (full 64 block update), `rerun` (already up to date) and `zero` (controller announces zero
blocks). `badcrc`, `cmd_*` and `stk` are checked by pattern in `run.sh` instead. The `rerun` and `zero` transcripts are
identical at all three clocks, the OTA transcript is the same at 8 and 16 MHz and differs at 1 MHz.

## Reproducing and re-recording

```sh
tests/sim/record_golden.sh            # rebuild the original from git, compare with golden.txt
tests/sim/record_golden.sh --write    # replace golden.txt
```

This needs the full git history (not a shallow clone), `avr-gcc`, `simavr` and `libelf`. Verified to reproduce the committed file.

Re-record only when the **protocol itself** changes on purpose (version number, message layout, timeouts), never to make a
failing optimisation pass. Say so in the CHANGELOG, because controllers and other nodes see the difference.

## Limits

The simulated radio is a model written for this harness, not a real nRF24, and the transcript says nothing about timing
on real hardware.
