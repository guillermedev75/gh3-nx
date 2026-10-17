"""Teste de boot: abre o jogo, conta quadros apresentados no log e passa ao atingir o alvo.

Conta as linhas "XELOG_GPU PRESENT" que o runtime escreve com --log_level debug (um por quadro apresentado).
Sai 0 ao chegar em --frames antes de --timeout; sai 1 em timeout ou ao ver "[FATAL]" no log. Sempre fecha o
processo que abriu (e só ele).

Uso:
  python3 tools/boot_test.py --frames 300 --timeout 180            # abre ./run-gh3.command
  python3 tools/boot_test.py --frames N --timeout S --log ARQ -- CMD ...   # comando próprio (testes)
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import time

FATAL = re.compile(r"\[FATAL\].*")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--timeout", type=float, default=180)
    ap.add_argument("--log", default="out/boot-test.log")
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args(argv)
    if os.path.exists(a.log):
        os.remove(a.log)
    cmd = a.cmd or ["./run-gh3.command", "--log_level", "debug"]
    env = dict(os.environ, GH3_LOG_FILE=os.path.abspath(a.log))  # run-gh3.command já passa --log_file
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    frames, deadline, result = 0, time.time() + a.timeout, 1
    try:
        while time.time() < deadline:
            time.sleep(0.5)
            if os.path.exists(a.log):
                text = open(a.log, errors="replace").read()
                frames = text.count("XELOG_GPU PRESENT")
                fatal = FATAL.search(text)
                if fatal:
                    print(f"FALHOU: {fatal.group(0)} (quadros: {frames})")
                    return 1
                if frames >= a.frames:
                    result = 0
                    break
            if proc.poll() is not None:
                print(f"FALHOU: o jogo saiu (código {proc.returncode}, quadros: {frames})")
                return 1
        print(("OK" if result == 0 else "FALHOU: timeout") + f": {frames} quadros em até {a.timeout:.0f} s (alvo {a.frames})")
        return result
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)  # só o grupo que abrimos
        except ProcessLookupError:
            pass


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
