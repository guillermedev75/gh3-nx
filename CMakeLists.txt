# gh3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gh3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(GH3_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(gh3 WIN32 ${GH3_SOURCES})
else()
    add_executable(gh3 ${GH3_SOURCES})
endif()

rexglue_setup_target(gh3 GPU_PLUGINS xenos)

