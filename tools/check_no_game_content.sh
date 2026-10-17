#!/usr/bin/env bash
# Recusa publicar qualquer coisa que veio do disco.
#
# Adaptado de project8-sw (tools/check_no_game_content.sh, GPL-3.0; ver NOTICE).
#
#   tools/check_no_game_content.sh        arquivos publicáveis do repo (versionados + não ignorados)
#   tools/check_no_game_content.sh DIR    todos os arquivos de DIR (ex.: out/sd, o pacote do SD)
#
# Confere a lista de arquivos, não as regras do .gitignore. Não há allowlist.
# Compatível com o bash 3.2 do macOS (sem mapfile).

set -euo pipefail

cd "$(dirname "$0")/.."

files=()
if [ $# -gt 0 ]; then
  [ -d "$1" ] || { echo "não é uma pasta: $1" >&2; exit 2; }
  root=${1%/}
  # -L segue symlinks: um link para um arquivo do jogo dentro do pacote também conta.
  while IFS= read -r -d '' f; do files+=("${f#"$root"/}"); done < <(find -L "$root" -type f -print0)
  mode=dir
else
  root=.
  while IFS= read -r -d '' f; do files+=("$f"); done \
    < <(git ls-files --cached --others --exclude-standard -z 2>/dev/null)
  if [ "${#files[@]}" -eq 0 ]; then
    while IFS= read -r -d '' f; do files+=("${f#./}"); done < <(find . -type f -not -path './.git/*' -print0)
  fi
  mode=repo
fi

fail=0
report() {
  printf '\n!! %s\n' "$1"; shift
  printf '   %s\n' "$@"
  fail=1
}

shopt -s nocasematch  # DEFAULT.XEX, game.ISO, data/ contam igual (bash 3.2 tem)

check_glob() {
  local pattern="$1" why="$2" f
  local hits=()
  [ "${#files[@]}" -eq 0 ] && return 0
  # O lado direito é um glob de propósito.
  # shellcheck disable=SC2053
  for f in "${files[@]}"; do [[ "$f" == $pattern ]] && hits+=("$f"); done
  if [ "${#hits[@]}" -gt 0 ]; then report "$why" "${hits[@]}"; fi
}

echo "== conferindo ${#files[@]} arquivos ($mode: $root) =="

check_glob '*.xex'      'o executável do jogo está na árvore'
check_glob '*.iso'      'uma imagem de disco está na árvore'
check_glob '*.img'      'uma imagem de disco está na árvore'
check_glob '*.xen'      'um arquivo do disco (.pak/.pab/.fsb/.img .xen) está na árvore'
check_glob '*.pak'      'um contêiner de assets do disco está na árvore'
check_glob '*.pab'      'um contêiner de assets do disco está na árvore'
check_glob '*.fsb'      'áudio do disco está na árvore'
check_glob '*.xma'      'áudio do disco está na árvore'
check_glob '*.bik'      'vídeo do disco está na árvore'
check_glob 'DATA/*'     'a pasta DATA do disco está na árvore'
check_glob '*/DATA/*'   'a pasta DATA do disco está na árvore'

# A saída do recompilador é a tradução do código de máquina da publisher.
check_glob 'generated/*'     'saída do recompilador está na árvore'
check_glob '*/generated/*'   'saída do recompilador está na árvore'
check_glob '*_recomp.*.cpp'  'unidades de tradução geradas estão na árvore'
check_glob 'extracted/*'     'o disco extraído está na árvore'
check_glob '$SYSTEMUPDATE/*'  'a atualização de sistema do disco está na árvore'
check_glob '*/$SYSTEMUPDATE/*' 'a atualização de sistema do disco está na árvore'

# Pelo conteúdo: XEX2 renomeado e contêineres da Xbox (STFS/GOD: CON, LIVE, PIRS) continuam sendo do disco.
magic=()
for f in ${files[@]+"${files[@]}"}; do
  [ -f "$root/$f" ] || continue
  case "$(head -c 4 "$root/$f" 2>/dev/null)" in
    XEX2|"CON "|LIVE|PIRS) magic+=("$f") ;;
  esac
done
if [ "${#magic[@]}" -gt 0 ]; then report 'um arquivo com cabeçalho XEX2/CON/LIVE/PIRS está na árvore' "${magic[@]}"; fi

# Rede de segurança no repo: tudo o que é legítimo aqui é código, config ou ícone. No pacote do SD
# não vale, porque o NRO passa de 8 MB.
if [ "$mode" = repo ]; then
  big=()
  for f in ${files[@]+"${files[@]}"}; do
    [ -f "$f" ] || continue
    size=$(wc -c < "$f" | tr -d " ")
    [ "$size" -gt $((8 * 1024 * 1024)) ] && big+=("$f ($((size / 1024 / 1024)) MB)")
  done
  if [ "${#big[@]}" -gt 0 ]; then report 'um arquivo de mais de 8 MB está na árvore' "${big[@]}"; fi
fi

if [ "$fail" -ne 0 ]; then
  echo
  echo "RECUSADO: a árvore tem algo que não pode ser publicado."
  exit 1
fi

echo "limpo"
