# PicRecomp

PicRecomp runs the **PICROSS S** series of Nintendo Switch games natively on a PC. It is not an emulator: the game's
ARM64 code is translated to C once, at build time, from your own copy of the game, and compiled into a library the
runtime loads. The runtime replaces the Switch's operating system (Horizon) and its GPU API (NVN), drawing with Vulkan.

This is a fan project. It is not affiliated with or endorsed by Nintendo or Jupiter. It contains no game files, keys or
assets, and it needs a legal copy of the game that you own.

## Status

Work in progress, for developers and testers. There are no packaged releases.

| | |
|---|---|
| Games | **PICROSS S** (title `0100BA0003EEA000`) with its update v524288. Other PICROSS games are planned |
| Platforms | **Linux x86-64** (developed and tested). **Windows**: build files exist (`tools/windows/`), but nobody has built or run it for PICROSS S yet |
| Graphics | Vulkan 1.3 |
| Working | Boots, title screen, puzzle selection, solving a puzzle, completed-puzzle thumbnails, audio, 60 fps in the menus on an RTX 5070 Ti |
| Not verified | Saves across restarts, the other game modes, other GPUs |
| Not done | The system controller-pairing screen (Plus at the main menu) is skipped, and the game returns to its menu |

## Requirements

- An x86-64 CPU with AVX2 and FMA (2013 or newer) and a Vulkan 1.3 GPU driver
- Your own PICROSS S base game and its update as `.nsp` files, and the `prod.keys` from your Switch
- About 2 GB of free disk space for the generated code and the build, and some patience: the first build compiles about
  170,000 generated functions

## Build

### Fedora

```bash
sudo dnf install clang lld make gcc pkgconf-pkg-config python3 python3-pip \
    SDL3-devel libavcodec-free-devel libavformat-free-devel libavutil-free-devel libswscale-free-devel \
    vulkan-loader-devel libshaderc-devel VulkanMemoryAllocator-devel
```

These are the packages the build was verified against on Fedora 44. You also need a Vulkan driver (`mesa-vulkan-drivers`
for AMD / Intel, or NVIDIA's). Install `gdb` if you want to debug a hang or a crash.

### Ubuntu / Debian

```bash
sudo apt install clang lld make gcc pkg-config python3-venv python3-pip libsdl3-dev libavformat-dev \
    libavcodec-dev libavutil-dev libswscale-dev libvulkan-dev libshaderc-dev libvulkan-memory-allocator-dev
```

SDL3 3.2 or newer is required. These package names have not been tested by the maintainer.

### All distributions

```bash
python3 -m venv tools/venv
tools/venv/bin/pip install lz4 capstone unicorn

make hactool                       # the bundled extractor, built with gcc
make -j"$(nproc)"                  # the runtime (and the game library, once the code is generated)
```

## Prepare your game

Keep your game files and keys outside the checkout; nothing from the game may ever be committed.

```bash
mkdir -p .picrecomp
python3 tools/picross/prepare.py "/path/to/base.nsp" "/path/to/update.nsp" \
    .picrecomp/prepared-game --prod-keys "/path/to/prod.keys"
```

This applies the update and writes `exefs/` and `romfs.bin` into `.picrecomp/prepared-game`. It works offline and uses
the tickets inside the packages. `tools/picross/launcher.sh` does the same from a small window (needs Python Tk,
`python3-tkinter` on Fedora, `python3-tk` on Debian).

You can also add a game from the start screen: choose "+", pick the folder with the base and update `.nsp` files (the
keys are read from there, from `~/.switch`, or a file you choose), and Import. It needs `python3` on the PATH and shows
each step as it runs. Either way, games prepared into `.picrecomp/runtime/games/<name>` appear as cards on the start screen, with the
icon the Switch shows. Each game's manifest is in `titles/`; `prepare.py` picks it from the package, so a PICROSS game
with a manifest needs no other setting. Only PICROSS S has been generated, built and run so far.

Then generate the C code from the game's executables, build the game library, and play. Both steps check the game's
build ID and refuse an unsupported version.

```bash
tools/picross/generate.sh .picrecomp/prepared-game      # a minute or two; writes generated/ (never committed)
make -j"$(nproc)"                                       # compiles generated/ into build/picgame.so (long)
tools/picross/play.sh .picrecomp/prepared-game
```

Settings, saves and shader caches go to `.picrecomp/runtime`. Set `PICRECOMP_DATA_ROOT` to use another folder.

## Controls

The defaults are keyboard and gamepad. Both can be remapped in the in-game menu (F1, Controls tab).

| Switch | Keyboard |
|---|---|
| A (confirm) | K |
| B (back) | J |
| X / Y | L / I |
| D-pad | Arrow keys |
| Left stick | W A S D |
| L / R | Q / E |
| ZL / ZR | 1 / 3 |
| Plus / Minus | Enter / Backspace |

Plus at the main menu opens the system's controller screen on a Switch. PicRecomp skips it: pair your controller on the
PC, and use the Controls tab to change the bindings.

## Troubleshooting

- **The window opens and nothing moves**: run it from a terminal and read the output. From a terminal the log goes to the
  terminal; when stderr is redirected it goes to `.picrecomp/runtime/picrecomp.log`.
- **A hang or a crash**: a crash prints a backtrace. For a hang, attach with
  `gdb -batch -p $(pgrep -n pic-main) -ex 'thread apply all bt 12'`. `PICRECOMP_PROF_SVC=1` logs where the game
  creates the runtime's events, which finds waits that nothing ever signals.
- **`make` stops with `clang: No such file or directory`**: install `clang` and `lld`.

More detail on how it works is in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); the current state and the known gaps are in
[docs/STATUS.md](docs/STATUS.md). Windows build notes are in [docs/BUILD-windows.md](docs/BUILD-windows.md).

