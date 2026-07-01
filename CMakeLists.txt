cmake_minimum_required(VERSION 3.3)
project(SoFixer)

# =========================================================
# SoFixer options
# =========================================================
set(SO_64 OFF CACHE BOOL "build SoFixer for 64bit target")

if(SO_64)
    message("building SoFixer for 64bit target")
    add_definitions("-D__SO64__")
    set(TARGET_NAME SoFixer64)
    set(CLI_TARGET_NAME SoFixer-skills-CLI64)
else()
    message("building SoFixer for 32bit target")
    add_definitions("-D__SO32__")
    set(TARGET_NAME SoFixer32)
    set(CLI_TARGET_NAME SoFixer-skills-CLI32)
endif()



set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
if(MINGW)
    message("build with mingw")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

# Core ELF source files. Listed directly per-executable (rather than via a
# shared library) so that SOFIXER_QUIET can be applied to the CLI target only
# — the legacy SoFixer32/64 keeps its original FLOG debug output. The project
# is small, so compiling these sources twice is negligible.
set(SOFIXER_CORE_SRC
        ElfReader.cpp
        ElfRebuilder.cpp
        ObElfReader.cpp)

# =========================================================
# Legacy entry point (unchanged behavior; FLOG output kept).
# =========================================================
add_executable(${TARGET_NAME} main.cpp ${SOFIXER_CORE_SRC})
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# =========================================================
# SoFixer-skills-CLI — unified, AI-friendly entry point.
# SOFIXER_QUIET suppresses FLOG printf so stdout stays clean JSON.
# =========================================================
add_executable(${CLI_TARGET_NAME}
        cli/CliMain.cpp
        cli/CliArgs.cpp
        cli/Json.cpp
        cli/Commands.cpp
        ${SOFIXER_CORE_SRC})
target_include_directories(${CLI_TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/cli)
target_compile_definitions(${CLI_TARGET_NAME} PRIVATE SOFIXER_QUIET)

# On Windows/MinGW ensure no "lib" prefix on the executable name.
if(MINGW OR MSVC)
    set_target_properties(${CLI_TARGET_NAME} PROPERTIES PREFIX "")
endif()
