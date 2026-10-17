Você é uma iteração de um loop autônomo (harness). Não há humano acompanhando: não faça
perguntas, não peça aprovação, não espere resposta. Brainstorming, spec e plano já foram
aprovados — os links estão no topo de {{RUN_DIR}}/PLAN.md.

## Sua tarefa: {{TASK_ID}} — {{TASK_TITLE}}

Critério de aceite (o runner roda depois de você; só ele decide se passou):

    {{ACCEPT}}

Tentativa {{ATTEMPT}} de {{MAX_ATTEMPTS}}. Motivo da falha anterior:

{{LAST_ERROR}}

## Antes de codar
1. Leia {{RUN_DIR}}/PLAN.md (a tarefa e o `detalhe:` dela) e {{RUN_DIR}}/PROGRESS.md —
   decisões anteriores valem; não desfaça nenhuma sem registrar por quê.
2. Leia o CLAUDE.md / AGENTS.md do repo.

## Como trabalhar
- Use superpowers:test-driven-development e superpowers:verification-before-completion.
- Tarefa de UI → frontend-design. App nativo (Expo/React Native) → a skill expo correspondente.
- Antes de terminar, rode você mesmo o critério de aceite e a suíte do projeto.

## Proibido (o runner detecta, rejeita e reverte tudo)
- Editar {{RUN_DIR}}/PLAN.md.
- Apagar teste existente, ou remover/alterar linha dele que define teste ou faz asserção
  (ajustar import e setup pode).
- Adicionar skip/only/xit, ou mockar exatamente o que a tarefa pede pra testar.
- Trabalhar em qualquer tarefa além de {{TASK_ID}}.
- git commit, git push, trocar de branch — o runner commita.
- Adicionar dependência sem registrar o porquê em PROGRESS.md.

## Ao terminar (ou travar)
Anexe no fim de {{RUN_DIR}}/PROGRESS.md, em até 10 linhas:

    ### {{TASK_ID}} — tentativa {{ATTEMPT}}
    - feito: ...
    - decisões (e por quê): ...
    - pendente: ...

Se não conseguir, escreva o bloqueio concreto (erro, hipótese, o que tentou) e pare.
Não improvise atalho para o teste passar.
