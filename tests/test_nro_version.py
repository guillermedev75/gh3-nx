import re

# A versão do NRO (Homebrew Menu) era fixa em 0.1.0 desde o primeiro build. Agora <marco>-<commit>
# (ex. 0.5.0-0d52936): cada build no console diz de qual código veio.


def test_nro_version_is_milestone_plus_commit():
    cmake = open("cmake/switch.cmake").read()
    m = re.search(r'set\(GH3_VERSION "(\d+)\.(\d+)\.(\d+)"\)', cmake)
    assert m and (int(m[1]), int(m[2])) >= (0, 5)
    assert 'set(GH3_NRO_VERSION "${GH3_VERSION}-${GH3_GIT_SHA}")' in cmake
    assert "$ENV{GH3_GIT_SHA}" in cmake          # o Docker não enxerga o git da worktree
    assert "unset(GH3_NRO_VERSION CACHE)" in cmake  # o 0.1.0 antigo não volta pelo cache
    assert 'VERSION "${GH3_NRO_VERSION}"' in cmake


def test_version_fits_the_nacp_field():
    # NACP: display version tem 16 bytes (com o terminador): "0.5.0-" + 7 do commit = 13
    assert len("99.99.99-0123456") <= 16


def test_docs_pass_the_commit_into_the_docker_build():
    for doc in ("README.md", "AGENTS.md"):
        assert "GH3_GIT_SHA=$(git rev-parse --short=7 HEAD)" in open(doc).read(), doc
