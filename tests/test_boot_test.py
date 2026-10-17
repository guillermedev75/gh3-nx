import subprocess
import sys
import textwrap

SCRIPT = "tools/boot_test.py"


def fake_game(tmp_path, body):
    """Processo falso que escreve linhas de log como o runtime do ReXGlue."""
    p = tmp_path / "fake.py"
    p.write_text(textwrap.dedent(body))
    return [sys.executable, str(p)]


def run(tmp_path, cmd, frames, timeout):
    log = tmp_path / "run.log"
    args = [sys.executable, SCRIPT, "--frames", str(frames), "--timeout", str(timeout), "--log", str(log), "--"] + cmd + [str(log)]
    return subprocess.run(args, capture_output=True, text=True, timeout=60)


def test_passes_when_enough_frames_are_presented(tmp_path):
    cmd = fake_game(tmp_path, """
        import sys, time
        with open(sys.argv[1], "w") as f:
            for i in range(50):
                f.write("[debug] [gpu] XELOG_GPU PRESENT: swap_texture_view=0x1\\n"); f.flush()
            time.sleep(30)
    """)
    r = run(tmp_path, cmd, frames=40, timeout=10)
    assert r.returncode == 0, r.stdout + r.stderr


def test_fails_on_timeout_when_frames_stall(tmp_path):
    cmd = fake_game(tmp_path, """
        import sys, time
        with open(sys.argv[1], "w") as f:
            for i in range(10):
                f.write("XELOG_GPU PRESENT\\n"); f.flush()
            time.sleep(30)
    """)
    r = run(tmp_path, cmd, frames=40, timeout=3)
    assert r.returncode == 1


def test_fails_fast_on_fatal(tmp_path):
    cmd = fake_game(tmp_path, """
        import sys, time
        with open(sys.argv[1], "w") as f:
            f.write("XELOG_GPU PRESENT\\n[critical] [FATAL] Call to invalid or unregistered function at guest address 0x82000000\\n"); f.flush()
            time.sleep(30)
    """)
    r = run(tmp_path, cmd, frames=40, timeout=20)
    assert r.returncode == 1 and "0x82000000" in r.stdout


def test_counts_native_renderer_presents(tmp_path):
    # O renderer nativo (src/native/gh3_native_system.cpp) não passa pelo PRESENT do Xenos emulado;
    # ele escreve a própria linha por swap, e o boot_test tem que contá-la.
    cmd = fake_game(tmp_path, """
        import sys, time
        with open(sys.argv[1], "w") as f:
            for i in range(50):
                f.write("[debug] [gpu] XELOG_GPU PRESENT native\\n"); f.flush()
            time.sleep(30)
    """)
    r = run(tmp_path, cmd, frames=40, timeout=10)
    assert r.returncode == 0, r.stdout + r.stderr
