# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\Uppersense_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\Uppersense_autogen.dir\\ParseCache.txt"
  "Uppersense_autogen"
  )
endif()
