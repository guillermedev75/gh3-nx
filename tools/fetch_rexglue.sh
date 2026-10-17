#!/usr/bin/env bash
# Baixa o ReXGlue SDK 0.10.0 para macOS arm64 em tools/rexglue (ignorado pelo git).
set -euo pipefail
cd "$(dirname "$0")"
v=0.10.0
if find rexglue -name rexglue -type f 2>/dev/null | grep -q .; then echo "rexglue $v já presente"; exit 0; fi
curl -fL -o /tmp/rexglue-$v.zip "https://github.com/rexglue/rexglue-sdk/releases/download/v$v/rexglue-sdk-$v-mac-arm64.zip"
mkdir -p rexglue && unzip -q -o /tmp/rexglue-$v.zip -d rexglue
