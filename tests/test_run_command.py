import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent


def fake_checkout(tmp_path, build_dir):
    """Cópia do run-gh3.command com um gh3 falso em build_dir que só imprime de onde foi aberto."""
    shutil.copy(ROOT / "run-gh3.command", tmp_path / "run-gh3.command")
    exe = tmp_path / build_dir / "gh3"
    exe.parent.mkdir(parents=True)
    exe.write_text(f"#!/bin/sh\necho {build_dir}\n")
    exe.chmod(0o755)


def run(tmp_path, **env):
    return subprocess.run([str(tmp_path / "run-gh3.command")], capture_output=True, text=True, timeout=30,
                          env=dict(os.environ, GH3_LOG_FILE=str(tmp_path / "x.log"), **env))


def test_default_build_dir(tmp_path):
    fake_checkout(tmp_path, "out/build/mac-arm64-release")
    env = {k: v for k, v in os.environ.items() if k != "GH3_BUILD_DIR"}
    r = subprocess.run([str(tmp_path / "run-gh3.command")], capture_output=True, text=True, timeout=30,
                       env=dict(env, GH3_LOG_FILE=str(tmp_path / "x.log")))
    assert r.stdout.strip() == "out/build/mac-arm64-release", r.stderr


def test_gh3_build_dir_overrides(tmp_path):
    fake_checkout(tmp_path, "out/build/mac-arm64-p8sdk")
    r = run(tmp_path, GH3_BUILD_DIR="out/build/mac-arm64-p8sdk")
    assert r.stdout.strip() == "out/build/mac-arm64-p8sdk", r.stderr
