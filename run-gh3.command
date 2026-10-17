#!/bin/bash
# Abre o GH3 recompilado (macOS) com a GPU emulada. Dois cliques no Finder.
cd "$(dirname "$0")"
exec out/build/mac-arm64-release/gh3 --game_data_root="$PWD/extracted" --gpu_plugin xenos --log_file "$PWD/out/last-run.log" "$@"
