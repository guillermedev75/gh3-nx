#!/usr/bin/env bash
# Codegen do GH3 + reparte gh3_register.cpp (sem isso a recompilação leva 6-11 min; com isso, segundos).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out
tools/rexglue/mac-arm64/bin/rexglue codegen gh3_manifest.toml > out/codegen.stdout 2>&1 || { tail -20 out/codegen.stdout; exit 1; }
tail -3 out/codegen.stdout
.venv/bin/python tools/split_register.py generated/default/gh3_register.cpp
.venv/bin/python tools/llamadas_diretas.py generated/default/gh3_recomp.*.cpp
