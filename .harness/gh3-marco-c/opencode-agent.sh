#!/bin/bash
# Agente do harness usando OpenCode + DeepSeek em vez do Claude.
#
# O harness-loop faz:  build_prompt ... | CLAUDE_BIN <flags do claude> > iter-N.json
# Este wrapper lê o prompt do stdin (as flags do claude são ignoradas) e chama o CLI do OpenCode
# no mesmo diretório, com o modelo DeepSeek via OpenRouter e as permissões do projeto (opencode.json,
# que proíbe git commit/push). O runner continua sendo quem commita e roda o aceite.
set -euo pipefail

prompt=$(cat)
model="${HARNESS_DEEPSEEK_MODEL:-openrouter/deepseek/deepseek-v4.1-flash-1017}"
title="${HARNESS_TASK_ID:-harness}"

exec npx -y opencode-ai@latest run \
  --model "$model" \
  --format json \
  --auto \
  --title "harness $title" \
  --dir "$PWD" \
  "$prompt"
