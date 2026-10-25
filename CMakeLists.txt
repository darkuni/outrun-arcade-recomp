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
)

if(WIN32)
    add_executable(outrun_arcade WIN32 ${OUTRUNARCADE_SOURCES})
else()
    add_executable(outrun_arcade ${OUTRUNARCADE_SOURCES})
endif()

rexglue_setup_target(outrun_arcade GPU_PLUGINS xenos)
