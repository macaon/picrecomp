# PicRecomp

PicRecomp runs Nintendo Switch **PICROSS** and **Piczle** games natively on a PC. It is not an emulator: the game's
ARM64 code is translated to C once, at build time, from your own copy of the game, and compiled into a library the
runtime loads. The runtime replaces the Switch's operating system (Horizon) and its GPU API (NVN), drawing with Vulkan.

This is a fan project. It is not affiliated with or endorsed by Nintendo or Jupiter. It contains no game files, keys or
assets, and it needs a legal copy of the game that you own.

Each title is independent: owning only S9 or one Piczle game is sufficient to use that title. PICROSS S is not required.
The importer builds from the selected game's own executable and SDK modules; the runtime loads that game's assets and saves.

## Status

Work in progress, for developers and testers. There are no packaged releases.

| | |
|---|---|
| Games | **23 title manifests**: 20 PICROSS games, Piczle Cross: Story of Seasons, Piczle Cross: Rune Factory, and Piczle Puzzle & Watch Collection |
| Platforms | **Linux x86-64** (developed and tested). **Windows**: build files exist (`tools/windows/`), but nobody has built or run it yet |
| Graphics | Vulkan 1.3 |
| Verified | PICROSS S: puzzle selection, solving a puzzle, completed-puzzle thumbnails and audio. Saves kept separately for S/S2/S3. Other PICROSS titles have title/menu/puzzle-selection smoke tests; Piczle coverage is described below |
| Not verified | Saves across restarts in every game, every game mode, AMD and Intel GPUs (the shaders need 32-wide subgroups, which the runtime asks for; testers welcome: look for `[vk] subgroups:` in the log) |
| Known limitations | The system controller-pairing screen is skipped. Piczle gameplay coverage is limited; Rune Factory still skips two unsupported compute shaders. See the per-title notes |

Story of Seasons has reached gameplay; Rune Factory has reached its tutorial and a rendered puzzle board; Puzzle & Watch
has reached its main menu. Puzzle completion and save/reload have not been verified across these three games. Most recorded
menu runs reach about 60 fps on the RTX 5070 Ti; startup compilation and Vulkan validation can be slower. See
[docs/STATUS.md](docs/STATUS.md) for the evidence and limitations per title.

## Requirements

- An x86-64 CPU with AVX2 and FMA (2013 or newer) and a Vulkan 1.3 GPU driver
- Your own copy of the game as `.nsp` files (the base game; an update is optional), and the `prod.keys` from your Switch
- Disk space for each game's assets, generated code and build. Installed sizes here range from 375 MB for PICROSS S to
  1.8 GB for Rune Factory, with additional temporary space needed during import. A PICROSS S build compiles about
  170,000 generated functions; larger games take longer

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
`.nsp`, optionally its update `.nsp`, and your `prod.keys` in one folder.

```bash
tools/picross/play.sh        # the start screen: choose "+", pick the folder, Import
```

Importing runs seven stages and then two long ones, shown as a step list with a bar: the files are prepared
(`tools/picross/prepare.py`, with the bundled hactool; it works offline and uses the tickets inside the packages), the
game's code is lifted to C and compiled into the game's own library (about four minutes for PICROSS S on 16 threads;
recent Piczle imports took 9–11 minutes). The game
then shows as a card with the icon the Switch shows. It needs `python3`, `clang`, `make` and `bash` (the Build section),
and temporary disk space beyond the installed sizes above. The keys are read from the folder, from `~/.switch`, or a file you choose.

The same from a terminal:

```bash
python3 tools/picross/install.py --folder "/path/to/the/game's/folder" --games .picrecomp/runtime/games
```

Each game's name, release date and title ids are in its manifest, `titles/<name>.json`; the importer picks the manifest
from the packages and refuses a game without one. The importer accepts a base game alone or a base game with an update;
untested versions may still expose runtime gaps. The table below lists all 23 titles with manifests. The three Piczle games
use Unreal Engine 4 and are not by Jupiter. `docs/STATUS.md` says how far each has been played.

