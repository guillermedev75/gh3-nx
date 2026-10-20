# Configuração do harness-loop (lida com `source`). Números inteiros.
# Agente: OpenCode + DeepSeek via OpenRouter (wrapper que lê o prompt do stdin). O runner continua
# sendo quem commita, roda o oráculo e o aceite. As permissões (sem git commit/push) estão em opencode.json.
CLAUDE_BIN="$RUN_DIR/opencode-agent.sh"
HARNESS_MODEL=''
HARNESS_DEEPSEEK_MODEL='opencode/deepseek-v4.1-flash-free'
ORACLE_CMD='.venv/bin/python -m pytest -q tests'            # verificação completa do projeto; precisa estar verde ANTES de começar
MAX_ITERS=50             # teto de iterações (cada uma = 1 sessão do agente com contexto limpo)
MAX_HOURS=48              # teto de tempo de parede
MAX_ATTEMPTS=3           # falhas numa tarefa antes de marcá-la [!]
NO_PROGRESS_LIMIT=5      # falhas seguidas (em qualquer tarefa) antes de parar tudo
BUDGET_PER_ITER=50       # US$ máximo por iteração (--max-budget-usd)
ITER_TIMEOUT_SEC=7200    # sessão que passar disso é encerrada e conta como tentativa falha (rede/Mac dormindo)
# Permissões da sessão headless. acceptEdits = edita arquivos do repo sem perguntar; comando Bash só
# roda se estiver em ALLOWED_TOOLS. NÃO use `auto` (no -p não há quem aprove: toda escrita é negada)
# nem bypassPermissions (libera qualquer comando na sua máquina).
PERMISSION_MODE=acceptEdits
ALLOWED_TOOLS='Bash(ls:*),Bash(mkdir:*),Bash(git status:*),Bash(git diff:*),Bash(git log:*),Bash(git show:*),Bash(git clone:*),Bash(git -C:*),Bash(git apply:*),Bash(git submodule:*),Bash(cmake:*),Bash(ninja:*),Bash(python3:*),Bash(.venv/bin/python:*),Bash(tools/*:*),Bash(./run-gh3.command:*),Bash(GH3_BUILD_DIR=*),Bash(docker:*),Bash(cp:*),Bash(mv:*),Bash(cat:*),Bash(grep:*),Bash(find:*),Bash(head:*),Bash(tail:*),Bash(wc:*),Bash(sed:*),Bash(nm:*),Bash(file:*),Bash(test:*),Bash(chmod:*),Bash(shasum:*),Bash(rm:*),Bash(touch:*),Bash(diff:*),Bash(sort:*),Bash(xxd:*),Bash(strings:*),Bash(bash:*),Bash(sh:*)'         # preenchido pelo harness-init conforme a stack; separado por vírgula
# Negado sempre (deny vence allow): o runner é quem commita; worktree tira o trabalho do lugar.
DISALLOWED_TOOLS='EnterWorktree,Bash(git commit:*),Bash(git push:*),Bash(git reset:*),Bash(git checkout:*),Bash(git switch:*),Bash(git rebase:*),Bash(git merge:*),Bash(git stash:*),Bash(git worktree:*)'
HARNESS_MODEL='claude-sonnet-5-5'         # vazio = modelo padrão do claude
# Arquivos de teste (globs de nome de arquivo).
TEST_GLOBS='*_test.go test_*.py *_test.py *.test.ts *.test.tsx *.test.js *.spec.ts *.spec.tsx *.spec.js'
# Proibidos em linhas adicionadas a arquivos de teste (literais separados por |).
SKIP_PATTERNS='@pytest.mark.skip|@unittest.skip|pytest.skip(|@pytest.mark.xfail'  # projeto Python: 'xit(' casava com sys.exit( (2026-10-17)
# Em teste existente, linha removida/alterada que case aqui = teste enfraquecido → rejeita.
# Literais separados por |; prefixo ^ = começo da linha (ignorando indentação).
PROTECTED_PATTERNS='^func Test|^func Benchmark|^def test|^async def test|^it(|^test(|^describe(|assert|expect(|t.Error|t.Fatal|t.Fail|require.|self.fail|pytest.raises|.should'
