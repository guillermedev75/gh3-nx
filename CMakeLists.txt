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


# O ReXGlue gera gh3_register.cpp como uma única função de ~60 mil registros (6 a 11+ min para compilar).
# Repartir logo depois do codegen, dentro do build, porque o passo de codegen do build regenera o arquivo
# quando ele foi alterado fora dele. Ver tools/split_register.py.
add_custom_command(TARGET gh3_codegen POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/.venv/bin/python ${CMAKE_SOURCE_DIR}/tools/split_register.py
            ${CMAKE_SOURCE_DIR}/generated/default/gh3_register.cpp
    VERBATIM)
