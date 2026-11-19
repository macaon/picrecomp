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
tools/venv/bin/pip install UnityPy==1.25.4   # optional: the start screen's per-game backgrounds (tools/picross/art.py)

make hactool                       # the bundled extractor, built with gcc
make -j"$(nproc)"                  # the runtime (and the game library, once the code is generated)
```

## Add your games

Keep your game files and keys outside the checkout; nothing from the game may ever be committed. Each game needs its base
`.nsp` and its update `.nsp` (and your `prod.keys`) in one folder.

```bash
tools/picross/play.sh        # the start screen: choose "+", pick the folder, Import
```

Importing runs seven stages and then two long ones, shown as a step list with a bar: the files are prepared
(`tools/picross/prepare.py`, with the bundled hactool; it works offline and uses the tickets inside the packages), the
game's code is lifted to C and compiled into the game's own library (together about four minutes on 16 threads). The game
then shows as a card with the icon the Switch shows. It needs `python3`, `clang`, `make` and `bash` (the Build section),
and about 1.5 GB of disk per game. The keys are read from the folder, from `~/.switch`, or a file you choose.

The same from a terminal:

```bash
python3 tools/picross/install.py --folder "/path/to/the/game's/folder" --games .picrecomp/runtime/games
```

Each game's name, release date and title ids are in its manifest, `titles/<name>.json`; the importer picks the manifest
from the packages and refuses a game without one. Any version imports: the base game alone, or with any update. Today there are manifests for PICROSS S, S2 to S9, S+, and the licensed games (Kemono Friends, Lord of the Nazarick, the Genesis & Master System and Namco Legendary and Doraemon & F Characters editions, Records of The Shield Hero, Juufuutei Raden's Guide for Pixel Museum). `docs/STATUS.md` says how far each has been played.

The builds below are the ones the importer has been run on (main build ID: the game's executable, from its own header). Each game is listed with its base and the newest update it has been run on; any version in between should import too, since nothing in the generated code is tied to a build.

| Game | Title ID | Version | Main build ID |
|---|---|---|---|
| PICROSS S | `0100ba0003eea000` | 1.0.0 (base) | `acd7476b42407f4ea50c663a99b6db70` |
|  |  | 1.6.2 (update) | `3b86ba97f214ea43929e3a4a0947bf1b` |
| PICROSS S2 | `0100c9600a88e000` | 1.0.0 (base) | `99af6113a64a574695f39af02cdbe6e1` |
|  |  | 1.5.3 (update) | `41f118c8371c79478ee2cdf9ea5038ff` |
| KEMONO FRIENDS PICROSS | `01004b100bda2000` | 1.0.0 (base) | `b3a956031cee684b8a4df42874cb3e00` |
| PICROSS S3 | `010079200d330000` | 1.0.0 (base) | `7570c9e44f5889459d98794425aee552` |
|  |  | 1.5.3 (update) | `7b2a38a86026464c8b4d4d3ffee0aabc` |
| PICROSS LORD OF THE NAZARICK | `010012100e8dc000` | 1.0.0 (base) | `2424aca935dd5f4188d5bafb92a83504` |
| PICROSS S4 | `0100c250115dc000` | 1.0.0 (base) | `9b6fca808b3f1748942eeb59481c1e60` |
|  |  | 1.4.2 (update) | `1d49a7937187f94c884c65b2a6a214fb` |
| PICROSS S5 | `0100ac30133ec000` | 1.0.0 (base) | `836b5804280cda4395fe9844bc726a49` |
|  |  | 1.3.2 (update) | `8437b2f0aaba5c40ac8f3be47070f855` |
| PICROSS S6 | `010025901432a000` | 1.0.0 (base) | `3b27925f9e8e03429986ca7f5a095558` |
|  |  | 1.3.1 (update) | `5e0294a0884a0e4a8ae64a4535214e90` |
| PICROSS S GENESIS & Master System edition | `010089701567a000` | 1.0.0 (base) | `dd82c4e72dadca46838654d03f8c2414` |
|  |  | 1.2.1 (update) | `1466f9109e267e458c517ada6b115e99` |
| PICROSS S7 | `01009b2016104000` | 1.0.0 (base) | `aa4cdc0eab6d4e4db6dd46e996f1ac47` |
|  |  | 1.2.0 (update) | `528b2e6d6d8a5c448471b84d4d54836f` |
| PICROSS S8 | `01002ce017d26000` | 1.0.0 (base) | `8dc7537c47d3ac4f998a5231204d9c0b` |
| PICROSS S9 | `010021a0197ba000` | 1.0.0 (base) | `93d7947d262dc4468eb1ae92b53b11b4` |
| PICROSS S+ | `0100d7b01ae58000` | 1.0.0 (base) | `8ba04068aa05ab45b15e9566fe5452d5` |
|  |  | 1.0.1 (update) | `5eb832c899625df5b6bbe8aea93d073c` |
| PICROSS S NAMCO LEGENDARY edition | `010015101def8000` | 1.0.0 (base) | `94682e93edc1b2f824d0f765d29484a5` |
|  |  | 1.0.1 (update) | `aa44d9a3064ec70be4f8835f3e3f14aa` |
| PICROSS Records of The Shield Hero | `01004c602018a000` | 1.0.0 (base) | `4252758df60a9acb4b226962e97e26e6` |
|  |  | 1.0.2 (update) | `4c1cf04820df7f24166defcac3409e4a` |
| PICROSS S Doraemon & F Characters edition | `01002b901fa74000` | 1.0.0 (base) | `edf8578a6cc16a6185679a63960314bd` |
| Picross: Juufuutei Raden's Guide for Pixel Museum | `01003d2020aba000` | 1.0.0 (base) | `0f6fb55fcdfc915715594dfe8a1bd751` |
|  |  | 1.0.5 (update) | `7cff713af0d89e278142db4fd0279a4b` |


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
| [UnityPy](https://github.com/K0lb3/UnityPy) and its dependencies, such as Pillow (Python, optional, import only: reads a game's own background picture out of its own files) | MIT |
| [Zig](https://ziglang.org) (Windows cross build only) | MIT |
