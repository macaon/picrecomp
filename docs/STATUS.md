# Status

Updated: 2026-11-17. Platform tested: Linux x86-64, Vulkan, NVIDIA RTX 5070 Ti (Wayland, PipeWire audio).
Windows has not been built or run for PICROSS S.

## Pinned game

| | |
|---|---|
| Game | PICROSS S, base title `0100BA0003EEA000` (v0), update title `0100BA0003EEA800` v524288 |
| `main` build ID | `260797018fefb8459d1bf07815e71c23` (the updated executable) |
| Modules | `main` (120,117 functions), `subsdk0` (13,670), `sdk` (39,292), loaded at `0x7000000000`, `0x7008000000`, `0x700C000000` |
| Generated | `generated/` is about 645 MB of C |

Patch and HLE addresses are valid only for this build. The generator refuses any other.

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

- PICROSS S4, S5, S6, S7 and S8 have manifests. Each was installed here (prepare, generate, compile, about four minutes)
  and started to its title screen: 60 fps, no fatal error, no unimplemented call, in a scratch data root. S4 to S6 and
  S8 use the same `sdk` and `subsdk0` as S; **S7's are different** (a newer SDK), which works because the sdk's import
  of `nninitStartup` and the unwinder's resume function are found by what they are (`loader_import_slot`,
  `fix-exception-resume.py`), not by address. S8 has no update package (the base game is the whole game; `prepare.py`
  takes a manifest without `update_id`). The exception landings of each were derived by matching S's two patched
  functions in its `main`; the same method reproduces S2's and S3's manifests exactly. Nothing past the title screen
  has been played on S4 to S8.

- The start screen's game cards wrap: at most five to a row (the "+" counts), more go to a second row on the same
  columns, and "+" is left out once every game with a manifest in `titles/` has a card (without the manifests next to
  the program, as in a package, it stays). Seen in screenshots with 8 cards (5 + 3, no "+") and 6 (5 + 1 + "+"); moving
  between the rows with a gamepad or the arrow keys has not been tried.

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
  `title.json`. Seen: S 1.6.1, S2 1.5.2, S3 1.5.2, S4 1.4.1, S5 1.3.1, S6 1.3.1, S7 1.2.0, S8 1.0.0. Games added before this
  have no such field; the line is left out for them (the ones here were filled in by hand from their packages).
  With the mouse a click chooses a card, the Play button at the bottom (lit while the chosen game can be played) starts it,
  and so does a double click; A / Enter / Start play it from the cursor. Settings is the "F1 Settings" hint, which is a
  link too. Seen with scripted clicks (`PICRECOMP_LAUNCHER_KEYS="frame:click_<x>_<y>"`): a click chooses, the Play click starts.

