# Fails the build if text+data of ELF exceeds LIMIT bytes (the bootloader section size).
execute_process(COMMAND ${SIZE_TOOL} -B -d ${ELF} OUTPUT_VARIABLE out OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "\n[ \t]*([0-9]+)[ \t]+([0-9]+)" _ "${out}")
math(EXPR used "${CMAKE_MATCH_1} + ${CMAKE_MATCH_2}")
message(STATUS "bootloader flash use: ${used} of ${LIMIT} bytes")
if(used GREATER LIMIT AND ENFORCE)
  message(FATAL_ERROR "bootloader is ${used} bytes, which exceeds the ${LIMIT} byte boot section")
endif()

# BOOTRST jumps to the start of the boot section, so main (section .init9) has to be the first code there. Any
# data placed in front of it (for example a PROGMEM table) would be executed as code.
execute_process(COMMAND ${NM_TOOL} ${ELF} OUTPUT_VARIABLE symbols)
string(REGEX MATCH "([0-9a-fA-F]+) T main" _ "${symbols}")
math(EXPR main_address "0x${CMAKE_MATCH_1}")
math(EXPR boot_start "0x8000 - ${LIMIT}")
if(NOT main_address EQUAL boot_start)
  message(FATAL_ERROR "main is at ${main_address}, but the reset vector jumps to ${boot_start}")
endif()
