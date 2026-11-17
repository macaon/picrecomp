# Architecture

PicRecomp is a static recompiler plus a runtime. At build time the game's ARM64 code is lifted to C and compiled into a
shared library. At run time a host program loads the game's executables into memory, runs the recompiled code, and
answers everything the game asks of the Switch: kernel calls, system services, and GPU commands.

It descends from LASRecomp through EoWRecomp (see the credits in the README). This document describes what the code does
today. Where a number comes from a measurement, the tool that produces it is named.

## 1. Pipeline

```
base .nsp + update .nsp + prod.keys ──tools/picross/prepare.py (hactool)──> exefs/ (NSOs) + romfs.bin
exefs/<module> ──tools/gen.py (tools/picross/generate.sh)──> generated/<module>/*.c ──clang──> picgame.so
runtime (picrecomp) ── loads the NSOs into guest memory, relocates, dlopen(picgame.so) ── nn::init::Start
```

The generated code is never committed and never edited by hand. Changes go in `tools/lifter.py`, `tools/gen.py` or
`tools/patches.py`.

### The recompiler (Python, `tools/`)

- **`nso.py`** reads an NSO: the header, the three LZ4 segments, MOD0, `.dynamic`, the symbol table (via `DT_HASH`) and
  the RELA / JMPREL relocations.
- **`analyze.py`** finds functions: `.eh_frame_hdr` (a binary-search table), `STT_FUNC` symbols, `R_AARCH64_RELATIVE`
  relocation targets that land in `.text` (vtables, callbacks), and `BL` targets. `--census` counts instructions.
- **`gen.py`** adds `ADR` and `ADRP`+`ADD` targets, then iterates to a fixed point: every branch or call that leaves the
  current function becomes a new boundary and splits it. It writes **chunks** (`<module>_NNN.c`, up to 1500 functions or
  about 1.5 MB of C each; a very large function gets its own file) plus `<module>_table.c` (sorted addresses and
  pointers). Each function is `void f_<address>(Cpu *c)`. Direct calls are C calls, jumps out of a function are
  `musttail`, indirect ones go through `rt_dispatch`. Unchanged chunks are not rewritten, so rebuilds are incremental.
  - **Jump tables** (`br xN`) are resolved three ways. Bounds-checked tables are resolved by emulating the code with
    Unicorn for every index (a `b.hi` / `b.hs` guard in the fall-through form, `b.ls` / `b.lo` in the taken form). LLVM's
    *compact* tables (`adr cases ; ldrb [table, idx] ; add lsl #2 ; br`) are read straight from the image, with the entry
    count taken from a guard anywhere earlier in the function or from an `and idx, x, #2^k-1` mask
    (`_jump_table_compact`; `tools/test_jumptable.py`). A table that none of these resolves gets a `switch` over every
    instruction of the function (`L_dispatch`) as a slow fallback.
- **`lifter.py`** lifts AArch64 to C: integers, loads and stores, NZCV flags, the exclusive and atomic instructions
  (LDXR / STXR, LDXP / STXP, CAS / CASP and the LSE set, checked against Unicorn and, for the atomics, against the host
  CPU). FP, SIMD and CRC32 become inline asm on an ARM64 host (`--simd asm`) or portable C (`--simd c`: `simd_c.py`,
  `runtime/simd.h`, `simd_fp.h`; needs AVX2 and FMA, `-march=x86-64-v3`). A guest address is a host address, so
  `LD64(a)` is a direct pointer dereference.
- **`patches.py`** replaces single instructions by address with C. Every patch first checks the original instruction
  word (`expect()`), so a wrong address or a different game build fails the generation loudly. It is a mechanism only;
  there are no per-game entries yet. `tools/picross/fix-exception-resume.py` is the one post-generation patch that
  remains (landing pads for an exception unwind path); it finds the generated chunks it edits by their content.

### The game code is a separate library

The generated code is compiled into `picgame.so` (`.dll` on Windows), which the runtime loads (`runtime/gamelib.c`; the
entry point is `tools/gamelib_entry.c`). The library's id is a hash of the generator, the headers it is compiled against
and the flags; a library with another id is never loaded. The executable contains no game code.

## 2. The runtime (`runtime/`)

- **Address space** (`rt.h`, `modules.h`): a fixed window starting at `0x7000000000` is reserved when the process starts.
  The modules load at `main` `0x7000000000`, `subsdk0` `0x7008000000` and `sdk` `0x700C000000`, followed by TLS, the HLE
  stubs, the alias region and the game heap.
- **`loader.c`**: loads the modules, decompresses them and applies relocations. An import is resolved first against host
  implementations (`HLE_FUNC`, by symbol name), then against the other modules' symbols. What is left becomes an
  "UNIMPLEMENTED" stub that returns 0 and logs.
