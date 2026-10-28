# outrun_arcade - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(outrun_arcade LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(OUTRUNARCADE_SOURCES
    src/main.cpp
    src/game_menu.cpp
    src/src_trace.cpp
    src/voice_trace.cpp
    src/crash_handler.cpp
    src/callout_hold.cpp
)

if(WIN32)
    add_executable(outrun_arcade WIN32 ${OUTRUNARCADE_SOURCES})
else()
    add_executable(outrun_arcade ${OUTRUNARCADE_SOURCES})
endif()

rexglue_setup_target(outrun_arcade GPU_PLUGINS xenos)

# Linker map: turns crash-report offsets (outrun_arcade.exe+0x...) into
# recompiled function names.
if(WIN32)
    target_link_options(outrun_arcade PRIVATE "LINKER:/MAP:${CMAKE_BINARY_DIR}/outrun_arcade.map")
endif()
