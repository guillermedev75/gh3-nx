#!/usr/bin/env bash
# Driver NVK do Switch (mesa-switch d4a00ea0ab3 + patches do project8-sw) → "driver SDK" do NRO.
#   tools/build_mesa.sh                  clona out/mesa, aplica os patches, compila (Docker) e monta out/driver-sdk/lib/
#   tools/build_mesa.sh --check [--out DIR]  sai 0 só se DIR/lib/libvulkan.a define vkCreateInstance e há libnak_rs.a
#   tools/build_mesa.sh --list           imprime os patches na ordem de aplicação
# Precisa do Docker rodando. A imagem devkitA64 é amd64: num Mac ARM roda emulada (lento).
# Patches: $P8, senão ~/gh3-refs/project8-sw, senão o clone que tools/setup_sdk.sh deixa em out/refs/.
set -euo pipefail
cd "$(dirname "$0")/.."
MESA_URL=https://github.com/danfromtico/mesa-switch
MESA_REV=d4a00ea0ab3
IMAGE=gh3-switch-clang
MESA=out/mesa
OUT=out/driver-sdk
if [ -z "${P8:-}" ]; then
  if [ -r "$HOME/gh3-refs/project8-sw/patches/mesa-switch/README.md" ]; then
    P8=$HOME/gh3-refs/project8-sw
  else
    P8=$PWD/out/refs/project8-sw
  fi
fi
PATCHES="$P8/patches/mesa-switch"
# Série do próprio GH3, aplicada depois da do project8-sw (idempotente).
LOCAL_PATCHES="$PWD/patches/mesa-switch"
MODE=build
while [ $# -gt 0 ]; do
  case "$1" in
    --list) MODE=list ;;
    --check) MODE=check ;;
    --out) OUT="$2"; shift ;;
    *) echo "uso: $0 [--list | --check] [--out DIR]" >&2; exit 2 ;;
  esac
  shift
done

p8_patches() { (cd "$PATCHES" && ls 0*.patch); }
local_patches() { (cd "$LOCAL_PATCHES" 2>/dev/null && ls 0*.patch); }
patches() { p8_patches; local_patches; }

check() {
  local a="$OUT/lib/libvulkan.a"
  [ -f "$a" ] || { echo "falta $a"; return 1; }
  [ -f "$OUT/lib/libnak_rs.a" ] || { echo "falta $OUT/lib/libnak_rs.a"; return 1; }
  # grep sem -q: com pipefail, o -q fecha o pipe no 1º acerto e o nm morre de SIGPIPE.
  nm "$a" 2>/dev/null | grep -E ' [TW] _?vkCreateInstance$' >/dev/null || { echo "$a não define vkCreateInstance"; return 1; }
  echo "driver SDK: OK ($OUT)"
}

case "$MODE" in
  list) patches; exit 0 ;;
  check) check; exit $? ;;
esac

[ -d "$PATCHES" ] || { echo "sem $PATCHES (rode tools/setup_sdk.sh antes ou defina P8)" >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo "Docker não está rodando" >&2; exit 1; }

# 1. Imagem do toolchain (devkitA64 + clang 19 + glslang), usada só para juntar e indexar as libs.
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build --platform linux/amd64 -t "$IMAGE" docker/switch

# 2. mesa-switch no commit da série, patches em ordem. Checkout já patcheado é reaproveitado.
if [ ! -d "$MESA/.git" ]; then
  mkdir -p out
  git clone -q "$MESA_URL" "$MESA"
fi
if [ ! -f "$MESA/build-switch-p8.sh" ]; then
  git -C "$MESA" checkout -q "$MESA_REV"
  for p in $(p8_patches); do git -C "$MESA" apply "$PATCHES/$p"; done
fi
# Patches locais valem também num checkout já patcheado pela série do project8-sw.
for p in $(local_patches); do
  if git -C "$MESA" apply --check "$LOCAL_PATCHES/$p" 2>/dev/null; then
    git -C "$MESA" apply "$LOCAL_PATCHES/$p"
    echo "patch local: $p"
  fi
done

# 3. Build: o script do patch 0004 usa a imagem devkitpro-mesa-rust-aarch64 se ela já existir.
#    Ela é montada aqui (Docker.rust + std do alvo Rust, ver docker/switch/mesa-rust.Dockerfile).
MESA_IMAGE=devkitpro-mesa-rust-aarch64
if ! docker run --rm --platform linux/amd64 "$MESA_IMAGE" rustup target list --installed 2>/dev/null \
    | grep -qx aarch64-unknown-linux-gnu; then
  docker build --platform linux/amd64 -f "$MESA/Docker.rust" -t "$MESA_IMAGE:base" "$MESA"
  docker build --platform linux/amd64 --build-arg BASE="$MESA_IMAGE:base" \
    -f docker/switch/mesa-rust.Dockerfile -t "$MESA_IMAGE" docker/switch
fi
(cd "$MESA" && DOCKER_DEFAULT_PLATFORM=linux/amd64 ./build-switch-p8.sh)

# 4. Uma libvulkan.a única (arquivo regular, não thin) com todas as libs menos a do Rust, que o
#    link do NRO quer separada. stage-mesa-archive.py confere que tudo é ELF AArch64 e indexa com o
#    ranlib do devkitA64 (o ar do host pode perder símbolos do runtime Rust no índice).
B=builddir-switch
LIBS="src/nouveau/vulkan/libnvk.a src/nouveau/vulkan/libvulkan.a src/vulkan/util/libvulkan_util.a
src/compiler/nir/libnir.a src/compiler/libcompiler.a src/compiler/spirv/libvtn.a src/util/libxmlconfig.a
src/nouveau/compiler/libnak.a src/nouveau/nil/libnil.a src/nouveau/nil/liblibnil_format_table.a
src/nouveau/mme/libnouveau_mme.a src/nouveau/winsys/libnouveau_ws.a src/nouveau/headers/libnvidia_headers_c.a
src/compiler/rust/libcompiler_c_helpers.a src/util/libmesa_util.a src/util/libmesa_util_simd.a
src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a"
mkdir -p "$OUT/lib"
{
  echo "create /out/lib/libvulkan.merged.a"
  for l in $LIBS; do echo "addlib /mesa/$B/$l"; done
  echo save; echo end
} > "$OUT/merge.mri"
docker run --rm --platform linux/amd64 -v "$PWD/$MESA:/mesa:ro" -v "$PWD/$OUT:/out" -v "$P8/tools/switch:/p8:ro" "$IMAGE" sh -c '
  set -e
  /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ar -M < /out/merge.mri
  python3 /p8/stage-mesa-archive.py /out/lib/libvulkan.merged.a /out/lib/libvulkan.a \
    --ranlib /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ranlib
  rm /out/lib/libvulkan.merged.a /out/merge.mri
  cp /mesa/'"$B"'/src/nouveau/compiler/libnak_rs.a /out/lib/'
check
