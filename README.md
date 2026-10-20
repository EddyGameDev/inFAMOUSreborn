# inFamous PC

An unofficial native Windows port of **inFamous** (PlayStation 3, 2009, European
release `BCES00220`). The game's PowerPC (PPU) and SPU code is statically
recompiled to x86-64 C/C++ with [ps3recomp](https://github.com/sp00nznet/ps3recomp),
so it runs as a normal Windows program rather than under an emulator.

**This repository contains no game code or data.** You build the game yourself,
on your own PC, from your own copy of the game.

## Easy install

Download **inFamous Setup.exe** from the latest release, run it, choose your
`.iso` (an encrypted image works with its `.dkey` next to it) or your game
folder (the one with `PS3_GAME` in it), pick an install location and press
**Install**.

Setup checks for Python and the C++ build tools and offers to install them,
copies the game out of the disc image, regenerates the recompiled code from your
copy, builds it and adds an inFamous shortcut. Running Setup again on the same
folder updates or repairs the install and only redoes what changed.

## Updating

When a new version is published here, players update in place instead of reinstalling:
**Update now** in Setup (it finds the existing install), the **Update inFamous** shortcut in
the Start menu, or `Update inFamous.cmd` in the install folder. The updater compares every file
with this repository, downloads only what changed, regenerates the game code only if the
port's patches changed, and recompiles only the affected parts. Installs made with an older
Setup need one run of the new Setup on the same folder first.

## Requirements

- Your own inFamous disc dump, European release (`BCES00220`).
- Windows 10 or 11 (64-bit) and a GPU with Direct3D 12 support.
- A CPU with SSE4.1 (2008 or newer). On CPUs without AVX2 (Intel before 2013, Pentium/Celeron
  without AVX, AMD before 2015) Setup builds a slower compatibility version automatically.
  On PCs with 16 or fewer CPU threads the game uses fewer threads and less CPU (tested down to 4 threads).
- [Python 3.10+](https://www.python.org/downloads/) with "Add python.exe to
  PATH" ticked.
- [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/)
  with the **Desktop development with C++** workload and the **C++ Clang
  Compiler for Windows** component.
- [RPCS3](https://rpcs3.net), only if your `PS3_GAME\USRDIR` has `EBOOT.BIN`
  but no decrypted `EBOOT.elf`. Setup uses it to decrypt the executable.
- About 10 GB of free disk space and 16 GB of RAM.

## Building manually

```bat
cd installer
cargo build --release
target\release\infamous-installer --game "D:\Dumps\inFamous"
```

`infamous-installer doctor` checks the requirements only. Finished steps are
skipped when you run it again, so an interrupted install continues where it
stopped. See [installer/README.md](installer/README.md) for every step and the
maintainer commands.

## Playing

Run **Play inFamous** (or the inFamous shortcut Setup made). Controllers work as
on the console, and keyboard and mouse are supported. F10 opens the settings
menu for vsync, frame cap, resolution and more (saved to `settings.ini`).

## How it works

| Path | Contents |
|---|---|
| `ps3recomp` | The recompiler toolkit and runtime: PPU and SPU lifters, HLE system libraries, RSX on Direct3D 12, audio and input |
| `infamous-recomp` | The inFamous-specific parts: program entry, game stubs and fixes, asset tools |
| `installer` | Setup and the command-line installer: reads the disc, regenerates the code from your game, applies `installer/patches`, builds |
| `installer/infamous.toml` | Everything about the game and the port: pipeline steps, generated files, build and launch settings |
| `installer/generated.lock` | Hashes of each generated file before and after the port's patches |

## Credits

- [ps3recomp](https://github.com/sp00nznet/ps3recomp) by sp00nznet and
  contributors (see [ps3recomp/CONTRIBUTORS.md](ps3recomp/CONTRIBUTORS.md)).
- [RPCS3](https://rpcs3.net), used to decrypt the game executable.
- Sucker Punch Productions, for the game.
