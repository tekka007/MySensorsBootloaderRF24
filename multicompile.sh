#!/bin/bash
# Builds bootloader variants for every clock/channel/power level into ./compiled.
# Generated sources and objects live in ./build; checked-in MYSBootloader.c and Makefile are not touched.
set -u
cd "$(dirname "$0")"

clock_speeds=( 16000000L 8000000L 1000000L )
power_levels=( RF24_PA_MIN RF24_PA_LOW RF24_PA_HIGH RF24_PA_MAX )

declare -A baudrates=( [16000000L]=115200 [8000000L]=57600 [1000000L]=9600 )

mkdir -p compiled
rm -rf build
mkdir build
cp ./*.h build/

for f in "${clock_speeds[@]}"
do
  baudrate="${baudrates[$f]}"
  for i in {1..110}
  do
    for p in "${power_levels[@]}"
    do
      echo "Frequency: $f Channel: $i Baud rate: $baudrate Power level: $p"
      out="compiled/MYSBootloader.ch$i.$f.$p.hex"
      sed -e "s/\${channel}/$i/" -e "s/\${power}/$p/" MYSBootloader.c.original > build/MYSBootloader.c
      sed -e "s/\${frequency}/$f/" -e "s/\${baudrate}/$baudrate/" Makefile.original > build/Makefile
      if ! make -C build; then
        echo "Build failed!!"
        rm -f "$out"
        continue
      fi
      if [ "$(avr-size -B -d build/MYSBootloader.elf | tail -n 1 | awk '{print $1+$2}')" -le 2048 ]
      then
        cp build/MYSBootloader.hex "$out"
      else
        echo "Compiled boot loader is too large to fit in bootloader area!!"
        rm -f "$out"
      fi
    done
  done
done
