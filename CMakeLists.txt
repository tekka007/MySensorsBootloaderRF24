cmake_minimum_required(VERSION 3.16)
project(MYSBootloader C)

# --- user options ---------------------------------------------------------------------------------------------------
set(MCU atmega328p CACHE STRING "Target MCU")
set(F_CPU 16000000L CACHE STRING "CPU clock in Hz (16000000L, 8000000L or 1000000L)")
set(BAUD_RATE "" CACHE STRING "STK500 serial baud rate (default: 115200/57600/9600 by F_CPU)")
set(CHANNEL 110 CACHE STRING "nRF24 RF channel, 0-127")
set(PA_LEVEL RF24_PA_MAX CACHE STRING "RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH or RF24_PA_MAX")
set(DATARATE RF24_250KBPS CACHE STRING "RF24_250KBPS, RF24_1MBPS or RF24_2MBPS")
set(SPI_PINS SPI_PINS_CE9_CSN10 CACHE STRING "nRF24 wiring, see src/config.h")
option(WATCHDOG_ON_SKETCH_START "Leave the watchdog running when the application starts" ON)
option(BOOTLOADER_COMMANDS "Enable low level bootloader commands (clear EEPROM, set node/parent id)" ON)
option(SIZE_CHECK "Fail the build if the image exceeds the boot section" ON)
option(BOOTLOADER_DEBUG "Show the bootloader state on PORTD" OFF)
set(BOOTLOADER_SIZE 2048)

if(NOT BAUD_RATE)
  if(F_CPU MATCHES "^16000000")
    set(BAUD_RATE 115200)
  elseif(F_CPU MATCHES "^8000000")
    set(BAUD_RATE 57600)
  else()
    set(BAUD_RATE 9600)
  endif()
endif()

math(EXPR BOOTLOADER_START "0x8000 - ${BOOTLOADER_SIZE}")
math(EXPR BOOTLOADER_START_HEX "${BOOTLOADER_START}" OUTPUT_FORMAT HEXADECIMAL)

file(GLOB BL_SOURCES CONFIGURE_DEPENDS src/*.c)
add_executable(bootloader ${BL_SOURCES})
set_target_properties(bootloader PROPERTIES
  OUTPUT_NAME "MYSBootloader.ch${CHANNEL}.${F_CPU}.${PA_LEVEL}" SUFFIX ".elf")

target_include_directories(bootloader PRIVATE src third_party)
target_compile_definitions(bootloader PRIVATE
  F_CPU=${F_CPU} BAUD_RATE=${BAUD_RATE} RF24_CHANNEL=${CHANNEL} RF24_PA_LEVEL=${PA_LEVEL}
  RF24_DATARATE=${DATARATE} ${SPI_PINS})
if(NOT WATCHDOG_ON_SKETCH_START)
  target_compile_definitions(bootloader PRIVATE NO_WATCHDOG_ON_SKETCH_START)
endif()
if(NOT BOOTLOADER_COMMANDS)
  target_compile_definitions(bootloader PRIVATE NO_BOOTLOADER_COMMANDS)
endif()
if(BOOTLOADER_DEBUG)
  target_compile_definitions(bootloader PRIVATE DEBUG)
endif()

target_compile_options(bootloader PRIVATE
  -mmcu=${MCU} -std=gnu99 -Os -flto -mrelax -fno-reorder-blocks -fno-tree-dominator-opts -ffunction-sections -fdata-sections -fpack-struct -fshort-enums
  -funsigned-char -funsigned-bitfields -Wall -Wextra -Wundef -pedantic)
target_link_options(bootloader PRIVATE
  -mmcu=${MCU} -nostartfiles -Os -mrelax -Wl,--gc-sections -Wl,-static
  -Wl,-section-start=.text=${BOOTLOADER_START_HEX} -Wl,-Map=$<TARGET_FILE_BASE_NAME:bootloader>.map)
target_link_libraries(bootloader PRIVATE m)

# Intel hex image plus a hard check that the result fits in the boot section.
add_custom_command(TARGET bootloader POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom $<TARGET_FILE:bootloader> $<TARGET_FILE_BASE_NAME:bootloader>.hex
  COMMAND ${CMAKE_COMMAND} -DELF=$<TARGET_FILE:bootloader> -DLIMIT=${BOOTLOADER_SIZE}
          -DSIZE_TOOL=${CMAKE_SIZE_UTIL} -DNM_TOOL=${CMAKE_NM} -DENFORCE=${SIZE_CHECK} -P ${CMAKE_SOURCE_DIR}/cmake/check_size.cmake
  VERBATIM)
