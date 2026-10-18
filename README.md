<h1 align=center>Guitar Hero III: Legends of Rock — Nintendo Switch port (gh3-nx)</h1>

An unofficial native Nintendo Switch port of the Xbox 360 version of **Guitar Hero III: Legends of Rock** (2007).

It's not an emulator. The PowerPC code of the game's `default.xex` is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch (ARM64), and the game is drawn by a
native Vulkan renderer running on NVK (Mesa), adapted from [project8-sw](https://github.com/R-drg/project8-sw)
(same Neversoft engine). The same code also runs on macOS (Apple Silicon) through MoltenVK, which is where every
change is tested first.

> [!IMPORTANT]
> **No game files are in this repository, and none may be added.** You need your own Xbox 360 disc of the game.
> The C++ generated from the game's executable (`generated/`), the extracted disc (`extracted/`) and anything built
> from them stay on your machine. The Switch package you build is for **personal use only** — never publish it.

## Status

| Milestone | State |
|---|---|
| **A — macOS, emulated GPU** | ✅ boots, plays full songs with correct audio (~5 FPS) |
| **C — Switch + native renderer** | ✅ boots in title takeover, plays full songs, HUD and 3D stage correct |
| **D — Performance** | 🔄 in progress: ~45 FPS at stock clocks on Switch, 60 is the goal |

Measured on 2026-10-17/18:

| Platform | Renderer | Gameplay |
|---|---|---|
| macOS (M1 Pro) | emulated Xenos GPU | ~5 FPS (reported) |
| macOS (M1 Pro) | **native** | **38–53 swaps/s** (measured from the renderer log) |
| Switch (Mariko, stock clocks, handheld) | native, full effects | ~34 game FPS (median), frequent >50 ms frames |
| Switch (Mariko, stock clocks, handheld) | native, **performance mode** (default) | **~45 game FPS** (median), >50 ms frames 46–108 → 8 per 10 s |

On the Switch the game is **GPU-bound**. The default `gh3.toml` turns on a performance mode that skips the shadow-map
and bloom passes (`gh3_native_skip_shadows`, `gh3_native_skip_bloom`); this also removes the over-bright look the
native renderer had. Set both to `false` to get the effects back. Next steps (3D at a lower resolution than the
HUD, overlapped present) are in [`docs/porting-playbook.md`](docs/porting-playbook.md) §8.

Known issues: resizing the macOS window turns it black (MoltenVK drawable stall).

## How it was built — in one day, with an agent harness

The whole port (from "nothing" to a Switch NRO) was driven by an autonomous agent harness: short `claude -p`
sessions, one per task, each gated by a **binary acceptance command** that the script — not the model — judges
(test suite, boot test counting presented frames, frame captures compared against the emulated renderer, skipped-draw
counters). Everything learned along the way is in [`docs/porting-playbook.md`](docs/porting-playbook.md), written to
make the next 360 → Switch port faster. Highlights:

- **Rebuild loop 6 min → 7.5 s** by splitting ReXGlue's 60k-call registration function (`tools/split_register.py`).
- **Missing indirect-call targets found statically** (`tools/find_missing_funcs.py`: decrypted image dump + code
  gaps referenced by pointers), validated against the ones found by crashing.
- **Black screen on macOS** traced to MoltenVK's immediate present mode stalling in `CAMetalLayer nextDrawable`.
- **Native renderer without geometry shaders** (Metal has none): rect/quad/point lists expanded in the vertex
  shader — skipped draws ~60k/10 s → 0.
- **White 3D stage** fixed: the game draws its 4× MSAA stage into EDRAM that it then reuses as a 1× target of twice
  the pitch; the native renderer now draws the samples into that shared target.
- **Scripted input + frame capture** reach gameplay unattended and compare native vs emulated frames.
- **Switch profiling over FTP**: the SDK profiler plus symbolized stack samples showed the threads mostly *waiting*
  on the GPU, not computing — so PGO and codegen tweaks gave nothing measurable, while cutting two GPU passes gave
  +32% FPS.

## Building

Requirements: macOS on Apple Silicon (or Linux for the Switch parts), CMake ≥ 3.25, Ninja, Python 3, Docker
(Switch toolchain), and your own copy of the game, extracted to `extracted/` (`default.xex` + `DATA/`).

```bash
python3 -m venv .venv && .venv/bin/pip install pytest
tools/setup_sdk.sh             # ReXGlue v0.10.0 (f5337cd) + project8-sw's 16 patches → sdk/
tools/build_host_sdk.sh        # SDK + codegen + macOS build → out/build/mac-arm64-p8sdk
.venv/bin/python -m pytest -q tests
python3 tools/boot_test.py --frames 300 --timeout 180
./run-gh3.command --gh3_renderer=native   # play on macOS (1–5 frets, arrows strum, Enter = Start)
```

Switch NRO (details and timings in the playbook §1):

```bash
docker build --platform linux/amd64 -t gh3-switch-clang docker/switch
tools/build_mesa.sh                       # NVK driver → out/driver-sdk
python3 tools/make_icon.py                # optional: icon from your own game's main menu → out/switch/icon.jpg
docker run --rm --platform linux/amd64 -v "$PWD":/project -w /project gh3-switch-clang sh -c \
  'cmake -S . -B build/switch -G Ninja -DCMAKE_BUILD_TYPE=Release -DGH3_SWITCH=ON \
     -DCMAKE_TOOLCHAIN_FILE=/project/cmake/switch-rexglue-gcc.cmake -DREXSDK_DIR=/project/sdk \
     -DREXGLUE_SWITCH_NVK_SDK=/project/out/driver-sdk && cmake --build build/switch -j 6'
tools/make_sd_package.sh --nro build/switch/gh3.nro   # → out/sd/switch/gh3-nx/
```

## Installing on the Switch

```text
sdmc:/switch/gh3-nx/
  gh3-nx.nro        (from out/sd)
  gh3.toml          (settings)
  LICENSES.txt
  game/             (your extracted disc: default.xex, DATA/)
```

Launch in **title takeover**: hold **R** while starting any installed game, then open the NRO from the Homebrew
Menu. Applet mode (Album) is not supported. At start-up the game checks the SHA-256 of `game/default.xex`; other
releases are refused, because the code in the NRO was compiled from that exact executable.

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) — the Xbox 360 static recompiler and runtime.
- [project8-sw](https://github.com/R-drg/project8-sw) by R-drg — SDK patch series, Switch toolchain, native renderer,
  dump gate, scripted input and parity capture (GPL-3.0).
- [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) by StevensND — the design of the native renderer and the
  optimization ladder this port follows (GPL-3.0).
- [mesa-switch](https://github.com/danfromtico/mesa-switch) — NVK on Horizon.

See [`NOTICE`](NOTICE) for every file taken from those projects and what was changed.

## License

GPL-3.0 (see [`LICENSE`](LICENSE)). Guitar Hero III and related marks belong to their owners, who have no
relation to this project.
