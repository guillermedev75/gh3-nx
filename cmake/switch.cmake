# NRO do Switch (adaptado de project8-sw/src/game/cmake/switch.cmake, commit 3393f26).
# Incluído pelo CMakeLists.txt com -DGH3_SWITCH=ON, dentro do container gh3-switch-clang
# (devkitA64 + libnx), com o toolchain cmake/switch-rexglue-gcc.cmake.
# O codegen não roda aqui: o rexglue é ferramenta do host (tools/build_host_sdk.sh) e o
# compilador cruzado só consome o generated/default/ que ele deixou.
set(REXSDK_DIR "" CACHE PATH "Árvore do SDK ReXGlue patcheado (tools/setup_sdk.sh)")
if(NOT EXISTS "${REXSDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Defina REXSDK_DIR com a árvore do SDK patcheado (tools/setup_sdk.sh)")
endif()
set(GH3_PGO "" CACHE STRING
    "PGO: vazio, gerar (NRO instrumentado que grava o perfil no SD) ou usar (perfil em GH3_PGO_DIR)")
set(GH3_PGO_DIR "${CMAKE_SOURCE_DIR}/out/pgo" CACHE PATH "Pasta dos .gcda do PGO")
set(GH3_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated/default" CACHE PATH
    "Código traduzido localmente do default.xex do próprio disco")
if(NOT EXISTS "${GH3_GENERATED_DIR}/sources.cmake" OR
   NOT EXISTS "${GH3_GENERATED_DIR}/gh3_pch.h" OR
   NOT EXISTS "${GH3_GENERATED_DIR}/gh3_init.h")
    message(FATAL_ERROR
        "generated/default/ incompleto: rode o codegen no host (tools/build_host_sdk.sh) "
        "com o default.xex do próprio disco antes do build do Switch.")
endif()
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
include("${GH3_GENERATED_DIR}/sources.cmake")
if(NOT GENERATED_SOURCES)
    message(FATAL_ERROR "generated/default/sources.cmake não lista nenhuma unidade de tradução")
