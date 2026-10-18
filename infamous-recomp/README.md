# infamous-recomp

The inFamous-specific half of the port, built on top of `../ps3recomp`.

| File | Contents |
|---|---|
| `main.cpp` | Program entry: memory setup, window, runtime start |
| `stubs.cpp` | Game-specific replacements for recompiled functions and system calls |
| `infamous_clock_probe.h`, `car_v233.h` | Frame pacing and streaming / pop-in options |
| `spu/jobmods_extra_map.json`, `spu/jobmod_*/lodx_*.h` | Hand-written additions to the lifted SPU job modules |
| `tools` | Asset tools run by the installer: dialog font, keyboard/mouse prompt glyphs |
| `config.toml` | Recompiler configuration |

`recompiled/`, `spu/` and `spu-images/` are generated from your own game by the
installer (see `../installer`) and are never part of this repository.

## Rebuild

After an install, `build.cmd` rebuilds into `build-fast` with Visual Studio
Build Tools, Ninja and clang-cl. It uses `python` from your PATH, or the one in
the `PYTHON` environment variable.
