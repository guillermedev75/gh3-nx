import subprocess
import sys

SCRIPT = "tools/render_glue.py"


def test_renders_cmake_vars_and_values(tmp_path):
    tpl = tmp_path / "sdk/resources/templates/init/rexglue_cmake.inja"
    tpl.parent.mkdir(parents=True)
    tpl.write_text('find_package(rexglue {{ sdk_version }})\ninclude("{{ cmake_var("DIR") }}/{{ entrypoint_out_dir }}")\n'
                   'add_custom_target({{ names.snake_case }}_codegen)\n')
    out = tmp_path / "generated/rexglue.cmake"
    r = subprocess.run([sys.executable, SCRIPT, "--sdk", str(tmp_path / "sdk"), "--out", str(out)])
    assert r.returncode == 0
    assert out.read_text() == ('find_package(rexglue 0.10.0)\ninclude("${DIR}/generated/default")\n'
                               "add_custom_target(gh3_codegen)\n")


def test_keeps_existing_file(tmp_path):
    out = tmp_path / "rexglue.cmake"
    out.write_text("# do rexglue\n")
    r = subprocess.run([sys.executable, SCRIPT, "--sdk", str(tmp_path / "nada"), "--out", str(out)])
    assert r.returncode == 0 and out.read_text() == "# do rexglue\n"
