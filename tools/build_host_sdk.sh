#!/usr/bin/env bash
# Compila o SDK patcheado (sdk/, montado por tools/setup_sdk.sh) para o host, instala em out/sdk-install e
# compila o GH3 sobre ele no preset mac-arm64-p8sdk (out/build/mac-arm64-p8sdk/gh3).
# O codegen roda dentro do build (target gh3_codegen) com o rexglue patcheado; o primeiro configure precisa
# de generated/rexglue.cmake, que tools/render_glue.py escreve do template do SDK quando ainda não existe.
set -euo pipefail
cd "$(dirname "$0")/.."
test -f sdk/CMakeLists.txt || { echo "sdk/ ausente: rode tools/setup_sdk.sh" >&2; exit 1; }
args=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
[ "$(uname)" = Darwin ] && args+=(-DCMAKE_OSX_ARCHITECTURES="$(uname -m)")
cmake -S sdk -B out/sdk-host "${args[@]}"
cmake --build out/sdk-host
cmake --install out/sdk-host --prefix out/sdk-install
[ "${1:-}" = --sdk-only ] && exit 0

python3 tools/render_glue.py
cmake --preset mac-arm64-p8sdk
cmake --build out/build/mac-arm64-p8sdk --target gh3_codegen
cmake --preset mac-arm64-p8sdk          # de novo: agora generated/default/sources.cmake existe
cmake --build out/build/mac-arm64-p8sdk
