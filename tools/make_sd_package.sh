#!/usr/bin/env bash
# Monta o pacote do SD: <out>/switch/gh3-nx/{gh3-nx.nro,gh3.toml,LICENSES.txt}.
#
#   tools/make_sd_package.sh [--nro build/switch/gh3.nro] [--out out/sd]
#
# Só o que o port produz. O jogo (game/ ao lado do NRO) vem do disco do próprio jogador e é copiado
# por ele (H2 no plano); por isso o pacote passa no tools/check_no_game_content.sh antes de sair.
set -euo pipefail
cd "$(dirname "$0")/.."

nro=build/switch/gh3.nro
out=out/sd
while [ $# -gt 0 ]; do
  case "$1" in
    --nro) nro=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    *) echo "uso: $0 [--nro ARQ] [--out DIR]" >&2; exit 2 ;;
  esac
done

python3 tools/check_nro.py "$nro"

pkg="$out/switch/gh3-nx"
# Pasta recriada do zero: nada de um pacote anterior (ou copiado à mão) sobra dentro dela.
rm -rf "$pkg"
mkdir -p "$pkg"
cp "$nro" "$pkg/gh3-nx.nro"
# O runtime lê <nome do app>.toml ao lado do executável (ReXApp::SetupEnvironment).
cp switch/gh3.toml "$pkg/gh3.toml"
{
  cat LICENSE
  printf '\n\n========================================================================\n\n'
  cat NOTICE
} > "$pkg/LICENSES.txt"

tools/check_no_game_content.sh "$out"
echo "pacote: $pkg"
