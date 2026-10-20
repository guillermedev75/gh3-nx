#!/usr/bin/env bash
# SDK ReXGlue v0.10.0 (f5337cd) + série de patches do project8-sw (camada Horizon, renderer nativo)
# + série local do repositório (patches/rexglue-local: spirv-tools/spirv-opt, F2.1 do plano 60 FPS).
#   tools/setup_sdk.sh                 clona sdk/, aplica as séries e confere
#   tools/setup_sdk.sh --check [--sdk DIR]  sai 0 só se DIR está exatamente em f5337cd + séries inteiras
#   tools/setup_sdk.sh --list          imprime a série do project8-sw na ordem de aplicação
# Patches: $P8, senão ~/gh3-refs/project8-sw, senão clone de R-drg/project8-sw em out/refs/ (ignorado pelo git).
# A série local (patches/rexglue-local/series) é aplicada depois da do project8-sw.
set -euo pipefail
cd "$(dirname "$0")/.."
BASE=f5337cd
P8_REV=3393f26  # project8-sw 1.0.0
if [ -z "${P8:-}" ]; then
  if [ -r "$HOME/gh3-refs/project8-sw/patches/rexglue-sdk/series" ]; then
    P8=$HOME/gh3-refs/project8-sw
  else
    P8=$PWD/out/refs/project8-sw
    if [ ! -d "$P8/.git" ]; then
      mkdir -p out/refs
      git clone -q https://github.com/R-drg/project8-sw "$P8"
      git -C "$P8" checkout -q "$P8_REV"
    fi
  fi
fi
PATCHES="$P8/patches/rexglue-sdk"
SERIES="$PATCHES/series"
LOCAL_PATCHES="$PWD/patches/rexglue-local"
LOCAL_SERIES="$LOCAL_PATCHES/series"
SDK=sdk
MODE=setup
while [ $# -gt 0 ]; do
  case "$1" in
    --list) MODE=list ;;
    --check) MODE=check ;;
    --sdk) SDK="$2"; shift ;;
    *) echo "uso: $0 [--list | --check] [--sdk DIR]" >&2; exit 2 ;;
  esac
  shift
done

# Árvore esperada = f5337cd + série inteira, montada num índice temporário. A árvore real
# (índice do sdk/) tem que ser idêntica e a cópia de trabalho não pode divergir do índice.
check() {
  local tmp expected actual
  git -C "$SDK" rev-parse -q --verify "$BASE^{commit}" >/dev/null 2>&1 || { echo "sem o commit base $BASE em $SDK"; return 1; }
  [ "$(git -C "$SDK" rev-parse HEAD 2>/dev/null)" = "$(git -C "$SDK" rev-parse "$BASE^{commit}")" ] || { echo "HEAD de $SDK não é $BASE"; return 1; }
  tmp=$(mktemp)
  GIT_INDEX_FILE=$tmp git -C "$SDK" read-tree "$BASE"
  while read -r p; do
    GIT_INDEX_FILE=$tmp git -C "$SDK" apply --cached "$PATCHES/$p" 2>/dev/null || { rm -f "$tmp"; echo "patch não aplica sobre $BASE: $p"; return 1; }
  done < "$SERIES"
  while read -r p; do
    GIT_INDEX_FILE=$tmp git -C "$SDK" apply --cached "$LOCAL_PATCHES/$p" 2>/dev/null || { rm -f "$tmp"; echo "patch local não aplica: $p"; return 1; }
  done < "$LOCAL_SERIES"
  expected=$(GIT_INDEX_FILE=$tmp git -C "$SDK" write-tree)
  rm -f "$tmp"
  actual=$(git -C "$SDK" write-tree)
  [ "$expected" = "$actual" ] || { echo "índice de $SDK difere de $BASE + séries"; return 1; }
  git -C "$SDK" diff --quiet --ignore-submodules=dirty || { echo "cópia de trabalho de $SDK difere do índice"; return 1; }
  echo "SDK patcheado: OK ($(wc -l < "$SERIES" | tr -d ' ') patches de $BASE + $(wc -l < "$LOCAL_SERIES" | tr -d ' ') local(is))"
}

case "$MODE" in
  list) cat "$SERIES"; exit 0 ;;
  check) check; exit $? ;;
esac

if check >/dev/null 2>&1; then echo "SDK já patcheado em $SDK"; exit 0; fi
[ -d "$SDK/.git" ] || git clone -q https://github.com/rexglue/rexglue-sdk "$SDK"
git -C "$SDK" reset -q --hard
git -C "$SDK" checkout -q "$BASE"
# --index: o 0002 avança o gitlink do MoltenVK; os submódulos só vêm depois (README da série).
while read -r p; do git -C "$SDK" apply --index "$PATCHES/$p"; done < "$SERIES"
while read -r p; do git -C "$SDK" apply --index "$LOCAL_PATCHES/$p"; done < "$LOCAL_SERIES"
git -C "$SDK" submodule sync -q --recursive
git -C "$SDK" submodule update -q --init --recursive
check
