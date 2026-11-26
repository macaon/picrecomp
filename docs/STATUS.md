# Status

Updated: 2026-11-26. Platform tested: Linux x86-64, Vulkan, NVIDIA RTX 5070 Ti (Wayland, PipeWire audio).
Windows has not been built or run for PICROSS S.

## Current overview

The repository contains 23 title manifests, all installed and started locally. A successful import or menu smoke test does
not establish complete gameplay support. The dated sections below retain the debugging history; this overview and the next
steps describe the current state.

| Titles | Recorded coverage | Still to verify |
|---|---|---|
| PICROSS S to S9, S+, X and nine licensed editions (20 titles) | All reached title/menu/puzzle-selection screens; S has puzzle-solving coverage, and S/S2/S3 saves were checked by the user. X's menu character and Easy Puzzle tutorial movie render | Every mode, puzzle completion and save/reload in every title; remaining tutorial clips |
| Piczle Cross: Story of Seasons | Title scene, menus and gameplay; ground and building rendering fixed | Complete a puzzle, save/reload, longer farm play, audio and Journal |
| Piczle Cross: Rune Factory | Main-menu terrain and tutorial-room colors restored; indirect draws/dispatches execute; observed SULD failures fixed; RFN008 tested | Complete a puzzle and save/reload; longer play and other scenes |
| Piczle Puzzle & Watch Collection | Opening confirmed working by the user after the Rune Factory fixes; colored title consoles and game boxes/reflections restored; layered rendering and formatted buffer reads verified | Included-game completion, saving, audio and longer play |

The three Piczle games now boot without experimental flags. PICROSS S keeps its declared system resources and its virtual
allocator works. Each Piczle game switched back to S in the same process; S → Story of Seasons also passed. Other title pairs
remain unverified. Shader policy is automatic per title: PICROSS and Rune Factory synchronous; Story of Seasons and Puzzle & Watch asynchronous. See the README's launch instructions.

Latest checks: runtime build, movie regressions (including ASan/UBSan), shader-policy/drain tests, storage-image GPU tests,
integer sampled-texture regressions and 2,000 generated shader cases. PICROSS S and Puzzle & Watch reached their menus with
Vulkan validation; Puzzle & Watch still emits unused-fragment-output warnings. All GPU checks used Linux/NVIDIA.

Documentation reconciled on 2026-11-26 against the manifests, installed Puzzle & Watch metadata and recorded test results.
The earlier 21-game rebuild was a dated test batch, not the current catalog size.

No title depends on owning or installing PICROSS S. It is used as a regression reference in the notes below. Separate data
roots containing only an S9 card or only a Story of Seasons card were tested through the launcher; each loaded its own game
library and modules. Missing game names now use a generic label, manifest-directory discovery does not probe S's manifest,
and a missing/invalid NPDM program ID fails instead of borrowing S's identity or the previous title's identity.

## Rune Factory transition delays (2026-11-26)

Profiling RFN008 reproduced a 5,194 ms call to `vkCreateGraphicsPipelines`. The corresponding GLSL/SPIR-V
cache lookup already hit; the delay was native driver pipeline compilation. Loading the existing saved Vulkan
pipeline cache reduced creation of the same shader-source pipeline to 1.28 ms on this Linux/NVIDIA machine.
With that cache loaded, scripted Save & quit, re-entry into RFN008 and a second Save & quit completed without
another multi-second compilation stall. An exit still reached 523 ms with effectively zero pipeline-build time;
that smaller transition cost remains to investigate separately.

The FPS log now includes aggregate native `pipeline` build milliseconds. `PICRECOMP_SHADER_TIMING=1` logs
SPIR-V cache/compilation, graphics-module setup and individual graphics/compute pipeline timings; native builds
taking at least 100 ms are logged without the flag. These are diagnostics, not a fix for first-use compilation.
Synchronous compilation remains enabled to preserve one-time render targets such as the color lookup table.
Normal shutdown saves the native pipeline cache; screenshot auto-exit bypasses shutdown, so profiling runs
must account for that when comparing cache behavior. Tests used scratch game/save data.

Validation: runtime build and shader-policy tests passed; screenshots verified both exits and the intervening
puzzle session. Detailed logs and screenshots are local ignored artifacts under `build/rune-delay/`.

## Rune Factory colors and missing terrain (2026-11-26)

The yellow/blue scene came from a corrupted 1024x32 color lookup table. Rune Factory generates this texture once; skipping
its draw while an asynchronous shader compiles leaves invalid data for subsequent tone mapping. Synchronous compilation
produced a valid lookup table and correct colors. Rune Factory now defaults to synchronous compilation. Current shipped
manifest policy overrides stale imported `async_shaders` fields; custom installs without a shipped policy retain theirs,
and the diagnostic environment override still wins.

The landscape was a separate omission: the main view uses `DrawElementsIndirect`, which the Vulkan executor silently
ignored. It now resolves indirect array/indexed draw and compute-dispatch arguments at execution time, after synchronizing
prior GPU writes, then uses the existing draw/dispatch paths. First-index byte offsets, signed base vertices and base
instances are preserved; zero vertex/instance/group counts do no work. The CPU resolution path adds GPU waits; native
indirect command submission remains a possible performance improvement.

Verified on Linux/Vulkan/NVIDIA RTX 5070 Ti:

- The main menu shows grass, dirt and paths beneath the buildings; the tutorial room has normal material colors.
- The validation-enabled menu capture reported no Vulkan errors or shader/pipeline failures. A separate cold-cache run,
  including a disabled NVIDIA disk cache, reached the main menu and tutorial room with the default synchronous policy.
  After compilation the tutorial room held about 58–60 fps, with zero skipped work. The menu was slower with indirect
  argument synchronization; full gameplay completion and save/reload remain unverified.
- `PICRECOMP_VK_VALIDATION=1 tools/test_vk_indirect.sh`: real GPU writes supply indirect arguments immediately before indexed
  and array draws and a 2x2x2 compute dispatch. Pixel/counter results pass, along with nonzero first indices, negative base
  vertices, base instances, execution-time reads and zero-count no-ops.
- `tools/test_shader_policy.sh` passes, including updated shipped policy overriding stale imported metadata.

## Puzzle & Watch missing scenes and console textures (2026-11-26)

The intro and menu geometry were rendered, then disappeared during tone mapping. Its color-grading lookup table is a
32-slice 3D render target, generated by an instanced draw with a geometry shader. The Vulkan backend rejected 3D attachment
views and ignored the geometry stage. It now creates compatible slice views, sets the framebuffer's layer count, and
translates/compiles/binds geometry programs, including per-vertex attribute reads, ISBERD, VMAD, VSETP, invocation information,
layer output and vertex emission. Clip conversion occurs once, in the last vertex-processing stage. Geometry texture
resources and unsupported instruction forms fail explicitly; this is not complete tessellation/geometry coverage.

A second issue made the recovered title consoles pale and incorrectly textured: TLD buffer reads used unsigned raw views
for half-float UV data. Sampled buffers now preserve float/normalized/signed/unsigned format conversion and select matching
sampler declarations. Raw SULD and storage-buffer views retain their own semantics. These are runtime changes; generated
game libraries and publisher assets are unchanged, and shader caches key on generated source.

Verified on Linux/Vulkan/NVIDIA RTX 5070 Ti:

- The startup 3D camera sequence is visible, the title shows yellow/red/cyan console artwork with reflections, and game selection
  shows the illustrated boxes and their reflections. Selecting Unbox also rendered the box-opening sequence. The final
  validation run reported no Vulkan errors or shader/pipeline failures, with zero skipped work after compilation.
  A synchronous-startup comparison ruled out asynchronous compilation
  as the cause of the pale consoles.
- `PICRECOMP_VK_VALIDATION=1 tools/test_vk_layered.sh`: synthetic Maxwell geometry instructions execute through the runtime's
  graphics pipeline. All 3D slices / array layers match, including selected layer ranges, mip levels and clip conversion.
- `PICRECOMP_VK_VALIDATION=1 tools/test_vk_texel.sh`: half-float, UNORM, SNORM, unsigned and signed buffer reads match their
  expected register values; descriptor formats and sampler numeric types agree.
- Storage-image promotion and raw SULD GPU regressions pass with validation. 2,000 generated shader cases compared with
  `abfe752` produced no MSL differences; all 849 successful GLSL translations compiled and passed SPIR-V validation.

The user confirmed on 2026-11-26 that the opening now works after the Rune Factory fixes. The exact connection to those
changes has not been isolated. Earlier local tests also showed the moving startup camera sequence with a clean runtime
shader cache, at 1280x720 and 1920x1080, with audio enabled, and through launcher autoplay. The package's full directory
inventory contains no standalone movie asset, although the executable imports movie APIs. The earlier menu smoke tests
had missed the absent 3D scenes. Full included-game completion, save/reload and audio remain unverified; Windows and other
GPUs remain untested.

## Rune Factory buffer surface loads (2026-11-26)

The two observed compute failures (`0xeb10048200490300`, `0xeb10028200480488`) were SULD.B.1D U128 loads. The Vulkan
translator now supports raw 32-, 64- and 128-bit buffer surface loads: byte addresses select words from an R32_UINT view,
independent of the texture's declared format, and data goes directly into consecutive guest registers. The address and all
loaded words are captured before writing destinations, including the game's R0..R3 load addressed by R3. Raw reads use a
read-only texel-buffer descriptor and do not mark memory as GPU-written. Raw loads and formatted stores of the same image
handle have separate views. Unsupported dimensions, sub-word sizes and other unimplemented forms fail explicitly.

The byte-addressing semantics follow NVIDIA's [surface-memory instruction description](https://patents.google.com/patent/US9519947B2/en).
This is a runtime translator/backend fix; generated game libraries and assets are unchanged. Shader caches key on source.

Verified on Linux/Vulkan/NVIDIA RTX 5070 Ti:

