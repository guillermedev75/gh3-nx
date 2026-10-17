# Progresso — gh3-marco-c

Memória da execução. O runner anexa ✅/❌/🚫 por iteração; o agente anexa o que fez,
as decisões (sempre com o porquê) e o que ficou pendente. Não apague entradas antigas.

## Log

### T1 — tentativa 1
- feito: `tools/setup_sdk.sh` (setup / `--check [--sdk DIR]` / `--list`), `tests/test_setup_sdk.py` (os 2 testes do plano), `sdk/` no `.gitignore`, linha no playbook §1. `sdk/` real montado: f5337cd + 16 patches (140 arquivos, +19289/−243), MoltenVK em db66022. Aceite e suíte (22 testes) verdes.
- decisões: o sandbox do agente não lê `~/gh3-refs`; o script usa `$P8` → `~/gh3-refs/project8-sw` se legível → senão clona `R-drg/project8-sw` fixo em 3393f26 (1.0.0) em `out/refs/` (ignorado). Nenhuma dependência nova.
- decisões: `--check` não usa `apply -R` patch a patch (o plano sugeria) porque patches posteriores mexem nos mesmos trechos e o reverso de um patch antigo falha numa árvore correta. Monta num índice temporário a árvore esperada (f5337cd + série) e exige: HEAD = f5337cd, índice idêntico a ela e cópia de trabalho sem diferença em relação ao índice.
- decisões: os patches entram com `--index` antes de `submodule update`, na ordem do README da série (o 0002 move o gitlink do MoltenVK), e não depois, como estava no esboço do plano.
- pendente: T2 consome `sdk/`. Não medi o tempo de clone + patch (não deu pra cronometrar no sandbox).
- 2026-10-17 05:37 T1 ✅ aceita (iter 1)