The builds below are recorded imports (main build ID: the game's executable, from its own header). A listed build does
not imply a completed puzzle or full compatibility. Puzzle & Watch has only its tested update listed. Other versions
need their own checks; the importer generates code from the selected executable.

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
| PICROSS X : PICBITS VS UZBOROSS | `0100f08018840000` | 1.0 (base) | `40e9866d26170441d914ea4811003d9f` |
|  |  | 1.1.0 (update) | `4f0f49e2051b074b56fb00d7a596a592` |
| PICROSS S9 | `010021a0197ba000` | 1.0.0 (base) | `93d7947d262dc4468eb1ae92b53b11b4` |
| PICROSS S+ | `0100d7b01ae58000` | 1.0.0 (base) | `8ba04068aa05ab45b15e9566fe5452d5` |
|  |  | 1.0.1 (update) | `5eb832c899625df5b6bbe8aea93d073c` |
| Piczle Cross: Story of Seasons | `01003f201c5ba000` | 1.0 (base) | `156d5f95f89b42b1d432b278bb82afe8` |
|  |  | 1.021 (update) | `b3630600658657938ae9a69e2b701bb5` |
| PICROSS S NAMCO LEGENDARY edition | `010015101def8000` | 1.0.0 (base) | `94682e93edc1b2f824d0f765d29484a5` |
|  |  | 1.0.1 (update) | `aa44d9a3064ec70be4f8835f3e3f14aa` |
| PICROSS Records of The Shield Hero | `01004c602018a000` | 1.0.0 (base) | `4252758df60a9acb4b226962e97e26e6` |
|  |  | 1.0.2 (update) | `4c1cf04820df7f24166defcac3409e4a` |
| PICROSS S Doraemon & F Characters edition | `01002b901fa74000` | 1.0.0 (base) | `edf8578a6cc16a6185679a63960314bd` |
| PICROSS S CAPCOM CLASSICS edition | `01009ad023f90000` | 1.0.0 (base) | `1de3e8003bb4a76aa7fa308099cbd4d5` |
|  |  | 1.0.2 (update) | `e212a43e76af2f1462a7eaa02e14680e` |
| PICROSS S SNK CLASSICS & NEOGEO edition | `0100122023706000` | 1.0.0 (base) | `54fc92b03b3b30748bbc9bab4676dc88` |
|  |  | 1.0.2 (update) | `7edb919a39cb182c5bc2f4a8d36991d9` |
| Picross: Juufuutei Raden's Guide for Pixel Museum | `01003d2020aba000` | 1.0.0 (base) | `0f6fb55fcdfc915715594dfe8a1bd751` |
|  |  | 1.0.5 (update) | `7cff713af0d89e278142db4fd0279a4b` |
| Piczle Cross: Rune Factory | `0100147020984000` | 1.0 (base) | `1ad16d8417120786b850f60a59732f18` |
|  |  | 1.04 (update) | `1ad16d8417120786b850f60a59732f18` |
| Piczle Puzzle & Watch Collection | `01001a30155b6000` | 1.01 (update) | `02dd54e917f1f763ddec54711f23cc36` |


Settings, saves and shader caches go to `.picrecomp/runtime`. Set `PICRECOMP_DATA_ROOT` to use another folder.

### Running the Piczle games

Launch the Piczle games from their launcher cards, or start an imported title directly. No experimental startup flags are required:

```bash
tools/picross/play.sh .picrecomp/runtime/games/piczle-cross-story-of-seasons
```

Use `piczle-cross-rune-factory` or `piczle-puzzle-watch-collection` as the folder name for the other two. The runtime now preserves
already-mapped allocator pages and reports the texture limits the games need. It keeps each title's declared system resource
size during startup. All three Piczle games were tested switching back to PICROSS S; S → Story of Seasons was also checked.
Other title pairs remain unverified. The old `PICRECOMP_TEMP_DI` and `PICRECOMP_TEMP_KEEP_SRS` experiments have been removed.

Shader compilation policy is automatic: PICROSS waits for shaders so one-time texture draws are preserved; the Piczle
manifests enable asynchronous compilation to avoid UE4's startup watchdog. A cold Piczle launch may show black or incomplete
frames while shaders compile. `PICRECOMP_ASYNC_SHADERS=0` forces synchronous compilation; `=1` forces asynchronous compilation.
Use those overrides for diagnosis: forcing asynchronous compilation in PICROSS can leave puzzle previews blank, while
forcing synchronous compilation in Piczle can trigger the watchdog. Rebuilding generated game libraries is unnecessary for
these runtime shader fixes.

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