endif()
add_library(gh3_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(gh3_recomp PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${GH3_GENERATED_DIR}")
target_link_libraries(gh3_recomp PRIVATE rex::runtime)
target_precompile_headers(gh3_recomp PRIVATE "${GH3_GENERATED_DIR}/gh3_pch.h")
# Do project8-sw: o GCC juntava pares de cópias de 64 bits do contexto do guest num load/store de
# registrador q de 128 bits, e uma page fault no meio voltava com o q31 zerado no Horizon (crash
# real no THPS). Registradores gerais sobrevivem a todo caminho de retomada; os vetoriais não.
target_compile_options(gh3_recomp PRIVATE -fno-tree-slp-vectorize -fno-tree-vectorize)
# LTO só no código traduzido + executável (o SDK fica como está): com as chamadas diretas
# (tools/llamadas_diretas.py) o GCC passa a inlinar entre funções traduzidas. -flto=6 limita os
# jobs de link (o Docker tem 7,7 GiB).
if(NOT GH3_PGO STREQUAL "gerar")
    target_compile_options(gh3_recomp PRIVATE -flto=6 -fno-fat-lto-objects)
endif()

# No Switch o plugin xenos já entra inteiro no executável (rexglue_switch_configure_target, com
# --whole-archive), então o renderer nativo e a GPU emulada linkam direto nele; não existe a
# cópia OBJECT gh3_gpu_xenos do build do Mac.
add_executable(gh3 src/main.cpp
    src/native/gh3_native_system.cpp
    src/native/native_renderer.cpp
    src/native/native_textures.cpp
    src/native/native_geometry_shaders.cpp)
target_include_directories(gh3 PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${GH3_GENERATED_DIR}"
    "${REXSDK_DIR}/thirdparty/imgui"
    # O SPIR-V pré-compilado do SDK (shader de gamma do swap, CAS da cena no T32).
    "${REXSDK_DIR}/src/graphics"
    "${REXSDK_DIR}/src/ui/shaders/vulkan_spirv"
    # O builder de SPIR-V do glslang, que os headers do tradutor incluem.
    "${REXSDK_DIR}/thirdparty/glslang")
target_compile_definitions(gh3 PRIVATE GH3_NATIVE_RENDERER=1)
target_link_libraries(gh3 PRIVATE gh3_recomp)
# F2.2 (T35): spirv-opt nos shaders traduzidos. O SDK é subdiretório aqui, então os alvos do
# T34 (F2.1) propagam os includes de spirv-tools.
target_link_libraries(gh3 PRIVATE SPIRV-Tools-opt SPIRV-Tools-link SPIRV-Tools)
if(NOT GH3_PGO STREQUAL "gerar")
    target_link_options(gh3 PRIVATE -flto=6 -flto-partition=balanced)
endif()
# PGO (retirado do nfsmw-nx, app/CMakeLists.txt e src/nfsmw_pgo.cpp): o build de gravação usa
# -fprofile-generate e o app desvia os .gcda para sdmc:/switch/gh3-nx/pgo; o de uso relê esses
# arquivos. Sem LTO na gravação (é o que o nfsmw faz e evita perder os contadores no link).
if(GH3_PGO)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" GH3_PGO_PREFIXO)
    if(GH3_PGO STREQUAL "gerar")
        # -fno-profile-values: o perfil de chamadas indiretas lê tpidr_el0, que é 0 no Horizon
        # (libnx usa -mtp=soft) e derrubava o binário antes do main. Ramos e funções bastam.
        set(GH3_PGO_OPCOES -fprofile-generate=${GH3_PGO_DIR}
            "-fprofile-prefix-path=${GH3_PGO_PREFIXO}"
            -fprofile-update=single -fno-profile-values)
        target_compile_options(gh3_recomp PRIVATE ${GH3_PGO_OPCOES})
        target_compile_options(gh3 PRIVATE ${GH3_PGO_OPCOES})
        target_link_options(gh3 PRIVATE -fprofile-generate -Wl,--wrap=fopen)
        target_compile_definitions(gh3 PRIVATE GH3_PGO_GERAR=1
            "GH3_PGO_DIR=\"${GH3_PGO_DIR}\"")
        target_sources(gh3 PRIVATE src/pgo.cpp)
    elseif(GH3_PGO STREQUAL "usar")
        # -fprofile-correction: os contadores da gravação não são atômicos
        # (-fprofile-update=single) e arquivos usados por vários threads ficam negativos; sem
        # isto o GCC trata o perfil como corrompido e para.
        set(GH3_PGO_OPCOES -fprofile-use=${GH3_PGO_DIR}
            "-fprofile-prefix-path=${GH3_PGO_PREFIXO}"
            -fprofile-partial-training -fno-profile-values -fprofile-correction
            -Wno-missing-profile -Wno-error=coverage-mismatch)
        target_compile_options(gh3_recomp PRIVATE ${GH3_PGO_OPCOES})
        target_compile_options(gh3 PRIVATE ${GH3_PGO_OPCOES})
        target_link_options(gh3 PRIVATE -fprofile-use -fprofile-partial-training -fprofile-correction)
    else()
        message(FATAL_ERROR "GH3_PGO tem que ser vazio, gerar ou usar (e '${GH3_PGO}')")
    endif()
endif()

rexglue_switch_configure_target(gh3 GPU_PLUGINS xenos)
set(GH3_NRO_NAME "Guitar Hero III: Legends of Rock" CACHE STRING "Título mostrado pelo Homebrew Menu")
# Ícone: o logo do jogo, gerado na máquina de quem tem o disco (tools/make_icon.py → out/switch/icon.jpg,
# fora do git); sem ele, o ícone genérico do repositório.
set(GH3_NRO_ICON "${CMAKE_SOURCE_DIR}/switch/icon.jpg")
if(EXISTS "${CMAKE_SOURCE_DIR}/out/switch/icon.jpg")
    set(GH3_NRO_ICON "${CMAKE_SOURCE_DIR}/out/switch/icon.jpg")
endif()
set(GH3_NRO_VERSION "0.1.0" CACHE STRING "Versão mostrada pelo Homebrew Menu")
rexglue_switch_add_nro(gh3 NAME "${GH3_NRO_NAME}" AUTHOR "Guillerme Manhaes"
    VERSION "${GH3_NRO_VERSION}" ICON "${GH3_NRO_ICON}")
