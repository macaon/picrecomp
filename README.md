# PicRecomp

PicRecomp is a work-in-progress native Linux recompilation of **PICROSS S** for Nintendo Switch. It uses the ARM64-to-C recompiler and Switch runtime from [EoWRecomp](https://github.com/Nycolazs/EoWRecomp), with changes for PICROSS S boot, exception handling, synchronization, and graphics.

## Status

The current prototype targets **Linux x86-64** and **PICROSS S update v524288**. The title screen, puzzle selection, cell fills and X marks, completed puzzle thumbnails, and audio have been exercised. Only PICROSS S has been tested. The project is for developer testing and has no packaged releases.

The inherited EoWRecomp start screen includes package selection, game setup, and settings. PicRecomp's tested path starts from pre-extracted game files and bypasses that screen. Package selection and automatic setup still need to be adapted and verified for PICROSS S. The runtime also inherits an in-game settings overlay, including input bindings and master volume.

## Build from source

You need an x86-64 Linux system with AVX2 and FMA, a Vulkan driver, GNU Make, Python with Capstone, and a Clang/LLD toolchain with support for the generated code's tail-call attributes. The runtime links against SDL3, Vulkan, shaderc and FFmpeg and uses the Vulkan Memory Allocator headers. The inherited `tools/linux/dev_deps.sh` provides a Debian/Ubuntu dependency setup.

Prepare a directory from your own PICROSS S base game and update. It must contain the **updated** `exefs/main`, `exefs/main.npdm`, `exefs/subsdk0`, and `exefs/sdk`, plus the matching `romfs.bin` image. Prepare the matching base/update data with a tool that supports Switch update overlays. The generator checks the updated main executable's build ID before starting. Keep your game files and keys outside the checkout.

```bash
python3 -m venv tools/venv
tools/venv/bin/python -m pip install capstone
tools/picross/generate.sh /absolute/path/to/prepared-game

# Use the dependency prefix from tools/linux/dev_deps.sh when applicable.
PATH="$PWD/build/bin:$PATH" make -j"$(nproc)" \
  DEPS=build/deps-linux GENARCH=-mcpu=x86_64_v3

tools/picross/play.sh /absolute/path/to/prepared-game
```

For system-installed dependencies, omit `DEPS` and provide compiler include and library flags as needed. With Clang instead of Zig, use `GENARCH=-march=x86-64-v3`. When using the bundled dependency prefix, export `LD_LIBRARY_PATH="$PWD/build/deps-linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"` before launching. The first build generates hundreds of megabytes of C and can take several minutes. Run it from disk if your system mounts `/tmp` as tmpfs.

The launcher stores settings and saves under `.picrecomp/runtime` unless `EOWRECOMP_DATA_ROOT` is set. Audio is enabled by default. The inherited keyboard defaults use the arrow keys for movement, **K** for Switch A, and **J** for Switch B.

## Source and credits

Forked from EoWRecomp at commit `dd9c79d`. See the [upstream README](README.upstream.md) and [license](LICENSE) for attribution and upstream documentation. PicRecomp's game-specific lifting patches live under `tools/picross` and run locally against the supported executable. Game packages, keys, generated game code, linked game libraries, and personal saves stay local.
