#!/usr/bin/env bash
# Depois de uma queda "Call to invalid or unregistered function" jogando: pega o endereço em out/last-run.log,
# registra em config/gh3_functions.toml, recompila, roda o teste de boot (60 s) e reabre o jogo.
set -euo pipefail
cd "$(dirname "$0")/.."
addr=$(grep -oE "unregistered function at guest address 0x[0-9A-F]{8}" out/last-run.log | tail -1 | grep -oE "0x[0-9A-F]{8}" || true)
[ -n "$addr" ] || { echo "nenhuma função não registrada no último log"; exit 1; }
if grep -q "^$addr " config/gh3_functions.toml; then echo "$addr já registrado — a queda é outra coisa"; exit 1; fi
echo "$addr = { name = \"sub_${addr#0x}\" }  # queda jogando, $(date +%F) (fix_last_crash.sh)" >> config/gh3_functions.toml
cmake --build out/build/mac-arm64-p8sdk -j10 > out/build.log 2>&1 || { grep -E "error" out/build.log | head -5; exit 1; }
RUN_SECS=60 MAX=1 tools/discover_functions.sh | grep -E "ciclo" || true
echo "registrado $addr"
