#!/bin/bash
# Builds bootloader variants for every clock/channel/power level into ./compiled.
# MYSBootloader.c and Makefile are copied into ./build with the channel, PA level, clock and baud rate lines rewritten;
# the checked-in files are not touched.
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
      sed -E -e "s/^(#define[[:space:]]+RF24_CHANNEL[[:space:]]+)\([0-9]+\)/\1($i)/" \
           -e "s/^(#define[[:space:]]+RF24_PA_LEVEL[[:space:]]+)RF24_PA_[A-Z]+/\1$p/" MYSBootloader.c > build/MYSBootloader.c
      sed -E -e "s/^(CLK[[:space:]]*=[[:space:]]*).*/\1$f/" \
           -e "s/^(BAUDRATE[[:space:]]*=[[:space:]]*).*/\1$baudrate/" Makefile > build/Makefile
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
