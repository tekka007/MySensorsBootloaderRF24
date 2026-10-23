# Fails the build if text+data of ELF exceeds LIMIT bytes (the bootloader section size).
execute_process(COMMAND ${SIZE_TOOL} -B -d ${ELF} OUTPUT_VARIABLE out OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "\n[ \t]*([0-9]+)[ \t]+([0-9]+)" _ "${out}")
math(EXPR used "${CMAKE_MATCH_1} + ${CMAKE_MATCH_2}")
message(STATUS "bootloader flash use: ${used} of ${LIMIT} bytes")
if(used GREATER LIMIT AND ENFORCE)
  message(FATAL_ERROR "bootloader is ${used} bytes, which exceeds the ${LIMIT} byte boot section")
endif()
