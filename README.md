# MYSBootloader 1.3.0-rc.1

OTA RF24 bootloader for [MySensors](https://www.mysensors.org), based on MySensors library 2.2. It fits in the 2 KiB
boot section of an ATmega328p, updates the application over the nRF24 radio, and doubles as a serial STK500v1 loader
(`avrdude -c arduino`) after an external reset.

- Protocol details for controller authors: [docs/PROTOCOL.md](docs/PROTOCOL.md)
- Release notes: [CHANGELOG.md](CHANGELOG.md)

## Layout

| Path | Contents |
|------|----------|
| `src/` | Bootloader sources. `config.h` holds the build settings, `main.c` the entry point. |
| `third_party/` | `boot.h`, the avr-libc SPM helpers with the short-form page operations used here. |
| `cmake/` | AVR toolchain file and the image size check. |
| `tools/build_variants.sh` | Builds every clock/channel/PA-level image. |
| `tests/sim/` | Regression tests that run the bootloader in simavr against an emulated nRF24 and controller. |
| `hex/` | Ready-made default images, see below. |
| `arduino/boards.txt` | Board definition for the Arduino IDE. |

## Building

Requires `avr-gcc`, `avr-libc`, `avr-binutils` and CMake 3.16 or newer.

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/avr-gcc.cmake -DF_CPU=8000000L -DCHANNEL=76
cmake --build build        # writes build/MYSBootloader.ch76.8000000L.RF24_PA_MAX.hex
```

The build fails if the image is larger than the 2048 byte boot section, and prints its size.

| Variable | Default | Meaning |
|----------|---------|---------|
| `F_CPU` | `16000000L` | CPU clock: `16000000L`, `8000000L` or `1000000L` |
| `BAUD_RATE` | by `F_CPU` | STK500 baud rate: 115200, 57600 or 9600 |
| `CHANNEL` | `110` | nRF24 channel, 0-127 |
| `PA_LEVEL` | `RF24_PA_MAX` | `RF24_PA_MIN` (-18 dBm), `RF24_PA_LOW` (-12), `RF24_PA_HIGH` (-6), `RF24_PA_MAX` (0) |
| `DATARATE` | `RF24_250KBPS` | `RF24_250KBPS`, `RF24_1MBPS` or `RF24_2MBPS` |
| `SPI_PINS` | `SPI_PINS_CE9_CSN10` | nRF24 wiring, see below |
| `WATCHDOG_ON_SKETCH_START` | `ON` | Leave the watchdog running (8 s) when the application starts |
| `BOOTLOADER_COMMANDS` | `ON` | Clear EEPROM / set node id / set parent id commands from the controller |
| `BOOTLOADER_DEBUG` | `OFF` | Show the bootloader state as a bit pattern on PORTD |
| `SIZE_CHECK` | `ON` | Fail the build when the image does not fit |

Less common settings (LED pin, watchdog timeout, radio address) are defaults in `src/config.h` and can be overridden
with `-D` compiler flags, e.g. `-DCMAKE_C_FLAGS=-DLED_PIN=PINB1` for NRF24Duino.

To build many variants at once:

```sh
CLOCKS="8000000L" CHANNELS="76 100" tools/build_variants.sh out
```

### Ready-made images

`hex/` holds the default images for the common boards (16 MHz, 8 MHz, 1 MHz and NRFduino 8 MHz, channel 110). They
were built before the source reorganisation. Anything else, in particular another channel or PA level, has to be built
from source.

## Supported nRF24 wiring configurations

Select exactly one `SPI_PINS_*` value with `-DSPI_PINS=...`.

| Define | CE pin | CSN pin | Board example |
|--------|--------|---------|---------------|
| `SPI_PINS_CE9_CSN10` | D9 (PB1) | D10 (PB2) | Default MySensors |
| `SPI_PINS_CSN7_CE8` | D8 (PB0) | D7 (PD7) | |
| `SPI_PINS_CE7_CSN8` | D7 (PD7) | D8 (PB0) | RF-Nano Red |
| `SPI_PINS_CE4_CSN10` | D4 (PD4) | D10 (PB2) | |
| `SPI_PINS_CE7_CSN10` | D7 (PD7) | D10 (PB2) | NRF24Duino |
| `SPI_PINS_CE10_CSN9` | D10 (PB2) | D9 (PB1) | RF-Nano Black |
| `SPI_PINS_CE4_CSN5` | D4 (PD4) | D5 (PD5) | RFduino, some RF-Nano variants |

### Note on RF-Nano boards

"RF-Nano" is a generic name used by several manufacturers for Arduino Nano-sized boards with an integrated nRF24L01+.
The CE/CSN wiring is **not guaranteed** to be the same across vendors or even across batches from the same vendor.
Common variants:

- **RF-Nano Red** (red PCB, V1): usually `SPI_PINS_CE7_CSN8` (CE=D7, CSN=D8).
- **RF-Nano Black** (black PCB, V1/V3): usually `SPI_PINS_CE10_CSN9` (CE=D10, CSN=D9).

If your board matches neither, follow the PCB traces from the nRF24L01+ module to the ATmega328P. The nRF24L01+ pinout
is: 1 GND, 2 VCC, 3 CE, 4 CSN, 5 SCK, 6 MOSI, 7 MISO, 8 IRQ. Then pick the matching `SPI_PINS_*` value, or add a new
one in `src/board.h`.

## Flashing

Flash the image and set the fuses with `avrdude` through an ISP programmer. The fuses depend on the clock source:

| Clock source | lfuse | hfuse | efuse |
|--------------|-------|-------|-------|
| External crystal 8-16 MHz | `0xFF` | `0xD2` | `0x06` |
| Internal RC 8 MHz | `0xE2` | `0xD2` | `0x06` |
| Internal RC 1 MHz | `0x62` | `0xD2` | `0x06` |

`hfuse=0xD2` is constant: it selects the 2048 byte boot section (`BOOTSZ=01`) and enables `BOOTRST`, which places the
bootloader at `0x7800`. Verify the `lfuse` against your board's actual clock source before flashing.

```sh
avrdude -c usbasp -p m328p -e -Ulock:w:0x3f:m -Uefuse:w:0x06:m -Uhfuse:w:0xD2:m -Ulfuse:w:0xFF:m
avrdude -c usbasp -p m328p -Uflash:w:build/MYSBootloader.ch110.16000000L.RF24_PA_MAX.hex -Ulock:w:0x0f:m
```

## Development notes

The image must fit in 2048 bytes and currently uses 1872 to 1940 bytes depending on the configuration, so size matters:
check the `bootloader flash use: N of 2048 bytes` line of `cmake --build` after every change. The build also fails if
`main` is not the first code in the boot section (the reset vector jumps there), which happens if data such as a
`PROGMEM` table is placed in front of it.

Size techniques in use: the sources are linked with `-flto`; all variables live in `.noinit` so no `.data`/`.bss`
startup code is linked in (every variable is written before it is read); EEPROM access, the CRC loop and the buffer
comparison are small hand-sized routines instead of libc calls; `main` is `OS_main` in `.init9` and the application is
started with a plain `jmp 0`.

### Tests

`tests/sim/run.sh <elf> <F_CPU>` runs the bootloader in [simavr](https://github.com/buserror/simavr) against an emulated
nRF24L01+ and a scripted MySensors controller. It covers a full OTA update (64 blocks, CRC check, start of the new
application), an up-to-date node, a firmware with a wrong CRC, a firmware config with zero blocks, the bootloader
commands and a serial STK500 programming session. It needs `simavr`, `libsimavr-dev` and `libelf-dev`:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/avr-gcc.cmake -DF_CPU=8000000L
cmake --build build
tests/sim/run.sh build/MYSBootloader.ch110.8000000L.RF24_PA_MAX.elf 8000000
```

CI builds all configurations and runs these tests for every clock.
