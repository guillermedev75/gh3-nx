/*
 * Funções quentes agrupadas no começo do .text (F4.2 / T43). O A57 tem 48 KB de I-cache e uma
 * TLB pequena, e o NRO tem ~53 MB de código: com as funções mais chamadas espalhadas pela
 * imagem, cada salto distante paga um passeio pela tabela de páginas. Isto só muda ONDE cada
 * função é colocada (-ffunction-sections já dá uma seção por função); o linker reordena as
 * seções de entrada com --section-ordering-file (cmake/switch.cmake, GH3_ORDEN_FUNCIONES).
 *
 * Lista inicial, tirada do perfil de pilha do Switch (playbook §8: o laço do ring sub_824D0070
 * é o mais quente do thread do jogo, seguido de sub_8272C7C0) e do caminho de espera do ring/
 * thread. A lista completa (centenas de funções) sai de `tools/calientes.py` quando o ELF do
 * MESMO build do perfil é guardado (F5); um ELF de outro build dá nomes errados.
 *
 * O `KEEP (*(.crt0))` tem que ficar primeiro: o _start mora no crt0 do libnx e o endereço 0 é
 * obrigatório (nm build/switch/gh3 | grep ' _start$' precisa dar 0; o NRO não inicia sem isso).
 */
.text : {
  /*
   * libnx's startup code (crt0) must stay at address 0, as in switch.ld. An earlier version
   * of this file moved it (memcpy ended up at 0) and the NRO did not boot.
   */
  KEEP (*(.crt0))
  /*
   * Funções do jogo mais quentes no perfil do Switch (playbook §8). `sub_X` e `__imp__sub_X`
   * são o mesmo corpo; as chamadas diretas (tools/llamadas_diretas.py) usam o `__imp__`.
   */
  *(.text.sub_824D0070)
  *(.text.__imp__sub_824D0070)
  *(.text.sub_8272C7C0)
  *(.text.__imp__sub_8272C7C0)
  /* Esperas do ring/swap/vblank reescritas em src/guest_waits.h (T13/T18). */
  *(.text.sub_824CE958)
  *(.text.sub_82172718)
  *(.text.sub_82172758)
  /*
   * Runtime de espera/troca de contexto e libc: no perfil, quase todo o topo do thread do jogo
   * é svcSleepThread/svcWaitProcessWideKeyAtomic/svcWaitSynchronization, e memcpy/memmove
   * aparecem em cópias de texturas. O memcpy do libc é assembly sem seção por função: vai pelo
   * arquivo (como no nfsmw).
   */
  *(.text.svcSleepThread)
  *(.text.svcWaitSynchronization)
  *(.text.svcWaitProcessWideKeyAtomic)
  *(.text.condvarWaitTimeout)
  *(.text.__thread_entry)
  *libc.a:libc_a-memcpy.o(.text .text.*)
  *libc.a:libc_a-memmove.o(.text .text.*)
  *libc.a:libc_a-memset.o(.text .text.*)
}
