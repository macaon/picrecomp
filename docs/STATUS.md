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
  folder on its first run (the old folder stays as a backup). Seen with scratch data roots: S and S2 each write their own.
  `OpenSaveDataFileSystem` with another title's program id in the attribute (what S2 would send to read S's saves) mounts
  that title's folder read-only, or returns ResultTargetNotFound: written from the IPC layout, never seen called.

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
  icon, `generated/` and its own `picgame.so`. `tools/picross/install.py` (what the start screen's importer runs) does
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

## What to test and watch for

- Saves: run S (play.sh) once, look for `[fs] save data of earlier versions copied` in the log and for your progress. The
  save that was in `.picrecomp/runtime/save/save_0/all.dat` may be S2's, not S's, if both were played before this change.
- S2 reading S's saves (extra puzzles): S2's code imports `nn::fs::IsSaveDataExisting(ApplicationId, Uid)` and
  `MountSaveDataReadOnly(name, ApplicationId, Uid)` (called from Unity wrappers at `0x189ed80` / `0x189ecb0` through a
  table at `0x2bbd740`); a 70 s scripted run through S2's title never called them, so the trigger is further in. A
  `[fs] fsp-srv cmd N` line with S's id in the log would show which IPC it uses.

- Pick another game card on the start screen: the window should stay and go straight into that game (not driven by a
  click here; tested with `PICRECOMP_SWITCH_TO`). The validation layer still logs a few one-pixel copy clamps at the
  deepest mip level of a scaled image after a reboot (harmless so far).

- Run `tools/picross/play.sh .picrecomp/prepared-game` from a terminal and play through a few puzzles, quitting and
  relaunching once to check that progress is kept.
- Press Plus (Enter) at the main menu: the game should stay responsive and return to the menu.
- Anything that freezes with 0% CPU is a deadlock; follow "Debugging a hang" in `docs/ARCHITECTURE.md`.