- Rune Factory loaded a scratch copy of the existing save and reached RFN008. Its puzzle board and outdoor background
  rendered; after compilation, draws and compute dispatches had zero skips. The validation-enabled run reported no Vulkan
  errors, shader translation failures or pipeline failures. A normal run then held about 58.5–60 fps with zero skips in
  the tested puzzle. Puzzle completion, saving and longer play remain unverified.
- `PICRECOMP_VK_VALIDATION=1 tools/test_vk_suld.sh` translates and executes synthetic Maxwell shaders with the real view
  resolver, without game data. Raw values match for U32/U64/U128 at nonzero byte offsets, including float/NaN/sign bit
  patterns and an address/destination overlap. RGBA32_FLOAT and R16_FLOAT source descriptors both retain raw word semantics.
  Out-of-range reads return zero; shared raw-load/formatted-store handles and rejection of unsupported forms are checked.
- 4,000 generated translator cases, compared with `abfe752`: no MSL differences; all 1,916 successful GLSL translations
  compiled and passed SPIR-V validation, with zero compile or validation failures. The existing missing default reference
  and local development-header limitation still apply; the explicit reference and scratch header were used.

Other GPUs and Windows remain untested.

## Startup allocator and texture limits (2026-11-26)

Removed `PICRECOMP_TEMP_KEEP_SRS` and `PICRECOMP_TEMP_DI` instead of adding title-specific workarounds. Startup now retains
the system resource size from each NPDM throughout allocator initialization, resetting it before reading the next title's
metadata. NVN device queries 61 and 64 report 16,384 rather than the placeholder 1 used before the experiments.

The allocator failure was `MapPhysicalMemory` clearing pages that were already mapped. Local PICROSS S SVC tracing showed
maps at alias address `0x7440000000` of 0x1000 bytes and then 0x740000 bytes. The game had written allocator metadata into
the first page; clearing the overlapping range erased it and the SDK subsequently dereferenced zero in `sdk+0x31dce4`.
The kernel now commits and zeroes only holes, preserving existing physical pages. This also commits new pages before
writing them, as Windows requires. No game code regeneration or publisher assets changed.

Checks on Linux/NVIDIA, with both old environment variables absent:

- `tools/test_physical_memory.sh`: overlapping and repeated maps preserve bytes, multiple holes are zeroed, unmap/remap
  provides fresh zero pages, untouched neighbours survive, and texture limits are correct. Also passed ASan/UBSan with leak checking.
- PICROSS S reached its main menu using its VAM allocator; S → Story of Seasons reached the rendered introduction.
- Story of Seasons reached puzzle S004, Rune Factory reached RFN008 with its outdoor scene, and Puzzle & Watch reached its menu.
- Each of the three Piczle games switched back to S, which reached its main menu. Puzzle & Watch → S also ran with Vulkan
  validation enabled, without validation errors; existing unused-fragment-output warnings remain.

At this point Rune Factory still reported two unsupported SULD compute programs, subsequently fixed by the buffer-load
work above. The startup fix alone did not establish full gameplay support.
Scripted Story of Seasons input exercised drawing on S004, but did not complete it; puzzle-completion/save-reload remains
unverified. All gameplay checks used scratch saves and left the user's save files unchanged.

## Per-title shader policy and storage images (2026-11-26)

Vulkan compiles shaders synchronously by default, preserving PICROSS's one-time draws into textures. The three Piczle
manifests opt into asynchronous compilation so cold compilation does not block presentation long enough to trigger UE4's
watchdog. `PICRECOMP_ASYNC_SHADERS=0` forces synchronous compilation; `=1` forces asynchronous compilation. Older installs
missing the field use the shipped manifest identified by their slug. Explicit installed fields take precedence.
The policy resets when loading another title. Reboot drains translation and compilation jobs before clearing Vulkan state
and guest memory; worker creation uses `pthread_once`.

Images first used for compute stores now use the game's native pixel dimensions, independent of window/render scaling.
Promotion preserves preceding GPU writes in command-buffer order, including mip levels and array layers; resized images
use nearest blits where supported. This prevents scaled TAA targets from discarding out-of-range native pixel stores.
These are runtime and title-manifest changes; rebuilding generated game libraries is unnecessary.

Validation on Linux/NVIDIA:

- `tools/test_shader_policy.sh`: defaults, installed flags, manifest fallback, environment overrides, both switch directions,
  and draining 2,000 asynchronous jobs passed without game data or a GPU.
- `PICRECOMP_VK_VALIDATION=1 tools/test_vk_storage.sh`: native-size promotion, preservation of pending GPU writes and all
  tested mip/layer contents, actual compute stores, and subsequent scale changes passed with no validation messages.
  Covers 2D at 0.5x/1x/1.5x, 3D and 2D arrays.
- Same-process PICROSS Lord of the Nazarick → Story of Seasons and the reverse both reached rendered title screens;
  logs confirmed synchronous → asynchronous and asynchronous → synchronous policies respectively.
- Puzzle & Watch, empty runtime cache with the NVIDIA disk cache disabled: presented frames after about four seconds,
  initially black while compilation continued, then reached its Select game / Unbox / View / Options menu without the watchdog crash.

- Rune Factory: a scratch copy of the existing save reached puzzle RFN008 with its board and outdoor background rendered
  at 1280x720. Some compute programs remain skipped, as previously recorded; this was a rendering smoke test, not a completed puzzle.

At this earlier checkpoint, all three Piczle games still required the experimental
`PICRECOMP_TEMP_DI=61,64 PICRECOMP_TEMP_KEEP_SRS=1` settings. Applying `TEMP_KEEP_SRS=1` to PICROSS S breaks allocator startup,
so the cross-engine switch tests used Nazarick. The startup allocator work above subsequently removed that limitation; the sampled integer texture work
below fixes Puzzle & Watch's float-sampler/integer-view error. Windows, AMD and Intel remain untested.

## Movie regression harness (2026-11-26)

`tools/test_movie_hle.sh` builds again after adding the renderer-present-counter stub required by movie texture-upload
debug logging. Like the existing upload stub, it asserts if called: the test exercises CPU output and must not enter the
renderer path. No runtime or generated game code changed. Both movie test scripts pass, including separate ASan/UBSan
builds with leak checking. The sanitizer runs require execution outside the sandbox's ptrace wrapper.

## Sampled integer textures (2026-11-26)

Puzzle & Watch bound an RGBA8_UINT texture view to fragment slot t7, but ordinary sampled textures were always declared
as float samplers. Vulkan reported `VUID-vkCmdDrawIndexed-format-07753`. Shader variants now follow the effective view's
numeric type, including format overrides, for vertex, fragment and compute stages. Integer reads use unsigned/signed GLSL
samplers and write integer register bits directly; signed values retain their sign extension. Graphics and compute program
keys include those types, so rebinding a different format selects the corresponding shader. Type selection avoids uploading
texture pixels. No generated game code or asset changes are required; the SPIR-V cache already keys on generated source.

Verification: Puzzle & Watch reached its menu with Vulkan validation enabled and no sampler numeric-type errors. The existing
unused-fragment-output warnings remain. Fetch and gather regressions compile and validate float, unsigned and signed variants;
the typed regressions fail against commit `9418e76`. Two seeds covering 2,000 generated programs produced no MSL differences
against that commit; all 1,033 successful GLSL translations compiled and passed SPIR-V validation. Unsupported instruction
patterns are still rejected by the translator. The storage-image GPU test also passes with validation, including checks that
unsigned/signed view overrides select their effective format. PICROSS S also reached its main menu with Vulkan validation
enabled and no validation errors. Linux/NVIDIA tested; other GPU vendors and Windows remain untested. These are menu/rendering
smoke tests, not completed puzzle sessions.

The translator test script's default reference `652e234` is absent from this checkout. These comparisons explicitly used
`9418e76`. SPIRV-Tools' library and CLI are installed locally but its development header is missing; the header was downloaded
from Khronos into ignored build scratch space for the test build.

## Original debugging reference: PICROSS S

| | |
|---|---|
| Game | PICROSS S, base title `0100BA0003EEA000` (v0), update title `0100BA0003EEA800` v524288 |
| `main` build ID | `260797018fefb8459d1bf07815e71c23` (the updated executable) |
| Modules | `main` (120,117 functions), `subsdk0` (13,670), `sdk` (39,292), loaded at `0x7000000000`, `0x7008000000`, `0x700C000000` |
| Generated | `generated/` is about 645 MB of C |

This historical baseline anchors the earlier address-specific notes. Do not reuse those addresses for another build.
The current importer supports the title manifests and generates libraries for their own executable build IDs.

## What works (seen running)

- Saves persist across restarts (checked by the user).
- The stage preview ("Play this puzzle?") shows its picture: shaders now compile before the draw runs. The game draws
  that preview once, into a texture, and the old default (skip draws while a shader compiles in the background) left it
  black. `PICRECOMP_ASYNC_SHADERS=1` brings the old behaviour back.
- Boots, shows the title screen and the main menu, selects puzzles, solves a puzzle, shows completed-puzzle thumbnails.
- Audio plays (SDL, PipeWire). The runtime's frame stats show 60 fps on the title screen and menus on the test GPU;
  the frame rate while solving puzzles has not been measured.
- Keyboard input (K = A, J = B, arrows, Enter = Plus; see the README) and the in-game F1 menu.

- Switching games without restarting the process (Linux, Vulkan): S to S2 through the in-place reboot, the window
  stays up (log: one process, two `[gamelib]` lines, S2 at 60 fps). `gamelib_load` swaps the library per game folder,
  `boot` re-points the RomFS and reads the program id from `main.npdm`, the `guest_sym` cache is reset.
  `PICRECOMP_SWITCH_TO=<game folder>` (test) picks the game after a `PICRECOMP_REBOOT_AT=<frame>:title`;
  `PICRECOMP_SWITCH_RESTART=1` brings back the old restart-the-process switch.
- Fixed with it: every in-place reboot lost the GPU (10 s hang at the first frames). The game's buffer to texture copy
  of the save slot screenshots overran a render-target-shaped image allocated at the output scale (`exec.c`).