## Credits and licenses

PicRecomp is a fork of [EoWRecomp](https://github.com/Nycolazs/EoWRecomp), a recompilation of *The Legend of Zelda: Echoes
of Wisdom*, which is itself a fork of [LASRecomp](https://github.com/peiikai/LASRecomp) by Peiikai, a recompilation of
*The Legend of Zelda: Link's Awakening*. The ARM64-to-C lifter, the Horizon and NVN runtime and the Maxwell shader
translator all descend from their work. PicRecomp was forked from EoWRecomp at commit `dd9c79d`. Thank you to both
projects and their contributors.

PicRecomp is released under the [MIT License](LICENSE), which carries the copyright notices of those projects. It does
not cover PICROSS S or any other game. Third-party components keep their own licenses:

| Component | License |
|---|---|
| [hactool](https://github.com/SciresM/hactool) (`third_party/hactool`), with its bundled mbed TLS | ISC; Apache-2.0 |
| [Dear ImGui](https://github.com/ocornut/imgui) (`third_party/imgui`) | MIT |
| [SMAA](https://github.com/iryoku/smaa) (`third_party/smaa`) | MIT |
| DejaVu Sans (`third_party/fonts/DejaVu`) | Bitstream Vera license, public-domain changes |
| Baloo 2 (`third_party/fonts/Baloo2`) | SIL Open Font License 1.1 |
| [SDL3](https://libsdl.org) | zlib |
| [FFmpeg](https://ffmpeg.org) (system library) | LGPL 2.1 or later |
| Vulkan loader and headers, [shaderc](https://github.com/google/shaderc) | Apache-2.0 |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | MIT |
| [Capstone](https://www.capstone-engine.org), [LZ4](https://github.com/lz4/lz4) (Python, build tools only) | BSD |
| [Unicorn](https://www.unicorn-engine.org) (Python, build tools only) | GPL-2.0 |
| [Zig](https://ziglang.org) (Windows cross build only) | MIT |
