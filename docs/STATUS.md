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
- Boots, shows the title screen and the main menu, selects puzzles, solves a puzzle, shows completed-puzzle thumbnails.
- Audio plays (SDL, PipeWire). The runtime's frame stats show 60 fps on the title screen and menus on the test GPU;
  the frame rate while solving puzzles has not been measured.
- Keyboard input (K = A, J = B, arrows, Enter = Plus; see the README) and the in-game F1 menu.

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
- The native start screen (`runtime/ui/launcher.cpp`) still carries the base project's setup flow (a randomizer tab,
  XCI-oriented extraction). It identifies PICROSS S's build ID but is not the supported way to set the game up:
  use `tools/picross/prepare.py` or `tools/picross/launcher.sh`.

- **Other titles.** `titles/picross-s2.json` and `picross-s3.json` exist and `prepare.py` prepares both from the user's
  packages (checked: S, S2 and S3 each give a card with the Switch icon). Their `sdk` and `subsdk0` modules are
  byte-identical to PICROSS S's (same build IDs and sizes); only `main` differs (S2 `427cae75...`, S3 `7615bc81...`).
  Not done: generating and building their code (`generated/` and `build/picgame.so` hold one game; a library's `.id`
  file now names its main build ID and is refused for any other game), the exception-landing patches in
  `tools/picross/fix-exception-resume.py` (main-module addresses valid only for S), and `rtld_init_sdk`'s `sdk + 0xcdd210`
  in `runtime/main.c` (the sdk module is shared, so it probably holds). Neither game has been started.

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
2. Make the code generation per title: patches in the manifest instead of `fix-exception-resume.py`, a library and a
   generated folder per game (`<game folder>/picgame.so` is already searched first), then generate S2 and S3.
3. Finish the launcher: set a game up from the start screen (it still has the base project's setup flow), tell the user
   why a card cannot be played, and test the cards with a gamepad.
4. The cleanup above, then Windows.

## What to test and watch for

- Run `tools/picross/play.sh .picrecomp/prepared-game` from a terminal and play through a few puzzles, quitting and
  relaunching once to check that progress is kept.
- Press Plus (Enter) at the main menu: the game should stay responsive and return to the menu.
- Anything that freezes with 0% CPU is a deadlock; follow "Debugging a hang" in `docs/ARCHITECTURE.md`.
