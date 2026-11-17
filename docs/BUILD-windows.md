# Windows

**Status: untested.** PicRecomp is developed and tested on Linux. The Windows pieces inherited from its base project are
still in the tree, but nobody has built or run PicRecomp on Windows, and some of the workflow is Linux-only today.
Treat this page as a starting point for a tester, not as a guide that is known to work.

## What exists

- The Makefile has a Windows branch (`PLATFORM=windows`): `picrecomp.exe` with the Vulkan backend, and the game library
  `picgame.dll`, linked against an import library made from `tools/windows/picrecomp.def` (the runtime functions the
  game code calls, exported by the `.exe`).
- `runtime/platform/windows.c`: address-space reservation for the guest window (`VirtualAlloc`, committed per region,
  with a vectored exception handler for first touches), crash handler and backtrace.
- `tools/windows/build.sh`: cross-build from Linux with Zig (`WINCROSS=1`): fetches MSYS2 UCRT64 and FFmpeg dependencies
  (`tools/windows/fetch_deps.sh`), builds `build/win/picrecomp.exe` and, when `generated/` exists, `picgame.dll`, and
  gathers the DLLs into `build/win/dist/PicRecomp/`. It needs Zig 0.17 (`tools/fetch_zig.sh`).
- `tools/setup_windows.ps1` and `tools/build_deps_windows.sh`: a native MSYS2 setup. Unverified for PICROSS S.

## What is missing

- `tools/picross/generate.sh`, `play.sh` and `launcher.sh` are bash scripts that assume Linux paths (`tools/venv/bin/python`)
  and `build/picrecomp`. They need Windows equivalents.
- Nothing has confirmed that the generated code (identical to Linux's) links and runs against the Windows runtime.

## If you want to try

The generated C is portable (x86-64, `--simd c`), so one way is to generate it on Linux and cross-build:

```bash
tools/picross/generate.sh /path/to/prepared-game     # on Linux
tools/windows/build.sh                                # needs zig; writes build/win/dist/PicRecomp/
```

then copy `build/win/dist/PicRecomp/` and the prepared game folder to a Windows machine, and run
`picrecomp.exe --play <prepared-game-folder>`. Please report what happens, with the log: on Windows the release
build writes `picrecomp.log` next to the `.exe`.

## Tests that apply

- `tools/test_translate.sh`: shader translator fuzzing.
- `tools/test_vk_pipelines.sh`: fuzzed shaders through the backend into real pipelines.
- `PICRECOMP_VK_VALIDATION=1` with `VK_ADD_LAYER_PATH` pointing at the validation layer's folder: Khronos validation.