- Updates. A game's update is a whole new build of its `main` (S 1.6.1 to 1.6.2: 6.86 of 7.24 million code words differ;
  `sdk` and `subsdk0` were identical), so a new version means regenerating and recompiling that game (about four minutes),
  nothing incremental. `titles/` has one manifest per supported version (`picross-s.json` is v524288 / 1.6.1,
  `picross-s-v589824.json` is v589824 / 1.6.2, same `slug`). The importer finds the newest supported update among the
  packages in a folder (its version from the package's `.cnmt.xml`, else the `[vNNN]` in its name, checked against the
  real metadata when it is opened) and mentions a newer one that has no manifest. Adding a game that is in the library
  with another build builds the new one beside it (`.<name>.new`) and swaps it in when it is done (the old one is kept
  if the build fails); if only the data changed (all three executables' build IDs equal) the library is reused and
  nothing is compiled. The same build again is refused ("already in the library (version 1.6.2)"). Seen: S 1.6.1 to
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
  description until a row was selected (Audio showed Video's); each page starts with its own now.

- Each game has its own muted background on the start screen (and behind Settings): the colour wash behind its title
  screen, `Common_BG000` in `Data/sharedassets5.assets` of its RomFS (found in all eight games: S a rainbow wheel, S2 a
  beach, S3 green light, S4 a sunset, S5 a night city, S6 a sunlit room, S7 snow, S8 a nebula), read at import time by
  `tools/picross/art.py` into `<game>/background.jpg` (never committed; UnityPy, optional: `pip install UnityPy==1.25.4`
  into `tools/venv`). `art.py <game folder>...` adds it to a game added before (done here for the eight installed). The
  start screen keeps one texture per game, dims it evenly (more for a bright picture) and fades the bottom for the lower
  line; without a `background.jpg` the plain gradient stays. The old shadow banners (for the left-hand menu) are gone, and
  the title-movie frame it used to look for (`romfs/region_common/movie/title.webm`, a Zelda path) with them.

- Controls: Settings > Controls has a "Face buttons" row, Switch layout (A on the right, the default) or Xbox layout (A at
  the bottom, so the letters on a PC controller match the game's): it applies the four swaps to the controller bindings
  (anything else bound to those buttons swaps over; the table below still rebinds each input) and saves them as
  `bind_pad_a..y` in `picrecomp.ini`. Seen with a scripted click: `A -> south, B -> east, X -> west, Y -> north`.
  The "Button icons" setting is gone: it fed `texpatch`, an empty stub (no PICROSS texture was ever patched), removed
  with its hooks in the Vulkan upload path. The game still draws its own Switch prompts.

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
- `tools/picross/fix-exception-resume.py` splices C into the generated code. It belongs in `tools/patches.py` or the
  lifter.
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
  generated from and is refused for any other game. The only per-title code left was the exception-landing patches;
  they are in the manifests now, and S2's and S3's were found by matching S's two patched functions in their `main`
  (identical code at identical offsets, see `titles/*.json`). S2's install ran end to end here (prepare, generate 2.5
  minutes, compile) and gave a ready card. After the generator changes below S and S2 were installed again (about
  4 minutes each on 16 threads) and both were started in a scripted run through the title, the main menu and the stage
  list to the "Play this puzzle?" popup, with its preview: S2 was checked to there and S through a puzzle's first
  seconds. **S3 has been prepared only, not generated, built or started.** Nothing beyond those paths has been played on S2. `runtime/main.c`'s `sdk + 0xcdd210` is in the shared `sdk` module
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

1. Verify the remaining screens and a gamepad. Look for `[am] ... unhandled`, `[ipc] STUB` and frozen windows.
2. Play S2, then import and play S3. Look for exceptions (the landing patches are untested on S2 and S3), missing
   textures and wrong shaders. The importer keeps `generated/` (about 650 MB per game) for rebuilds; only `obj/` is removed.
3. Finish the launcher: set a game up from the start screen (it still has the base project's setup flow), tell the user
   why a card cannot be played, and test the cards with a gamepad.
4. The cleanup above, then Windows.

## PICROSS S+ (not supported yet)

An older build (its `sdk`, `subsdk0` and `main` all differ; SDK of the 2019 generation), manifest kept out of
`titles/` until it runs (id `0100d7b01ae58000`, update `0100d7b01ae58800` v65536, main build ID `5eb832c8...`). With the
generic fixes in this commit it generates, compiles and starts, then stops: the game throws a C++ exception during
startup (`f_70001b79d4` in `main` calls `__cxa_throw`, right after `aoc:u` cmd 50) and the unwinder finds no handler, so
`__cxa_throw` falls into `__cxa_begin_catch` and faults. Not known yet: whether the exception comes from a service
answer the older SDK reads differently (the `aoc:u` / `ectx:aw` replies are the first suspects), or from a landing
patch S+ needs (S's two sites are not in its `main`: the code differs). Also: `rtld` normally sets
`nn::ro::detail::g_pLookupGlobalManualFunctionPointer`; `boot()` now gives it a scratch word. Its DLC packages
(`Picross e2` to `e9`) are not handled at all.

## What to test and watch for

- Saves: run S (play.sh) once, look for `[fs] save data of earlier versions copied` in the log and for your progress. The
  save that was in `.picrecomp/runtime/save/save_0/all.dat` may be S2's, not S's, if both were played before this change.
- S2 and S3 do not read other titles' saves (the bonus puzzles from an earlier title's save are a PICROSS S4 feature):
  nothing in S2 calls `IsSaveDataExisting` / `MountSaveDataReadOnly`, and no run opened another title's save. For S4,
  `OpenSaveDataFileSystem` with another program id already mounts that title's save folder read-only (or returns
  ResultTargetNotFound), which is what the SDK's `IsSaveDataExisting` / `MountSaveDataReadOnly` need (read in Ghidra).

- Pick another game card on the start screen: the window should stay and go straight into that game (checked by the
  user with the cards; also tested with `PICRECOMP_SWITCH_TO`). The Vulkan validation layer logs nothing after a switch (S to S2, checked).

- Run `tools/picross/play.sh .picrecomp/prepared-game` from a terminal and play through a few puzzles, quitting and
  relaunching once to check that progress is kept.
- Press Plus (Enter) at the main menu: the game should stay responsive and return to the menu.
- Anything that freezes with 0% CPU is a deadlock; follow "Debugging a hang" in `docs/ARCHITECTURE.md`.
