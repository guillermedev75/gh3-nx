#!/usr/bin/env bash
# SPIRV-Tools + SPIRV-Tools-opt para o spirv-opt do renderer nativo (plano 60 FPS, F2.1).
# A ligação no SDK é um patch local (patches/rexglue-local, aplicado pelo tools/setup_sdk.sh); aqui só se constrói
# e instala.
#   tools/build_spirv_tools.sh            build + install no Mac (tools/build_host_sdk.sh --sdk-only)
#   tools/build_spirv_tools.sh --switch   build + install com o devkitA64 (Docker), em out/sdk-switch-install
#   tools/build_spirv_tools.sh --check    sai 0 só se o patch está aplicado e a lib do Mac tem o Optimizer
set -euo pipefail
cd "$(dirname "$0")/.."

SDK_TERCEIROS=sdk/thirdparty/CMakeLists.txt
LIB_MAC=out/sdk-install/lib/libSPIRV-Tools-opt.a
HEADER_MAC=out/sdk-install/include/spirv-tools/optimizer.hpp
LIB_SWITCH=out/sdk-switch-install/lib/libSPIRV-Tools-opt.a

# Sem grep -q: sob pipefail o nm morre de SIGPIPE e o pipeline "falha" com a lib certa.
tem_optimizer() { nm "$1" 2>/dev/null | grep '8spvtools9Optimizer' >/dev/null; }

check() {
  grep -q 'add_subdirectory(spirv-tools' "$SDK_TERCEIROS" ||
    { echo "sdk/ sem o spirv-tools: rode tools/setup_sdk.sh" >&2; return 1; }
  [ -f "$LIB_MAC" ] ||
    { echo "falta $LIB_MAC: rode tools/build_spirv_tools.sh" >&2; return 1; }
  [ -f "$HEADER_MAC" ] || { echo "falta $HEADER_MAC" >&2; return 1; }
  tem_optimizer "$LIB_MAC" ||
    { echo "$LIB_MAC sem o símbolo spvtools::Optimizer" >&2; return 1; }
  echo "spirv-tools: OK ($LIB_MAC)"
  if [ -f "$LIB_SWITCH" ] && tem_optimizer "$LIB_SWITCH"; then
    echo "spirv-tools (devkitA64): OK ($LIB_SWITCH)"
  else
    echo "spirv-tools (devkitA64): ausente em $LIB_SWITCH (tools/build_spirv_tools.sh --switch)"
  fi
}

case "${1:-}" in
  --check)
    check
    ;;
  --switch)
    # spirv-tools avulso com o toolchain do NRO: as mesmas opções do patch no sdk/thirdparty
    # (só bibliotecas), instalado à parte para o T35 linkar no build do Switch.
    docker run --rm --platform linux/amd64 -v "$PWD":/project -w /project gh3-switch-clang sh -c '
      set -e
      cmake -S sdk/thirdparty/spirv-tools -B out/spirv-switch -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/project/cmake/switch-rexglue-gcc.cmake \
        -DSPIRV-Headers_SOURCE_DIR=/project/sdk/thirdparty/spirv-headers \
        -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON \
        -DSPIRV_WERROR=OFF -DSPIRV_TOOLS_BUILD_STATIC=ON \
        -DCMAKE_INSTALL_PREFIX=/project/out/sdk-switch-install \
        -DCMAKE_INSTALL_LIBDIR=lib
      cmake --build out/spirv-switch -j 6
      cmake --install out/spirv-switch
    '
    check
    ;;
  "")
    tools/build_host_sdk.sh --sdk-only
    check
    ;;
  *)
    echo "uso: $0 [--switch | --check]" >&2
    exit 2
    ;;
esac
