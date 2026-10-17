#!/usr/bin/env bash
# Roda o jogo, pega o endereço de "Call to invalid or unregistered function", registra em
# config/gh3_functions.toml, refaz o build (que roda codegen + split_register) e repete. Para quando um ciclo roda RUN_SECS sem esse erro.
set -uo pipefail
cd "$(dirname "$0")/.."
RUN_SECS=${RUN_SECS:-60}; MAX=${MAX:-20}
for i in $(seq 1 "$MAX"); do
  rm -f out/last-run.log
  ./run-gh3.command --log_level debug > out/discover-run.stdout 2>&1 & pid=$!
  for _ in $(seq 1 "$RUN_SECS"); do sleep 1; kill -0 $pid 2>/dev/null || break; done
  kill $pid 2>/dev/null; pkill -f "mac-arm64-release/gh3" 2>/dev/null; sleep 1
  frames=$(grep -c "PRESENT" out/last-run.log 2>/dev/null || echo 0)
  addr=$(grep -oE "unregistered function at guest address 0x[0-9A-F]{8}" out/last-run.log | grep -oE "0x[0-9A-F]{8}" | head -1)
  if [ -z "$addr" ]; then
    echo "ciclo $i: sem chamada não registrada em ${RUN_SECS}s (quadros: $frames) — fim"
    grep -E "FATAL|critical" out/last-run.log | head -3
    exit 0
  fi
  if grep -q "^$addr " config/gh3_functions.toml; then echo "ciclo $i: $addr já registrado e voltou a falhar — parar"; exit 1; fi
  echo "ciclo $i: quadros $frames, registrando $addr"
  echo "$addr = { name = \"sub_${addr#0x}\" }  # chamada indireta descoberta em runtime (discover_functions.sh, $(date +%F))" >> config/gh3_functions.toml
  cmake --build out/build/mac-arm64-release -j10 > out/discover-build.log 2>&1 || { echo "build falhou"; grep error out/discover-build.log | head -3; exit 1; }
done
echo "chegou a MAX=$MAX ciclos"
