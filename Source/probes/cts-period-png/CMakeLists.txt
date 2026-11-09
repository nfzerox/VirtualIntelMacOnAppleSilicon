cmake_minimum_required(VERSION 3.5)
project(VIMPeriodCTS_PNG C)
# Diagnostic dependency only. Old libpng's own CMake reads target LOCATION,
# removed by modern CMake. Keep its C sources/config unmodified and compile
# the portable implementation against the same period SDK as the CTS.
if(NOT EXISTS "${PNG_SOURCE}/png.h")
    message(FATAL_ERROR "PNG_SOURCE must name the libpng 1.6.27 source tree")
endif()
configure_file("${PNG_SOURCE}/scripts/pnglibconf.h.prebuilt" pnglibconf.h COPYONLY)
set(names png pngset pngget pngrutil pngtrans pngwutil pngread pngrio pngwio
    pngwrite pngrtran pngwtran pngmem pngerror pngpread)
foreach(name ${names})
    list(APPEND sources "${PNG_SOURCE}/${name}.c")
endforeach()
add_library(png16 STATIC ${sources})
target_include_directories(png16 PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" "${PNG_SOURCE}")
target_compile_definitions(png16 PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0)
