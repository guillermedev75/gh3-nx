# gh3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gh3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NRO do Switch: build cruzado no container (docker/switch), só sobre o generated/ que o host já gerou.
option(GH3_SWITCH "NRO do Switch (devkitA64 + libnx; ver cmake/switch.cmake)" OFF)
if(GH3_SWITCH)
    include(cmake/switch.cmake)
    return()
endif()

include(generated/rexglue.cmake)

# Renderer nativo (GPL-3.0, do project8-sw, que o adaptou do nfsmw-nx; ver NOTICE). Escolhido em tempo
# de execução com --gh3_renderer=native; a GPU emulada continua o padrão. Só compila contra o SDK
# patcheado (preset mac-arm64-p8sdk): usa APIs internas de GPU que o zip 0.10.0 não tem.
option(GH3_NATIVE_RENDERER "Renderer nativo do project8-sw (exige o SDK patcheado em sdk/)" OFF)

# Sources
set(GH3_SOURCES
    src/main.cpp
)
if(GH3_NATIVE_RENDERER)
    list(APPEND GH3_SOURCES
        src/native/gh3_native_system.cpp
        src/native/native_renderer.cpp
        src/native/native_textures.cpp
        src/native/native_geometry_shaders.cpp)
endif()

if(WIN32)
    add_executable(gh3 WIN32 ${GH3_SOURCES})
else()
    add_executable(gh3 ${GH3_SOURCES})
endif()

rexglue_setup_target(gh3 GPU_PLUGINS xenos)

if(GH3_NATIVE_RENDERER)
    set(GH3_SDK_SRC "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "Árvore do SDK ReXGlue patcheado (tools/setup_sdk.sh)")
    if(NOT EXISTS "${GH3_SDK_SRC}/src/graphics/plugin_main.cpp")
        message(FATAL_ERROR "GH3_NATIVE_RENDERER precisa do SDK patcheado em ${GH3_SDK_SRC} (tools/setup_sdk.sh)")
    endif()
    # O nativo chama direto o código de GPU do SDK (registradores, tradutor de shader, builder SPIR-V).
    # No desktop esse código vive no plugin librexgpu-xenos.dylib com visibilidade oculta, então não dá
    # para linkar contra ele. Compila-se o plugin inteiro dentro do executável, como o SDK faz no Switch
    # (sdk/src/graphics/CMakeLists.txt, REXGLUE_PLATFORM_SWITCH), e o Gh3App cria a GPU emulada por essa
    # cópia (rex_gpu_create) em vez de abrir o .dylib: duas cópias carregadas registrariam os mesmos
    # cvars duas vezes e a segunda seria ignorada. Lista de fontes = REXGPU_XENOS_SOURCES com Vulkan,
    # sem D3D12, no SDK fixado pelo tools/setup_sdk.sh.
    set(_gh3_gpu "${GH3_SDK_SRC}/src/graphics")
    add_library(gh3_gpu_xenos OBJECT
        ${_gh3_gpu}/plugin_main.cpp
        ${_gh3_gpu}/flags.cpp
        ${_gh3_gpu}/register_file.cpp
        ${_gh3_gpu}/registers.cpp
        ${_gh3_gpu}/pipeline/shader/shader.cpp
        ${_gh3_gpu}/format/ucode.cpp
        ${_gh3_gpu}/sampler_info.cpp
        ${_gh3_gpu}/graphics_system.cpp
        ${_gh3_gpu}/command_processor.cpp
        ${_gh3_gpu}/util/draw_extent_estimator.cpp
        ${_gh3_gpu}/util/draw.cpp
        ${_gh3_gpu}/packet_disassembler.cpp
        ${_gh3_gpu}/primitive_processor.cpp
        ${_gh3_gpu}/pipeline/render_target/cache.cpp
        ${_gh3_gpu}/shared_memory.cpp
        ${_gh3_gpu}/pipeline/texture/cache.cpp
        ${_gh3_gpu}/pipeline/shader/interpreter.cpp
        ${_gh3_gpu}/pipeline/shader/translator.cpp
        ${_gh3_gpu}/pipeline/shader/translator_disasm.cpp
        ${_gh3_gpu}/pipeline/shader/spirv_builder.cpp
        ${_gh3_gpu}/pipeline/shader/spirv.cpp
        ${_gh3_gpu}/pipeline/shader/spirv_translator.cpp
        ${_gh3_gpu}/pipeline/shader/spirv_translator_alu.cpp
        ${_gh3_gpu}/pipeline/shader/spirv_translator_fetch.cpp
        ${_gh3_gpu}/pipeline/shader/spirv_translator_memexport.cpp
        ${_gh3_gpu}/pipeline/shader/spirv_translator_rb.cpp
        ${_gh3_gpu}/vulkan/graphics_system.cpp
        ${_gh3_gpu}/vulkan/deferred_command_buffer.cpp
        ${_gh3_gpu}/vulkan/command_processor.cpp
        ${_gh3_gpu}/vulkan/draw_census.cc
        ${_gh3_gpu}/vulkan/native_residency.cc
        ${_gh3_gpu}/vulkan/pipeline_cache.cpp
        ${_gh3_gpu}/vulkan/primitive_processor.cpp
        ${_gh3_gpu}/vulkan/render_target_cache.cpp
        ${_gh3_gpu}/vulkan/shader.cpp
        ${_gh3_gpu}/vulkan/shared_memory.cpp
        ${_gh3_gpu}/vulkan/texture_cache.cpp)
    target_include_directories(gh3_gpu_xenos PRIVATE
        "${GH3_SDK_SRC}/thirdparty/renderdoc"
        "${GH3_SDK_SRC}"
        "${GH3_SDK_SRC}/thirdparty/glslang")
    target_compile_options(gh3_gpu_xenos PRIVATE -Wno-unused-variable)
    target_link_libraries(gh3_gpu_xenos PRIVATE rex::runtime rex::xxhash)
    # O SPIR-V pré-compilado do SDK (shader de gamma do swap) e o builder de SPIR-V do glslang, que os
    # headers do tradutor incluem.
    target_include_directories(gh3 PRIVATE
        "${GH3_SDK_SRC}/src/graphics"
        "${GH3_SDK_SRC}/thirdparty/glslang")
    target_compile_definitions(gh3 PRIVATE GH3_NATIVE_RENDERER=1)
    target_link_libraries(gh3 PRIVATE gh3_gpu_xenos rex::xxhash
        rex::SPIRV rex::glslang rex::MachineIndependent rex::GenericCodeGen rex::OSDependent rex::OGLCompiler)
endif()

# O ReXGlue gera gh3_register.cpp como uma única função de ~60 mil registros (6 a 11+ min para compilar).
# Repartir logo depois do codegen, dentro do build, porque o passo de codegen do build regenera o arquivo
# quando ele foi alterado fora dele. Ver tools/split_register.py.
add_custom_command(TARGET gh3_codegen POST_BUILD
    COMMAND ${CMAKE_SOURCE_DIR}/.venv/bin/python ${CMAKE_SOURCE_DIR}/tools/split_register.py
            ${CMAKE_SOURCE_DIR}/generated/default/gh3_register.cpp
    VERBATIM)