- **`main.c`**: does not run the system's `rtld`. It calls `nn::init::Start` in the `sdk` module directly and passes
  callbacks that run each module's `DT_INIT` and `init_array`, as the `rtld` would. The main thread's priority and stack
  size come from `main.npdm`.
- **`dispatch.c`**: guest address to recompiled function (binary search over two tables plus a per-thread direct-mapped
  cache), HLE registration, per-address overrides, `guest_call`, backtraces and debug watchpoints.
- **`kernel.c`**: the supervisor calls. Threads are pthreads (priority and core are only recorded), the Horizon mutex and
  condition variable (`ArbitrateLock`, `WaitProcessWideKey`), `WaitForAddress` / `SignalToAddress`, events,
  `WaitSynchronization`, memory (heap, map and unmap by copy, shared and transfer memory), `GetInfo`, `QueryMemory`, and
  the 19.2 MHz tick counter. The process can reboot itself in place by unwinding its threads at safe points.
- **`ipc.c` and `services/`**: HIPC / CMIF with domains and control messages; the service manager `sm:` is answered by the runtime itself. Implemented
  services include `appletOE` (and its `ICommonStateGetter`, `ISelfController`, `IApplicationFunctions`, a minimal
  `ILibraryAppletCreator`), `apm`, `lm`, `acc:u0`, `fsp-srv`, `audren:u`, `audout:u`, `time:*`, `hid`, `set`, `pctl*` and
  `aoc:u`. Every other service falls back to a stub that logs the call and answers success with zeros
  (`[ipc] no HLE implementation for service ...`).
- **SDK replacements** (`hle/`): `nn::vi` and `nv::` (display), `nn::hid` (controller state from SDL), `nn::pl` (system
  fonts), `nn::swkbd`, `movie::` (video through FFmpeg), the NVN bootstrap, engine heaps, and frame pacing.
- **Graphics** (`nvn/`): the NVN API is reimplemented at a high level; the GPU's command stream is not emulated.
  `objects.c` holds the `nvn*` objects (devices, memory pools, textures, samplers, programs, queues, sync, windows) and
  `cmdbuf.c` records commands as `CmdRec` entries inside the guest's own command memory. `vulkan/exec.c` executes the
  lists with the Vulkan Memory Allocator, a swapchain, descriptor sets and a pipeline cache. Textures use the Tegra
  block-linear layout (`tegra.c`) and ASTC is decoded in software (`astc.c`). **Shaders** are Maxwell binaries (NVN
  header, SPH and code); `maxwell/translate.c` turns them into GLSL, which shaderc compiles to SPIR-V at run time, cached
  on disk (`cache/spirv/`). Output scaling offers bilinear and FSR 1, and SMAA is available.
- **Host**: SDL3 (window, input, audio), FFmpeg (video), Dear ImGui (the F1 menu and the start screen), and
  `platform/linux.c` / `platform/windows.c` (memory reservation, crash handler, backtrace).

## 3. Testing and debugging

Tests (each runs without game files unless noted):

| | |
|---|---|
| `tools/test_lift.py` | the lifter against Unicorn, one instruction at a time (`--synth` needs no NSO) |
| `tools/test_simd.py` | the portable SIMD C against the instruction semantics |
| `tools/test_jumptable.py` | compact jump-table resolution on a hand-assembled function |
| `tools/test_translate.sh` | the Maxwell shader translator, fuzzed (GLSL compiles and validates) |
| `tools/test_vk_pipelines.sh` | fuzzed shaders through the Vulkan backend into real pipelines |
| `tools/test_astc.sh`, `tools/test_nxpkg.sh` | the ASTC decoder; the NSP / XCI / NCA reader (crypto and LZ4 vectors; header checks need your own package) |

Scripted runs use `PICRECOMP_*` environment variables (recorded input, a screenshot at frame N, profilers; `tools/shot.sh`,
`tools/frames.sh`). `PICRECOMP_PROF_SVC=1` also logs every event the runtime creates, with a backtrace.

**Debugging a hang.** A frozen window with 0% CPU is a deadlock, not a slow frame. Attach with `gdb -batch -p $(pgrep -n
pic-main) -ex 'thread apply all bt 25'` and look for threads blocked in `svc_wait_synchronization` (`timeout=-1`). In
that frame the argument `handles_ptr` points at the handle the thread waits on; the kernel's `handles[h - 0x100]` gives
its type (1 thread, 2 event, 3 session) and whether it is signaled. An event created by a service handler that nothing
signals will block the game forever: with `PICRECOMP_PROF_SVC=1` the creation line is followed by a backtrace naming the
handler (`addr2line -f -e build/picrecomp <address>` resolves unnamed static functions).