- Save data is kept per title: `<data root>/save/<program id>/save_<type>/`. Before, every title shared
  `save/save_0/all.dat`, so booting S2 over S's file rewrote it. The old `save/save_<type>` is copied into PICROSS S's
  folder on its first run (the old folder stays as a backup). Checked by the user for S, S2 and S3 (saves kept and separate).
  `OpenSaveDataFileSystem` with another title's program id in the attribute (what S2 would send to read S's saves) mounts
  that title's folder read-only, or returns ResultTargetNotFound (for S4; no title here calls it).

- PICROSS S4, S5, S6, S7, S8 and S9 have manifests. Each was installed here (prepare, generate, compile, about four minutes)
  and started to its title screen: 60 fps, no fatal error, no unimplemented call, in a scratch data root. S4 to S6 and
  S8 use the same `sdk` and `subsdk0` as S; **S7's are different** (a newer SDK), which works because the sdk's import
  of `nninitStartup` is found by what it is (`loader_import_slot`) and the generator finds the unwinder's resume
  function by its code, not by address. S8 has no update package (the base game is the whole game; its manifest has no
  `update_id`). Nothing past the title screen has been played on S4 to S8.
- PICROSS S9 (`010021a0197ba000`, base only, v1.0.0) imports and reaches its main menu at 60 fps (Vulkan, Linux); it found the S5 save and
  unlocked an extra puzzle (the cross-save feature, see the S4 notes). No update package exists here; nothing past the menu played.
- PICROSS S Doraemon & F Characters edition (`01002b901fa74000`, base only, v1.0.0, released 2025-03-26): no code-side work; imports (11 minutes: a
  larger game) and reaches its puzzle select at 60 fps on a fresh save (Vulkan, Linux). Nothing played.
- PICROSS S CAPCOM CLASSICS edition (`01009ad023f90000`, released 2025-11-27; base v1.0.0 and update v1.0.2 / 131072): imports and
  reaches its puzzle select at 60 fps on a fresh save, base alone and base with the update (Vulkan, Linux). The first game of
  the 2025 SDK (20.5.17); four things were new:
  - **hactool** (`third_party/hactool/nca.c`) refused the update's patch section: its subsection table is followed by
    0x4200 more bytes, and its total size counts the data up to the relocation table only. Both checks accept that now (the
    IVFC hashes of the patched RomFS verify).
  - **Symbols:** only `DT_GNU_HASH` (see above), so nothing was found until `nso.py` and `loader.c` read it.
  - **The sdk's own exceptions:** landing pads in every module, not just `main` (a catch in the sdk's clock code).
  - **Time:** the SDK's `CalculateSystemClockCurrentTimespec` reads a ContinuousAdjustmentTimePoint at `+0xD0` of the time
    service's shared memory and wants its clock source id to match the contexts': without it `clock_gettime(CLOCK_REALTIME)`
    failed and `std::chrono::system_clock::now()` threw out of the game (terminate). All zero but the id is no adjustment.
  Nothing played.
- PICROSS S SNK CLASSICS & NEOGEO edition (`0100122023706000`, released 2025-11-27; base v1.0.0 and update v1.0.2 / 131072): same SDK as
  Capcom's, nothing new needed: imports (base alone, and with the update) and reaches its puzzle select at 60 fps on a fresh
  save (Vulkan, Linux). The manifest name uses a plain `&`, not the fullwidth one of the package's file name. Nothing played.
- PICROSS X : PICBITS VS UZBOROSS (`0100f08018840000`, released 2022-12-19; base v1.0 and update v131072 / 1.1.0; the same base
  executable in the Japanese and the USA packages): a different game from the S series. The first package tried was the
  Japanese one, whose NACP lists Japanese only (its icon is `icon_Japanese.dat`: the importer picks the first icon there is when
  there is no English one); the USA package has the 7 languages of the store listing and is the one installed now (the 7 add-on
  skin packages are not imported). Imports and runs past the title screen, the first-run tips and into the main menu at 60 fps
  (Vulkan, Linux). One shader did not translate: a bindless TEX whose handle (MOV + LOP.OR from the driver's c2 tables) is built
  once at the start and sampled after branches; the search for the handle stopped at the block boundary, and a DEPBAR on the way
  would have ended it. It goes on across blocks now and passes a DEPBAR (no destination); `check_handle_across_blocks` in
  `tools/test_translate.c` fails on the old translator. The account name shown beside the character is "PicRecomp" (it was
  "Link", left from the project this one came from).
  The jumping character in the main menu's top-left circle now appears (Linux, Vulkan, NVIDIA RTX 5070 Ti).
  The game assembles its equipped character parts using `gs.CharaRendering.Render` before the main menu opens: a camera
  renders four expressions into a 128x128 RenderTexture, then `Texture2D.ReadPixels` / `Apply` turns them into the menu's
  sprites. The Vulkan texture-to-buffer copy rejected destinations below `HEAP_BASE`, but NVN buffer addresses are CPU
  addresses and may live in the alias region: the portrait readback here targets `0x71508bb800`. Each copy was silently
  skipped, leaving blank portrait images. The heap-only restriction is removed; no game code or assets changed.
  Verified by running the installed game through the first-run tips into its main menu; the same scripted run before the
  fix had no character, and after the fix shows the red body, eyes and turquoise ears. Captures 15 frames apart at
  frames 1900/1915/1930 confirm its jumping motion; the menu runs at 60 fps with no skipped draws. The original expression renders
  were already correct. Earlier draw tracing at frames 1250-1530 missed their one-time creation before the menu opened.
  The paw cursor is the game's stick pointer: it shows once the left stick moves, follows it and ignores the d-pad (as on Eden).
  The "Easy Puzzle!" tutorial video now plays in color (Linux, Vulkan, NVIDIA RTX 5070 Ti). Three runtime gaps hid it:
  - Its VideoClip is embedded in a Unity asset bundle, with an `archive:/...resource` URL. The movie extractor ignored
    the supplied `ClientStreamReader` and tried opening that URL as a host file. FFmpeg now reads and seeks through the
    guest reader callbacks, using guest-addressed scratch memory and the current extractor thread's CPU state.
  - This Unity version uses direct extractor exports, metadata assignment / 64-bit lookup, `Buffer::Data`, and the
    decoder's output-buffer-plus-format overload (vtable slot 17). They now use the host implementations throughout;
    the original SDK methods cannot operate on the host replacement objects.
  - Unity supplies a client buffer and retains its pointer for uploading. Returning a different buffer left its original
    Y/UV planes untouched (green after decoding was enabled). The decoder now copies tightly packed NV12 into the supplied
    allocation, checks its capacity, and reports the matching stride, UV offset and slice height.
  Verified with a scratch save and an in-memory selection of the actual Easy Puzzle tip: the original black panel becomes
  an advancing video in captures at frames 1500/1800/2100, and playback reaches/restarts its loop without a crash.
  `tools/test_movie_source.sh` tests synthetic MP4 decoding/seeking through short callback reads; `tools/test_movie_hle.sh`
  tests metadata ownership, reader ABI/register preservation, and packed client output at 580x326 and odd dimensions,
  including bounds and EOS. Both pass, also under ASan/UBSan. No game code regeneration or asset changes are needed.
  No puzzle gameplay was tested; other tutorial clips and Windows playback remain unverified.
  Debugging aids: `PICRECOMP_DEBUG_FRAME=<frame>` dumps each draw and pass image; `PICRECOMP_DEBUG_DATA=1` adds vertex
  floats and constant buffers. `PICRECOMP_LOG_UPLOADS` / `PICRECOMP_LOG_FILES` trace uploads and RomFS reads.
  Scripted host inputs use `PICRECOMP_HOST_INPUT` with frame times (`lstick` moves the stick).

- The start screen's game cards: five to a row, two rows (ten) to a page, the "+" counting as a card; more games make
  pages. The "+" is always there (more games than the importer knows are coming), as the last slot. The cards sort
  by `"released"` (the release date, `YYYY-MM-DD`) in the manifests: new games slot in by their own date, read by slug so a game added
  before the field needs no re-import. Right / Left step through the cards in reading order, and past a page's last /
  first card turn the page; L / R, PageUp / PageDown, the mouse wheel and the gold arrows at the sides turn it too, and
  dots under the cards show the page. The block is the same size on every page. Seen with 12 cards in scripted runs
  (scratch library of symlinks): Right across a row end and a page end, PageDown, Left back, the arrows' look. Not tried:
  clicking an arrow, the wheel, a gamepad, the "+" at a page's edge.

- The start screen has no PLAY / SETTINGS / QUIT menu any more: the title is top middle, the cards are centred and are
  the menu. The cursor (arrows, d-pad, left stick) chooses a card; A, Enter or Start play it (a mouse click chooses,
  a second click plays), Select, `-` or F1 open Settings, and SETTINGS and QUIT are small links at the lower right
  (down from the cards, or the mouse); a hint line at the bottom names the buttons of the last device used. Seen with
  scripted keys (`PICRECOMP_LAUNCHER_KEYS="frame:Right,..."` with `PICRECOMP_LAUNCHER_SHOT`): moving along and down a
  row, to the SETTINGS link, F1 to Settings and back to the chosen card, Enter into the first-run question. A
  controller on the start screen and the hint line's controller wording have not been tried (no pad here).
- `PICRECOMP_HOST_INPUT="frame:shot:<name>"` now saves `build/<name>.png` (it set a request nothing read), so a run can
  take several screenshots between scripted key and button presses.

- The start screen shows the chosen game's real version ("Game version 1.4.1"), read from the NACP of its update's
  control data (the base game's when it has no update): `prepare.py` writes it as `display_version` in the game's
  `title.json`. Seen: S 1.6.1, S2 1.5.3 (earlier 1.5.2), S3 1.5.3, S4 1.4.2, S5 1.3.2, S6 1.3.1, S7 1.2.0, S8 1.0.0, S9 1.0.0. Games added before this
  have no such field; the line is left out for them (the ones here were filled in by hand from their packages).
  With the mouse a click chooses a card, the Play button at the bottom (lit while the chosen game can be played) starts it,
  and so does a double click; A / Enter / Start play it from the cursor. Settings is the "F1 Settings" hint, which is a
  link too. Seen with scripted clicks (`PICRECOMP_LAUNCHER_KEYS="frame:click_<x>_<y>"`): a click chooses, the Play click starts.

- Updates. A game's update is a whole new build of its `main` (S 1.6.1 to 1.6.2: 6.86 of 7.24 million code words differ;
  `sdk` and `subsdk0` were identical), so a new version means regenerating and recompiling that game (about four minutes),
  nothing incremental. `titles/` has one manifest per game (metadata only: name, release date, title ids; nothing is
  tied to a build), and **any version imports**: the importer takes the newest update among the packages in a folder
  (its version from the package's own metadata), or just the base game when there is none; the main build ID is read
  from the extracted executable and recorded in the game's `title.json`. Adding a game that is in the library
  with another build builds the new one beside it (`.<name>.new`) and swaps it in when it is done (the old one is kept
  if the build fails); if only the data changed (all three executables' build IDs equal) the library is reused and
  nothing is compiled. The same build and update version again is refused ("already in the library (version 1.6.2)"). Seen: S 1.6.1 to
  1.6.2 in a scratch library (installed, swapped, 1.6.2 boots at 60 fps), the same version refused, the data-only path
  (compile skipped, library kept, boots). Saves are kept per title, not per version, and carry over. The data-only
  path is only simulated (no update that left `main` alone has turned up). From the start screen: Settings > Game files
  names the chosen game and its version and has "Update this game", which opens the importer page headed "Update <game>"
  (the "+" card is gone once every game is added, so this is the way in). Run through the in-app importer in a scratch
  library with S's real folder (base + v524288 + v589824 in it): it picked v589824, built beside the installed S, swapped
  it in (1.6.1 to 1.6.2), and cleared the stale `.picross-s.new` an earlier, interrupted run had left. Closing the
  launcher during an import ends it; the installed game is untouched then.

- Start screen, lower left: nothing about a game that is ready (a problem with the chosen game still shows there); its
  version is in Settings > Game files, which now lists Title, Version and Size on disk, and has Open folder, Open save
  folder (`<data root>/save/<title id>`, dimmed until the game has saves), Update this game and Delete game files, each
  with a one-line tooltip. Fixed with it: Delete game files was refused for games of the library (it only accepted the old
  `extracted/`) and left `title.json` and the icon behind; it removes the whole `games/<name>` now, and the start screen
  returns to the cards (or the "+") instead of the old SET UP GAME FILES page. Seen in scratch libraries with scripted
  clicks (`PICRECOMP_LAUNCHER_KEYS=click_<x>_<y>`): delete S7, S8 stays with the "+".

- Zelda leftovers out of Settings: the "Game font" section (PICROSS never asks for the system fonts: no `pl:u` call in any
  boot log; the games are Unity and bring their own fonts), and the Audio tab's "Room reverb" and "Movie audio" (the
  games use `audout:u` only, plain PCM with the volume applied: no `audren`, so no reverb, and no nnMedia movie player).
  Old `reverb` / `movie_audio` lines in `picrecomp.ini` are ignored. The audren reverb and the nnMedia movie HLE
  (`runtime/hle/movie.c`) are still in the runtime, unused by these games. The settings pages showed the last page's
  description until a row was selected (Audio showed Video's); each page starts with its own now. The Controls tab's
  "D-pad moves like the left stick" is gone too (a Zelda need: PICROSS reads the d-pad itself; Right once at S4's main
  menu moved the cursor one step with the setting on and off), with the stick-from-d-pad code in `hle/input.c`; an old
  `dpad_move` line in `picrecomp.ini` is ignored.

- Each game has its own muted background on the start screen (and behind Settings): the colour wash behind its title
  screen, `Common_BG000` in `Data/sharedassets5.assets` of its RomFS (S+ keeps it as `Common_BG000_DL` in sharedassets4; found in all nine games: S a rainbow wheel, S2 a
  beach, S3 green light, S4 a sunset, S5 a night city, S6 a sunlit room, S7 snow, S8 a nebula, S+ pale light), read at import time by
  `tools/picross/art.py` into `<game>/background.jpg` (never committed; UnityPy, optional: `pip install UnityPy==1.25.4`
  into `tools/venv`). `art.py <game folder>...` adds it to a game added before (done here for the eight installed). The
  start screen keeps one texture per game, dims it evenly (more for a bright picture) and fades the bottom for the lower
  line; without a `background.jpg` the plain gradient stays. The old shadow banners (for the left-hand menu) are gone, and
  the title-movie frame it used to look for (`romfs/region_common/movie/title.webm`, a Zelda path) with them.

- Controls: Settings > Controls has a "Face buttons" row, Switch layout (A on the right, the default) or Xbox layout (A at
  the bottom, so the letters on a PC controller match the game's): it applies the four swaps to the controller bindings
  (anything else bound to those buttons swaps over; the table below still rebinds each input) and saves them as
  `bind_pad_a..y` in `picrecomp.ini`. Seen with a scripted click: `A -> south, B -> east, X -> west, Y -> north`.
  The "Button icons" setting (Auto / Nintendo / Xbox / PlayStation) is gone: it fed an empty `texpatch` stub. In its
  place `runtime/nvn/texpatch.c` makes the game's own A / B / X / Y prompts follow the bindings: the game draws each as
  a diamond of four circles with the pressed one filled (Switch positions), four 64x64 DXT5 textures identical in all
  eight games; at upload the one shown is read from its alpha and its blocks are turned (a lossless permutation of the
  pixels inside each DXT5 block) until the filled circle is where the bound controller button is, and uploaded again
  when a binding changes. Unity stores rows bottom first, so on the screen a clockwise turn is the other way in the
  data (`tools/test_texpatch.sh`, which also checks it against the picture turned pixel by pixel; seen in S4's bottom
  guide: Switch layout Confirm = right circle, Xbox layout Confirm = bottom, Back = right, X = left, also when the layout
  is chosen while the game runs: `PICRECOMP_FACE_AT=<frame>:xbox|switch`). The shoulder / trigger / Plus / Minus icons
  are still the Switch's (an Xbox pad's LB / RB / LT / RT / Menu / View would need pictures of our own).

- The game's "+ - Controller Settings" (Plus *or* Minus at the main menu, each on its own) starts the console's controller
  applet (`CreateLibraryApplet` id 0xc), which the runtime used to complete at once with nothing to show; starting it now
  opens our menu on the Controls tab (`menu_open_controls`). Seen in S4 with Plus alone and with Minus alone scripted.
  On a controller, only Minus and Plus *together* (Back + Start) is the menu's own hotkey, so a single press reaches the game.

- Settings stay global, with two additions. A note by the "Settings" title says what the tab applies to ("applies to all
  games"; "this game's files" on Game files). The Language list offers only the languages the chosen game has text for:
  `prepare.py` saves the metadata's SupportedLanguageFlag as `language_flags` in the game's `title.json`
  (`settings_game_languages`, read when the cursor moves to a game and when it boots). All eight installed games have the
  same ten (English US, Japanese, French, German, Spanish, Italian, Korean, both Chinese, Brazilian Portuguese), so English
  UK, Latin American Spanish, Canadian French, Dutch and Russian are gone from the list; a saved language the game lacks
  shows as Auto and the game is served English (US) (else its first language) instead. Games added before have no such
  field and offer them all (the eight here were filled in from their packages).

- With the Xbox face-button layout chosen, the menus (the in-game F1 menu and the start screen) confirm with the bottom
  button (Xbox A) and go back with the right one (B), instead of the Switch's fixed A on the right: `key_from_pad` in
  `menu.cpp` and `launcher_pad_key` follow `bind_face_layout()`. Any other binding (hand-edited) keeps the Switch's fixed
  menu layout, so no binding can lock the player out. Seen in the F1 menu with scripted pad events: Xbox layout, bottom
  button opens the Language list, right button closes the menu; Switch layout, right button opens it.

- F1 menu: K confirms and J goes back as well as Enter / Space and Esc (the keyboard's default A and B, fixed: menus never
  use the rebindable keys; not while a field is being typed in). On the Quit tab the cursor goes to the first row of the
  page it switches to ("Yes, quit ..." / back on the choice), where it used to vanish so that the first Down landed on
  Cancel, and B / Esc backs out of the question without also closing the menu. The window keeps 5% of the height free above
  and below. Seen with scripted keys (`PICRECOMP_HOST_INPUT` `sdlkey:`) on Linux; Start does not confirm (it never did: it
  opens the menu with Select).

## Not verified

- Other game modes, the options screens, quitting from the game, other GPUs and drivers, gamepads.

## Known gaps

- **Plus at the main menu launches system applet `0xC`**, which by libnx's `AppletId` list is the Controller applet
  (pairing and configuration; on a Switch). It is not emulated: `runtime/services/am.c` creates an applet object that
  finishes as soon as it is started, with no output, and the game returns to its menu. Possible later: open the
  runtime's own Controls page instead.
- `ISelfController` commands 1 and 2 (LockExit / UnlockExit) and `ICommonStateGetter` 66 and 900 are logged as
  "unhandled, ok".
- **Events that never signal.** `new_event()` and `new_event_handle()` (`am.c`, `misc.c`, `hid_set.c`, audio commands 4,
  11, 12) hand the game an event that nothing ever signals. If a service call makes the game hang, look here first
  (`docs/ARCHITECTURE.md`, "Debugging a hang").
- The start screen shows a card for each installed game and a "+" card. "+" opens the importer: pick a folder with the
  two `.nsp` files (and a `prod.keys` if it is not beside them or in `~/.switch`), Import. `runtime/importer.c` runs
  `tools/picross/prepare.py --folder` as a child process (so it needs python3 and the built `hactool`, found from the
  checkout) and shows the `@step n/10` and `@progress` lines it prints as a step list and a segmented bar (generation
  and compiling report how far they are). Needs `clang`, `make`, `bash` and the Python tools venv. The inherited in-app setup (`SETUP` page, `extract_thread`, the XCI reader) is unreachable and still
  checks the old Zelda title ID; it can be deleted.
  Layout checked by screenshots (no game, S only, all three, the importer idle, running, done and failed); an import of
  S3 was run end to end and gave a card. Clicking the buttons and the file dialogs, and gamepad navigation, have not
  been tried.

- **Other titles.** Every game now has its own folder `<data root>/games/<name>` holding `exefs/`, `romfs.bin`, the
  icon and its own `picgame.so` (`generated/`, about 640 MB, is only there while it builds: `install.py` deletes it after a successful compile, `--keep-generated` keeps it for a rebuild without regenerating). `tools/picross/install.py` (what the start screen's importer runs) does
  everything: prepare, `generate.sh`, `make game GAME=<folder>`. A library's `.id` file names the main build ID it was
  generated from and is refused for any other game. Nothing is per title or per build in
  the generator any more (see "Exceptions and versions" below). S2's install ran end to end here (prepare, generate 2.5
  minutes, compile) and gave a ready card. After the generator changes below S and S2 were installed again (about
  4 minutes each on 16 threads) and both were started in a scripted run through the title, the main menu and the stage
  list to the "Play this puzzle?" popup, with its preview: S2 was checked to there and S through a puzzle's first
  seconds. S3 was subsequently built and run; the user verified separate saves for S/S2/S3, and the 2026-11-24 batch
  rebuilt the then-existing 21 titles. Full gameplay coverage remains limited. `runtime/main.c`'s `sdk + 0xcdd210` is in the shared `sdk` module
  (same build ID in all three games).

- **Generated code and jump tables.** A function with a jump table gets a fallback switch over all its instructions,
  so a case the resolver missed still lands. In one 2,900-instruction function of S that switch took clang 8 minutes
  (the "frozen at 100%" in the importer was the last chunk compiling). Functions of more than 1,500 instructions
  whose tables are all exact (the compare's own count, every index emulated: `gen.py` `SAFETY_NET_MAX`) now go
  without it. A compare is taken as a table's bound only if the register indexing the table load is the compared
  one (`_indexes_table`); the earlier, looser match sent one S2 switch to one wrong target and the game stopped at
  boot (`tools/test_jumptable.py` has both cases). Not audited: every exact table in the three games.
  The compact tables (`_jump_table_compact`) guess their size and keep the fallback.
- **Importer progress.** It counts only the game's own objects and holds at 99% until the library is linked.

## Leftovers from the base project

Removed in this stage: the Android, macOS and Metal ports, and every name from EoWRecomp (environment variables are now
`PICRECOMP_*`, the binary `picrecomp`, the library `picgame.so`). Still to clean up:

- The Metal-only code paths that remain in shared files: the MSL output of `runtime/nvn/maxwell/translate.c`, the
  MetalFX upscaler enum in `settings.[ch]`, and many comments that mention Metal, macOS or Android.
- Phone touch controls (`runtime/ui/touch.cpp`) and the `GUEST_SHIFT` address-layout offset in `runtime/cpu.h`
  (both only mattered on Android).
- The unused mod-folder overlay in `runtime/services/fs.c` (`mod_dir()` always returns NULL).
- `tools/linux/` (AppImage packaging, container build, smoke test) and `tools/windows/` are inherited and untested for
  PICROSS S.
- `tools/nca_extract.py` and `runtime/nxpkg.c` read a package directly; PICROSS S is prepared with hactool instead.

## Next steps

1. Complete a puzzle, quit and reload its save in each Piczle game. For Puzzle & Watch, first test entry into its included games.
2. Check audio and longer play in Rune Factory and the other Piczle games, including other scenes, long frames and any
   renewed watchdog failures or newly encountered unsupported shader instructions.
3. Make the translator regression workflow reproducible: its default reference commit is absent from this checkout and
   the local SPIRV-Tools development header was missing. Then broaden gamepad, remaining modes and tutorial-movie checks.
4. Test AMD/Intel subgroup behavior and the Windows build; address the inherited cleanup items separately.

The launcher already imports through its "+" card and switches installed games. The inherited setup flow is unreachable
cleanup, not a prerequisite for importing. Successful imports delete generated C by default; use `--keep-generated` when
needed for development rebuilds.

### Exceptions and versions (2026-11-19)

The per-build parts of a game used to be hand-found: the addresses of the calls that unwind into a catch or a clean-up
(each new game or update needed a bring-up loop and a manifest entry), the unwinder's resume patch, and the compact
relocations of the newer toolchain. They are the generator's own work now (`tools/gen.py`, `nso.py`, `lsda.py`), and
manifests carry no code addresses:
- **Landing pads.** `lsda.py` reads the compiler's own exception tables (`.eh_frame_hdr`, the LSDA call-site records). For
  every module the generator wraps every call the tables say can unwind into a pad of its own function (the sdk's own
  catches matter in the 2025 SDK: PICROSS S CAPCOM CLASSICS edition)
  (`rt_call_with_landing` / `rt_invoke_with_landing`, the guest unwinder's jump back becomes a `longjmp` to that frame)
  and gives every pad a label. Shield Hero: 3,751 functions with pads, 44,425 wrapped calls; S: 2,119 and 21,704.
  Every path that throws is covered, not just the ones that were hit while testing.
- **The unwinder's resume** (the sdk's register-restore jump, in either of its two forms) is found by its code and ends in
  `rt_resume_landing`.
- **RELR** (`DT_RELR`, the compact relative relocations of a game's 2024 modules) is read by `nso.py`, and applied by
  `runtime/loader.c`.
- **Symbols from `DT_GNU_HASH`.** The 2025 toolchain's modules have no `DT_HASH`, which the symbol count came from: they
  loaded with no symbols at all (no `nn::init::Start`, no imports). `nso.py` and `runtime/loader.c` count them from the
  GNU hash table (the symbols past `symoffset`, to the end of the last bucket's chain) when there is no `DT_HASH`.
- **A rebuild from the start screen** (`runtime/gamebuild.c`, which calls `gen.py`) is therefore right for every game; it
  used to skip the per-game patches.
- **Versions.** Since nothing is tied to an executable, the base game alone imports for every game, and any update does.
  Checked with the base package only, for all twelve games that have an update (Vulkan, Linux): each imports and reaches
  its main menu at 60 fps. Two base builds needed runtime changes: S3's and S4's trap on the NVN API minor (every game is told
  350 now), and the first PICROSS S (2017) has no `nn::init::Start`: its rtld runs the startup steps itself, so
  `runtime/main.c` (`rtld_old_start`) runs the same sequence from the sdk's and the game's own symbols (its SDK has no
  `InitializeVirtualAddressMemory` either, now optional). It reaches its tutorial puzzle. Measured: for 12 of 14 games the base package's main executable differs from the update's build.
- **Cost of the generator change:** the library ID has changed twice (a third of an hour to rebuild a few games, an hour and a half
  for all eighteen; 3 to 13 minutes each); a library with another ID is shown as "Game code not built yet". The start screen's Game Files window then offers "Recompile the
  game" (`runtime/gamebuild.c`, from the game files already there: it needs the generator and compiler of a shipped build, or
  a source checkout started from its own folder with `extracted/` in it; the `PICRECOMP_DATA_ROOT` runs used here count as
  neither). Tested natively on S2 (2026-11-20): the build finishes, the id file has both lines and the game runs from it. The
  importer refuses the same version again ("already in the library"), so the other way is to delete the game first. The
  launcher's old "choose a folder, set up the game files" part (it only accepted another project's title ID) is gone; Add
  a game (the importer) is the way in. `nxpkg.c` and `romfs_build.c` are left for a possible native importer.

## PICROSS S+

An older build (its `sdk`, `subsdk0` and `main` all differ from the others; Unity code compiled with other vtable offsets),
id `0100d7b01ae58000`, update `0100d7b01ae58800` v65536, main build ID `5eb832c8...`. It installs (generate, compile) and runs:
the title screen at 60 fps and on into its e-Picross puzzle select (seen with scripted A presses); nothing beyond that has
been played. What it took, in order:
- `rtld` normally sets `nn::ro::detail::g_pLookupGlobalManualFunctionPointer`; `boot()` gives it a scratch word (the SDK
  stored through a null).
- Its C# exceptions need landing sites of their own (other addresses and vtable offsets than S's): at the time two were
  found by hand from the runtime's "Guest unwind has no continuation at <pc>"; the generator finds them all itself now.
- Memory: this SDK maps physical memory a page at a time (976 `MapPhysicalMemory` calls): the kernel's region table
  (1,024 entries, now 4,096) filled up. Touching pieces of that type are one region now and `UnmapPhysicalMemory` removes a
  range, not a start address.
- Shaders: its vertex shaders have `VOTE.VTG` (`0x50e0`, a vote of the vertex / tessellation / geometry stages: no
  register is written; Eden and yuzu leave it empty) and, between their two halves, `EXIT.FCSM_TR` (condition code `0x1c`):
  a conditional exit that is not taken (Eden stubs its flow test to false). We took it as an unconditional exit, so the
  position was never written and all 1,800 draws a frame were black. `tools/test_translate.sh` checks the second one
  (a plain EXIT still ends the program, an `FCSM_TR` one does not); its pinned reference commit was missing from this
  repository, so run it as `GLSL=0 REF=HEAD tools/test_translate.sh 16000 8 msl` (needs a clang build only; the sampled
  differences are all programs with those two instructions).
Its DLC (`Picross e2` to `e9`) are entitlements only: the puzzles (`Title_E2`..`E9`, `IdxRes_Unlock_Aoc`) are in the game's
own RomFS and each package's data NCA is a 322-byte text banner. The importer lists the packages it finds beside the
game as `"addons": [1..8]` in `title.json` (`addon_indices()` in prepare.py; id = game's with the low 12 bits cleared + 0x1000
+ index) and `aoc:u` reports them (count / list / base id). Seen: all eight listed in a run (the game asked "8 of 8");
a game imported before this needs a re-import (or `"addons"` added by hand). An unowned DLC's "go to the store" opens
the Shop applet (id 0x14) and the game aborted: it wants a 0x1010-byte output storage back (am.c now gives that;
seen working by the user with an empty save and no add-ons: Yes returns to the menu, no crash). The game keeps an unlocked DLC
in its save, so removing a package later does not lock it again (test the shop with `PICRECOMP_DATA_ROOT` pointing at a fresh folder). S+ comes last on the start screen (the cards sort "+" after
the digits).

- Video > Internal resolution: the `1x` entry is now called **Native 1080p** (1920 x 1080 whatever the window; the
  saved value is still `resolution = 1x`). Every game's scene targets are 1920 x 1080 (measured on all ten at their
  title screens); Kemono at Native 1080p shows its title screen right, in a 1280 x 720 window. Our own UI (menu,
  start screen) is drawn on the window itself, after the game image, so it is not affected by this setting.

## KEMONO FRIENDS PICROSS

The first licensed game (2018, between S2 and S3): id `01004b100bda2000`, v0 (no update), main build ID
`b3a956031cee684b8a4df42874cb3e00`, manifest `titles/kemono-friends-picross.json`. It imports (about four minutes) and
**plays**: the title screen, the Japari Park main menu, into Picross and its tutorial question, at 60 fps, seen in
screenshots from scripted presses (nothing past that has been played). It has the oldest SDK so far, and what it took:
- Its build ID ends in a zero byte. `nso.py` strips trailing zeros (30 hex digits) while the runtime reads 16 bytes
  (32): `prepare.py` and `generate.sh` take the 16 bytes now (`nso.py` is part of `GAMELIB_ID`: touching it makes every
  installed game "built for another version", so it was left alone).
- `nn::init::Start` has four arguments there (no `fini`): `main.c` looks the older symbol up when the newer is missing.
- The sdk's unwinder resume is the older libunwind jump (`mov sp, x1` earlier, ending `ldp x0, x1, [x0]; ret`): the
  generator finds either form by its code.
- `nn::vi::SetLayerScalingMode` is hooked (the SDK looked for the vi service).
- Present is at most one a vsync (`nvnQueuePresentTexture`): the game does not wait for the vsync itself and ran at
  1000 fps. A present now waits for the next tick of the 60 Hz timer in `vi.c` (started on demand, `vi_vsync_start`);
  the games that wait for the vsync themselves are already past a tick, and every game measures 60.0 fps (an earlier
  fixed-interval version let these two run at 63.8).
- A multisampled (2x) 1080p scene target resolved into the 720p texture the output pass samples was dropped (a copy
  between different sample counts); it is resolved into a temporary image and scaled now.
- Its background on the start screen is `Common_BG001` (no 000).
Not done: its in-game button prompts (the face-button layout patch looks for S's 64x64 icons); DLC (it has none here);
nothing past the first tutorial question.

## PICROSS LORD OF THE NAZARICK

The second licensed game (released 2019-07-25, between S3 and S4): id `010012100e8dc000`, v0 (the folder it came in also
held an update for OVERLORD: ESCAPE FROM NAZARICK, a different game: the importer ignores it), main build ID
`2424aca935dd5f4188d5bafb92a83504`, manifest `titles/picross-lord-of-the-nazarick.json`. It imports and **plays** to its
story prologue ("The Fortress City E-Rantel ..."): title screen, a button press, the opening scenario text, at 60 fps,
seen in screenshots from scripted presses. The same older SDK as Kemono's (four-argument `nn::init::Start`, the older
unwinder), so those fixes carried over unchanged. What was new:
- It asks `nvnDeviceGetInteger` for the NVN API minor version and traps unless it is above 309 (the others got 207).
  Every game is told 350 now: the base builds of S3 and S4 trap below 309 and 311 too, and none checks an upper bound.
- Its exception landing pads are code of their own (real clean-up code, not one jump to a shared handler): the generator
  labels every pad from the compiler's exception table and wraps the calls that reach it. Three sites were once found by
  hand; nothing is hand-found now.
Not done: past the prologue; its in-game button prompts follow S's icon layout only; long names on the start screen.

## PICROSS S GENESIS & Master System edition

The 2021 edition between S6 and S7 (released 2021-08-05): id `010089701567a000`, update `010089701567a800` v196608
(display version 1.2.1), main build ID `1466f9109e267e458c517ada6b115e99`, manifest `titles/picross-s-genesis.json`. It
imports (about five minutes) and **plays** to its Main Menu and the Picross tutorial question, at 60 fps, seen in
screenshots from scripted presses; nothing beyond that has been played. Its sdk is S's (same build), so only the title's
own parts were new:
- The translator: a predicated bindless TEX (the title screen's grid shader, one draw a frame) failed with "unresolved
  bindless tex". The handle is built by MOV and LOP.OR from the driver's c2 tables under a predicate, with IPAs and
  arithmetic in between, and the resolver gave up at any instruction it did not know. It now stops only at an
  instruction that can write a predicate (`writes_predicate`: the SETPs, PSETP, CSETP, R2P, VOTE, SHFL, LOP / LOP3 with a
  predicate result). `tools/test_translate.c` has the shader (`check_predicated_tex`): it fails on the old translator.
Not done: past the tutorial question (the retro "Clip Picross" and the emulated-console parts have not been tried).

## PICROSS S NAMCO LEGENDARY edition

The 2024 edition after S+ (released 2024-05-30): id `010015101def8000`, update `010015101def8800` v65536 (display version 1.0.1),
main build ID `aa44d9a3064ec70be4f8835f3e3f14aa`, manifest `titles/picross-s-namco-legendary.json`. It imports and
**plays** to its Main Menu (Picross, Mega, Color, Clip, Extra, Tutorials, Options), at 60 fps, seen in screenshots from
scripted presses; nothing beyond the menu has been played. Its sdk is S+'s (same build), so the kernel and Maxwell work
from S+ carried over. What was new:
- Its two packages have **no tickets** (standard crypto), and the importer told a package's game by its ticket's file
  name. A package without one is now read through its own content metadata: only its small `.cnmt.nca` files are taken
  out of the package and decrypted with `hactool` (header key only), which gives the title ID, kind and version
  (`package_metas` in `prepare.py`; the keys and the extractor are found before the packages are matched). There is no
  filename fallback: keys that cannot read the package stop the import with that message. Checked with the two packages under names with no ID in them
  (identified and imported), and with a ticketed game (S2) unchanged.
- Two exception sites in the unified `call` form (a `blr` and a direct call).
- Two fragment shaders (the CRT-style title and menu background, two draws a frame) did not translate: **HSETP2 with a
  constant-buffer operand** (`7e8x` / `7c8x`: the layout checked against Eden's HSETP2_cbuf: compare 49-52, H_AND 53,
  |b| 54, -b 56, the word one F32 for both halves) and a **bindless handle copied with MOV** before the sample (the
  resolver follows `MOV Rd, Rb` back to Rb's instructions). Both are in `tools/test_translate.c`
  (`check_copied_handle_and_hsetp2_cbuf`: they fail on the old translator). `PICRECOMP_LOG_TEX_FAIL=1` prints every
  instruction before a bindless TEX that did not resolve.
Not done: past the Main Menu.

## PICROSS Records of The Shield Hero

The October 2024 game after Namco Legendary (released 2024-10-03): id `01004c602018a000`, update `01004c602018a800` v131072
(display version 1.0.2), main build ID `4c1cf04820df7f24166defcac3409e4a`, manifest `titles/picross-shield-hero.json`. It
imports and **plays** to its Main Menu (Picross / Mega, Color, Episode, Extra, Clip, Tutorials, Options), at 60 fps, seen
in screenshots from scripted presses; the title screen shows its image carousel. Nothing past the menu has been played.
Its modules come from a newer toolchain, which was the new thing:
- **RELR relocations.** All three modules have `DT_RELR` (tags 0x23-0x25, the compact relative relocations), which the
  loader ignored: the SDK's first constructor read a module offset as an address (`signal 11 accessing 0xb19020`).
  `runtime/loader.c` applies them now (an even entry is a word to add the base to, an odd one a bitmap of the 63 after).
- The generator took code pointers stored in data (thread entries, vtables, callbacks) as function starts from `.rela`'s
  RELATIVE entries only, so a thread entry was "no function at 700115db04". `nso.py` reads `DT_RELR` into `relocs` now.
- Two exception sites (a `blr`, a direct call), in the unified `call` form.
- **Episode background garbled** (vertical stripes, seen by the user against Eden). The episode pictures are 800x450
  textures the game only uploads (`CopyBufferToTexture`, straight into the Vulkan image, never into guest memory) but
  flags as render targets, so they count as "scalable" (a 16:9 target is allocated at the output scale). One was created
  at scale 1.0 and, when the scale became 0.667, reallocated empty: its guest memory is not the picture, so it showed
  whatever was there. `tex_backend` now scales the old image into the new one when it reallocates a colour texture
  (`vkCmdBlitImage` in the upload command buffer: the reallocation can happen in the middle of a draw's setup, inside a
  render pass, where recording into the main command buffer lost the device: found with `PICRECOMP_VK_VALIDATION=1`).
  `PICRECOMP_LOG_UPLOADS=1` logs every buffer upload and every such reallocation. Checked: all fourteen games reach their
  main menu at 60 fps with no fatal error; S, S+, Nazarick, Namco and Kemono with the validation layer too (one
  message, on Nazarick, once, not seen again).
Not done: past the Main Menu.

## Picross: Juufuutei Raden's Guide for Pixel Museum

The June 2025 game after Shield Hero (released 2025-06-05): id `01003d2020aba000`, update `01003d2020aba800` v327680
(display version 1.0.5), main build ID `7cff713af0d89e278142db4fd0279a4b`, manifest `titles/picross-juufuutei-raden.json`.
The card is named without the "™" marks the game's own title carries (the manifest's `name` is ours; the packages' file
names have them too, which nothing reads). It imports and **plays** to its story: the title screen (Start Game /
Options), a press, and the curator's introduction in the museum ("Thank you for visiting the Pixel Museum today ..."), at
60 fps, seen in screenshots from scripted presses; the puzzles themselves have not been reached. RELR modules like
Shield Hero's. What was new:
- The SDK's own constructors (its libc registering the module) allocate through `VammManager::AllocateAddressRegion`,
  which needs `nn::os::InitializeVirtualAddressMemory` to have run: a null region (`signal 11 accessing 0xc0`).
  The runtime called it before the game's constructors only; it is before the SDK's now (`rtld_init_sdk`), once.
Not done: the puzzles; its languages (English and Japanese only: the language list already follows the game's flags).

## Piczle Cross: Story of Seasons (work in progress, 2026-11-20)

Unreal Engine 4 (not a Jupiter title), base 01003F201C5BA000 + update v131072 (manifest in `titles/`). Imports
(about 5 min of generation) and boots; runs at 60 fps with no fatal error, but the screen stays black.

Fixed on the way (uncommitted at the time of writing): sparse .eh_frame made the jump-table replay quadratic
(`gen.py` bounded once-per-table replay, `_after_last_write`); Makefile `ls -S` over the 128 KB argument limit;
VAM / alias-region sizing (64 GB alias, engine reserves ~62 GB); GetInfo 20; NVN device ids 61/64 (max texture
size); texture refcount after nvnTextureFinalize; vi frame-history and HID touch HLE; TLD / TLD.B; the movie
library reads from the RomFS image, Decoder keeps the DecoderEvents pointer, Extractor slots 2 and 13.

Storage texel buffers (2026-11-20, Vulkan on Linux, validation layer clean): the three compute shaders that failed were not
image shaders but SUST.P / SUATOM on 1D *buffer* textures (UE4's RWBuffer clears and counters). Now translated
(`emit_sust`, `emit_suatom`: untyped SUST of four 32-bit components, SUATOM ADD/MIN/MAX/AND/OR/XOR/EXCH on 32-bit unsigned;
handle resolved from the driver c[2] image table, slot k at 0x120 + 8k; test: `check_buffer_images`). Backend: the
reflection reports STORAGE_TEXEL_BUFFER, `texel_view_for` makes a buffer view over the texture's memory (heap import, or
`texel_import` for the engine's own mappings), buffer textures no longer allocate a width-sized image, the data goes in as
raw bits through a `uimageBuffer` (32-bit components only: narrower formats log "stored as raw bits"). All compute
programs now run (1080 dispatches / 120 frames, 0 skipped). Still not done: INC/DEC atomics, signed MIN/MAX, typed SUST,
SULD, buffer textures *sampled* (samplerBuffer, `[tex] type mismatch ... target 10`).

The screen is still black. Found on the way: the movie player's extractor thread (0x7003c02de4) wants
GetSampleTrack = 0x2000 at the end of a file (to send its decoders the end-of-stream input) - it polled 1 forever; the
decoder-output surfaces are 0x0 (`flags 2`) and now take the frame's size. Only 36 frames are ever decoded (presents 7-36), then
the game holds all 6 output buffers and stops - and no draw ever samples a video texture (`PICRECOMP_COPY_TRACE=<frame>`:
every frame is 5 draws: the scene composite, tonemap, Slate boxes). So the engine never gets to showing the movie: what it is
waiting for is unknown (guess: something the title's boot waits on, not the decoder). The game's log function was not found
(`~/temp/gdb_msg.txt` breaks on a stale address).
UE4 log route (step 1): a dead end - the shipping build has no log category strings (only `LogLoad`), so the guest log function can't be
found that way. Decompiling the player instead (Ghidra, `~/temp/ghidra`, project `p_pz`; functions by guest address, `nm picgame.so | grep f_<addr>`
gives function starts): the output thread is 0x7003c03fc4 (AcquireOutputBuffer = slot 12), GetOutputBuffer is called from 0x7003c056f8, which builds the
Buffer with the game's NVN texture and queues the sample; 0x7003c039d8 releases queued samples in order, only while
`fn(sample + 0x10, 0) != 2` (fn = the import at 0x70060bc0b8 = nvnSyncWait, timeout 0: confirmed with a trace - 22 calls from 0x7003c03a44, ours returns 0 = signaled, so every
release passes; the sync is NOT what stalls). What stalls: the engine's fetch function 0x700112f1a4 (the only caller of the GetOutputBuffer wrapper
besides 0x70011745c4) starts with `if (10 < *(int *)(this + 0x90)) return;` - its list of decoded frames waiting to be shown (vector at +0x88, 0x10 per
entry) is capped at 11, which is exactly the 11 frames per movie we see (presents 7-17 and 26-36). Nothing ever drains that list, so the
engine stops fetching, the 6 output buffers stay acquired and the decoder backs off. **Solved (2026-11-20): Piczle shows its logo movies and the title screen** ("Press A to start", v.1.021; Vulkan on Linux, hidden window).
The cause was in our extractor, slot 13 (GetSampleMetaData): it was a stub that *wrote into the guest's 8-byte MediaData handle as if it were
the host struct* (a memory corruption, and the reason of the crashes in 0x7003c02de4) and gave no data. UE4's player (0x7003c02b28) reads
"durationUs" per sample (and for video "width", "height", "cropLeft/Top/Right/Bottom") from it; its sample queue's FetchVideo refuses a sample
whose duration is below 1, so the engine never played the media (11 frames queued, none popped). Now `ex_sample_metadata` fills them (crop
edges inclusive), through `md()` like the other MediaData slots. How it was found: gdb write-watch on the sample queue's count (`$cpu[0]` is x0 =
`this` at a generated function's entry; vtable slots read from guest memory), Ghidra on the player (0x7003c02b28 / 0x7003c03fc4 / 0x7003c056f8 /
0x700112f1a4 / vtable 0x70_05a32658 slot 7 = FetchVideo). `PICRECOMP_LOG_MOVIE=1` logs each frame with the present it reached the game's texture.
Shader gaps (2026-11-21): TLD4 / TLD4.B (`emit_tld4`: gather, 2D / 2D array / cube, AOFFI, depth compare) and VMNMX (`emit_vmnmx`: the signed clamp
`min(max(Ra, Rb), Rc)` UE4 uses for dynamic indices; yuzu's field layout; MRG / ACC secondary ops, SAT and CC are not translated) now translate, test
`check_gather_and_vmnmx`; the vertex shader that failed compiles. Remaining: the "indirection texture" was a misreading. The handle search (`separate_texture_pair`) stopped at an SSY / SYNC / BRA between the
handle's MOV / LOP and the TLD.B (control flow has a zero low byte, which looked like a write to R0): fixed (`is_control_flow`), and the three
fragment shaders translate. What really skipped ~66 draws a frame: programs with a vertex shader and no fragment shader (stage mask 0x21 =
vertex + compute: UE4's depth / shadow passes) were refused by `get_program`; they now get an empty fragment shader (0 skipped, ~106 draws a
frame; the title looks the same, so those passes are not what is missing from it). `shaderImageGatherExtended` is enabled (textureGatherOffset).
Seen: a blurred circular iris-out at the end of the logo movies (in both builds: the game's own transition).
**The title screen is complete (2026-11-21):** sky, clouds and sea reflections behind the logo, "Press A to start" (Vulkan on Linux). Two things did it:
1. *Buffer textures sampled with TLD.* A TLD does not tell a 1D texture from a buffer texture, so a program is a variant per set of buffer slots:
   `ShaderInfo.tld1d_mask` (slots only a 1D TLD used), `ShaderKey.buf_mask` (hashed into the program key), `draw_` resolves the bound textures after
   the first translation (`buffer_slots`) and asks for the variant (log line "buffer textures in 1D slots"). The variant declares a `usamplerBuffer`
   (the registers get the bits), the descriptor is UNIFORM_TEXEL_BUFFER over the texture's memory (`texel_view_for` / `texel_import`, formats through
   `uint_view_format`). A ring slot of `texel_import` reused while recorded commands still pointed at it lost the device: dropped imports now wait in
   a graveyard until their submission completes.
2. *Global memory outside the guest heap.* The tonemap shader loads the exposure (`LDG` at the address of storage buffer 0) and every storage buffer the
   engine binds lives in its own VAM mappings, outside the 3.25 GB heap window the shader's `gmem_` covers: the load returned 0 and the picture was black.
   Now each bound storage buffer outside the window is imported with a device address (`far_import`, VK_KHR_buffer_device_address, enabled when the
   device has it: `g_vk.bda`), `gfar_table` hands the shader a table of them (binding 22, `GFAR_BUFFER_INDEX`), and LDG / STG / RED fall back to
   `gfar_ld` / `gfar_st` / `gfar_stb` / atomics through `GFarRef` (translate.c `far_word` ...). Not done: other atomics (ATOM with a result), pointers that
   do not come from a bound storage buffer.
Also: depth-only programs draw (empty fragment shader); the handle search skips control flow; TLD4(.B) and VMNMX exist.
Not tested: Vulkan validation on (the validation layer itself crashed once after the depth-only change), the other games, Windows.
Also still: A (scripted `key:Z`) does not start the game - not looked at.
**Playable (2026-11-21, Vulkan on Linux, hidden window):** title -> menu -> puzzle list -> a puzzle with clue numbers, filled cells, X marks and
the cursor; How to play (the TV scene with its characters), Settings, Almanac, Credits (night sky, scrolling text). What it took after the
title screen: scripted input uses SDL names - **A is `key:K`** (B `J`, D-pad = arrow keys: `key:Down`; the defaults of runtime/hle/bindings.c;
`pad:` takes SDL gamepad names: `east` for A), the keyboard / mouse / six-axis states are empty HLE (junk from the unfilled HID shared memory
held a key down: the menu walked to its last item by itself), and the big one: **`ReportCounter` type 1 (SAMPLES_PASSED, occlusion queries)
answered 0** - UE4 culled every dynamic primitive (characters, the puzzle's cells, numbers and cursor were drawn into the shadow maps
only). It now reports a visible result (0x7FFFFFFF); no real query. Also: compute shaders get the buffer-texture variants; the handle
search accepts a predicate written between the handle and its TEX when the handle's own instructions are not predicated.
**Fixed (2026-11-24) - the main-menu flicker** (seen by the user on a real window): the lit buildings of the title scene flipped at random, frame to
frame, between their colours and a dark green. Cause: UE4's forward light grid (compute shaders 7df2262100 and 7e014c2b00) reserves its light
links with one atomic per warp, each lane taking `popcount(ballot & SR_LANEMASK_LT)` as its offset; the translator read `S2R SR_LANEMASK_LT`
(0x39) as 0, so every lane of a warp wrote the same link and which light survived was a race. S2R now gives the lane id (0x00) and the lane
masks (0x38-0x3c) for a 32-wide subgroup (test: `check_lane_masks` in tools/test_translate.c). Verified: 60 consecutive frames at the same
brightness (before: 87 / 138 at random), screenshots of the title, menu, puzzle list and a puzzle (Vulkan on Linux, hidden window). The
dead ends on the way (sync submit, fence flush, per-pass submissions, copy aliasing, cbuf hashes) all pointed away from timing on the host:
a race inside a shader is invisible to them. Subgroup width: the translated shaders assume 32 lanes (ballots, shuffles, lane masks). NVIDIA is always 32; for AMD RDNA
and Intel, pipelines whose shaders use these (ShaderInfo.uses_subgroup) now ask for 32-wide subgroups (Vulkan 1.3 subgroup size control); the log
says `[vk] subgroups: <min> to <max> wide, 32 can be asked for in: ...`, and warns once per stage when the GPU cannot (AMD GCN: always 64 -
those shaders may draw wrongly; no fallback yet). Tested only on NVIDIA, with `PICRECOMP_VK_SG32=1` forcing the request: validation clean, the
title scene unchanged. **Needs testers on AMD and Intel** (look for that log line and for the title scene's lit buildings).
**Fixed (2026-11-24, reported by the user) - no ground, black flash in the intro.** (1) The title scene's ground was missing (the sky dome
showed below the houses, which I had taken for water): the NVN format list inherited from LASRecomp put BGRX8 at 0x98; it is 0x99 (BGR565,
BGR5, BGR5A1, A1BGR5 at 0x95-0x98, then BGRX8, BGRA8, BGRX8_SRGB, BGRA8_SRGB). UE4's landscape heightmap and light-attenuation target are
PF_B8G8R8A8 = 0x9A and were read as BGRX8_SRGB: sRGB-decoded heights. Seen working: ground, dirt path and grass on the title, the farm behind
the menu and the puzzles. **This touches every game** using 0x98-0x9C (none of the PICROSS games was seen to; not re-run). (2) A one-frame black
flash in the logo movie (LOGO_splashes.mp4): UE4 decodes each frame into the next texture of a pool and a pool texture has no backend until
first drawn; `renderer_upload_rgba` dropped such frames. They are kept now and handed over when the backend is made. Seen: frames 160-385 of
the intro shot one by one, no black frame, smooth fades (before: frame 178 black).
Debug: `PICRECOMP_DEBUG_FRAME=<n>` writes `build/pass_NNN.png` of that frame's passes, `PICRECOMP_COPY_TRACE=<n>` lists its draws, the log says
which no-op NVN commands and shaders taken for compute the game used. The other games were subsequently rebuilt in the batch
below; the statement that their libraries were stale applied before that rebuild. Current gaps: Windows, saving (the game opens
the save data file system ~60 times), audio, longer play (finishing a puzzle, the farm), `Journal`, and replacing the experiment
hooks `PICRECOMP_TEMP_DI` / `PICRECOMP_TEMP_KEEP_SRS` with proper handling (now fixed by the startup work above).
Later Vulkan validation coverage is recorded above.

**All 21 games rebuilt and checked (2026-11-24)** after the merge of `piczle-cross` into main (library id changed by the generator work):
each game's library rebuilt from scratch, then one scripted run each (A every ~8 s, 6 screenshots; Vulkan on Linux, hidden window).
All 21: no crash, no shader translation failure, no subgroup warning, 60 fps, nothing drawn wrong in the screenshots. Scripts (scratch, not in the repo): `~/temp/rebuild_all.sh g01 ...`, `~/temp/testall.sh`.

## Piczle Cross: Rune Factory (started 2026-11-24)

Title 0100147020984000 + update 0100147020984800 v262144 (1.04, main build 1ad16d84...), Unreal Engine 4 like Story of Seasons;
initially tested with the same two experiment settings (`PICRECOMP_TEMP_DI=61,64 PICRECOMP_TEMP_KEEP_SRS=1`), now removed.
Imports and builds (11 min).
Seen working (Vulkan on Linux, hidden window, scripted input): logo, title, profile selection, profile creation (Down x10 to
"Create profile", A), the tutorial with its puzzle board, the guide character and the animated background, 60 fps there.
What it took: LEA (27 shaders failed on 0x36d7...), compute shared memory LDS / STS / ATOMS and ATOM with a result, the texture
handle search past FSETP and through IADD Rd, RZ, Rb copies (vertex shaders: ~1650 draws a frame were skipped; the town behind the
menus was black), and audout ignoring an uninitialised buffer data offset (a crash in SDL at the first sound).
2D / 3D image stores (SUST.P.2D / .3D) work since 2026-11-24: an image a compute shader stores to is recreated with STORAGE usage on
first use (contents copied; only those images, storage usage can cost compression), viewed as UNORM for sRGB, float or integer
image type by the bound format (a shader variant); test check_image_stores. Here: two 1920x1080 B10G11R11 targets. The main
menu then shows the full town in daylight, the puzzle list its scene. Around 100 draws a frame were still skipped at that
stage; the 2026-11-26 SULD fix above removes the remaining observed skips in the RFN008 run.
Once the town behind the profile screen stayed black in one run and not in the next five (timing; not reproduced since).
Debug: `PICRECOMP_DUMP_FAILED=1` writes the code words of a shader that fails to translate (build/glsl/<addr>_<stage>_failed.txt).

## Piczle Puzzle & Watch Collection (started 2026-11-24)

Title 01001a30155b6000 + update 01001a30155b6800 v65536 (1.01, main build 02dd54e9...), Unreal Engine 4.26.2; same two experiment
settings as the other Piczle games were used initially; no startup flags are needed now. Imports and builds (9 min).
It asks nifm whether it may go online at startup: the logging stub
gave no IGeneralService and nn::nifm RequestClient's constructor read through null; runtime/services/nifm.c now answers as a
console without network (requests never accepted, result NIFM 1111). Early synchronous-compilation tests reached frames after
~4 s warm or ~23 s cold, with six runs without a crash. Another run ended in the game's own deliberate
crash, a store of 0xE0000002 to address 3 (main 0x70008d01b0), reached when a worker loop (main 0x70008d01f0 -> 0x70008d0310) finds
`now - [obj+0x88] > [obj+0xa0]` with `[obj+0x140] > 0` - a watchdog on some timestamp, likely the frame present time.
Synchronous cold compilation with Vulkan validation reproduced that timeout. Since 2026-11-26, its manifest enables
asynchronous compilation: cold tests presented frames after ~4 s, initially black, then reached the main menu without the crash.
Validation-enabled menu tests also passed; unused-fragment-output warnings remain. If the watchdog returns during longer play,
trace the writes to obj+0x88. Its compute image-store shaders now translate (including a 64x64x16 RGBA16F 3D image and 2D R16F
images). The Select game / Unbox / View / Options menu runs at about 60 fps after compilation; startup scenes can be slower.
The float-sampler/RGBA8_UINT mismatch at binding t7 is fixed by the sampled integer texture variants described above.
Not played past the menu yet.

## What to test and watch for

- Piczle Cross: Story of Seasons (merged into `main`): watch the intro and the title / menu on a real window: no black flash in the logo movie, ground
  (dirt path, grass) under the houses, the buildings keep their colours with no flashing. The title scene's colours change between boots
  (a seasonal variant, it seems). Then play a puzzle; report any other flicker or dark spots.
- Saves: run S (play.sh) once, look for `[fs] save data of earlier versions copied` in the log and for your progress. The
  save that was in `.picrecomp/runtime/save/save_0/all.dat` may be S2's, not S's, if both were played before this change.
- Extra puzzles from earlier titles (PICROSS S4 on): the game does not read the earlier titles' saves; it asks the console's
  play history, `IApplicationFunctions` command 110 `QueryApplicationPlayStatistics(ids)` (SDK: 8 bytes per id in, 0x18 bytes
  per statistics entry out, the count back as an int), for S, S2 and S3 (at its indices 2 to 4; in S4's code at
  `0x129ed00`) and unlocks a title's extras when the 32-bit value at +0x10 of its entry is above 0. The runtime answered
  with an empty reply, so the entries were whatever was on the game's stack: S4 always "detected" all of them. It now
  answers from the save folders: a title counts as played when `save/<program id>/` holds a file (`runtime/services/am.c`,
  logged as `[am] play statistics of ...`). Seen in S4: no earlier saves, no unlock message; with only an S2 save,
  "Picross S2 play data detected. Extra puzzle E004 unlocked." (S and S3 not). A game keeps what it unlocked in its own
  save. (S2 and S3 never ask.) The read-only mount of another title's save (`fs.c`) is not used by these games.

- Pick another game card on the start screen: the window should stay and go straight into that game (checked by the
  user with the cards; also tested with `PICRECOMP_SWITCH_TO`). The Vulkan validation layer logs nothing after a switch (S to S2, checked).

- Run `tools/picross/play.sh .picrecomp/prepared-game` from a terminal and play through a few puzzles, quitting and
  relaunching once to check that progress is kept.
- Press Plus (Enter) at the main menu: the game should stay responsive and return to the menu.
- Anything that freezes with 0% CPU is a deadlock; follow "Debugging a hang" in `docs/ARCHITECTURE.md`.
