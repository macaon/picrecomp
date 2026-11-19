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

- The start screen's game cards: five to a row, two rows (ten) to a page, the "+" counting as a card; more games make
  pages. The "+" is always there (more games than the importer knows are coming), as the last slot. The cards sort
  by `"order"` in the manifests (release order: S, S2, Kemono Friends, S3, Lord of the Nazarick, S4 .. S6, Genesis & Master System, S7, S8, S+, Namco Legendary, Shield Hero), read by slug so a game added
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

### Consolidation once every game is in

Adding the licensed games went through the generator's neighbours on purpose (wrappers and post-passes), because
`tools/gen.py`, `lifter.py`, `analyze.py`, `nso.py`, `passes.py`, `patches.py` and `runtime/hle/hle_symbols.txt` are the game
library's ID: an edit makes every installed game rebuild (about four minutes each, once). When the last game is in, fold
them into the generator in **one** change, so the rebuild happens once, and delete the scaffolding:
- `nso.py` reads `DT_RELR` itself; `tools/picross/gen_relr.py` goes, `generate.sh` calls `gen.py` again, and a rebuild from
  the start screen (`runtime/gamebuild.c`, which calls `gen.py` directly) is right for the RELR game.
- `gen.py` labels every landing pad of the compiler's exception tables (`tools/picross/lsda.py` moves next to it); then
  `pad_labels.py` goes, and a manifest site is just `{"return", "landing"[, "callee"]}`.
- One form of `exception_landings`: S, S+ and Kemono still use the older `resume` / `catch` groups; convert them (and
  re-check each of those games for exceptions) and drop the legacy branches of `fix-exception-resume.py`.
- Fold the bring-up loop that lives in scratch scripts (placeholder manifest, read the build ID, import, run, find the
  landing for each "no continuation", patch, recompile) into one `tools/picross/` helper, and move the shared bits of
  `prepare.py` / `install.py` that grew per game into plain functions.
- Re-measure every game afterwards (title screen, a puzzle, 60 fps, no `[shader] ... failed`).

## PICROSS S+

An older build (its `sdk`, `subsdk0` and `main` all differ from the others; Unity code compiled with other vtable offsets),
id `0100d7b01ae58000`, update `0100d7b01ae58800` v65536, main build ID `5eb832c8...`. It installs (generate, compile) and runs:
the title screen at 60 fps and on into its e-Picross puzzle select (seen with scripted A presses); nothing beyond that has
been played. What it took, in order:
- `rtld` normally sets `nn::ro::detail::g_pLookupGlobalManualFunctionPointer`; `boot()` gives it a scratch word (the SDK
  stored through a null).
- Its C# exceptions need landing sites of their own (the code is the same shape as S's, at other addresses and with other
  vtable offsets; S's two sites are not in it). With only the SDK's unwinder patch the runtime reports the landing it is
  missing ("Guest unwind has no continuation at <pc>"); the site is the call before it. Two were needed: a `blr` call
  (return `0x20101f8`, landing `0x2010338`, resume `0x2010348`) and a direct call followed by the function's `ret` and
  the landing (`0x1d35444` / `0x1d354e0` / `0x1d35450` / `0x1d354c8`), both in `titles/picross-s-plus.json`. The patch
  script takes either group alone now.
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
- The sdk's unwinder resume is the older libunwind jump (`mov sp, x1` earlier, ending `ldp x0, x1, [x0]; ret`):
  `fix-exception-resume.py` finds either form by its code.
- Two exception landing sites (one `call`, one `catch` group), found with the new `tools/picross/find-landing.py` from
  the runtime's "no continuation" pc and the compiler's own exception table: it names the call site for a landing pad
  exactly (checked on S+'s two). The importer's `--keep-generated` keeps the code, but the patch script has to be run
  by hand on it after a manifest change.
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

The second licensed game (2019, between S3 and S4, sort order 045): id `010012100e8dc000`, v0 (the folder it came in also
held an update for OVERLORD: ESCAPE FROM NAZARICK, a different game: the importer ignores it), main build ID
`2424aca935dd5f4188d5bafb92a83504`, manifest `titles/picross-lord-of-the-nazarick.json`. It imports and **plays** to its
story prologue ("The Fortress City E-Rantel ..."): title screen, a button press, the opening scenario text, at 60 fps,
seen in screenshots from scripted presses. The same older SDK as Kemono's (four-argument `nn::init::Start`, the older
unwinder), so those fixes carried over unchanged. What was new:
- It asks `nvnDeviceGetInteger` for the NVN API minor version and traps unless it is above 309 (the others got 207).
  `"nvn_api_minor"` in a manifest sets it per title (`settings_title_int`, read from title.json); this one says 310.
- Its exception landing pads are code of their own (`mov xN, x0; cmp w1, #1; b.eq ..; b ..`, or real clean-up code),
  not one jump to a shared handler, so there is no label to resume at. `tools/picross/pad_labels.py` lifts the
  function that holds a pad again with the lifter's own code (`gen.py`, imported: it is part of the library ID, so an
  edit would have made every installed game rebuild) and the function's pads from the compiler's exception table
  (`tools/picross/lsda.py`) as extra labels, checking that the old text is exactly what the lifter gives. A `call`
  group in the manifest is `{"return", "landing"}` (a `blr`) or with `"callee"` (a direct `bl`) now and resumes at the
  pad itself; `find-landing.py <game> main <pc>` prints every call of the range as manifest entries (the table says
  which range reaches the pad, not which call throws, so all are listed). The groups may be lists. Three sites were
  needed here (one `blr`, one old-style `catch`, one range of three calls).
- `fix-exception-resume.py` takes the older forms (S, S+, Kemono) unchanged.
Not done: past the prologue; its in-game button prompts follow S's icon layout only; long names on the start screen.

## PICROSS S GENESIS & Master System edition

The 2021 edition between S6 and S7 (sort order 075): id `010089701567a000`, update `010089701567a800` v196608
(display version 1.2.1), main build ID `1466f9109e267e458c517ada6b115e99`, manifest `titles/picross-s-genesis.json`. It
imports (about five minutes) and **plays** to its Main Menu and the Picross tutorial question, at 60 fps, seen in
screenshots from scripted presses; nothing beyond that has been played. Its sdk is S's (same build), so only the title's
own parts were new:
- Two exception landing sites (a `blr` with a one-jump pad, a direct call with the older catch shape), both written in
  the unified `call` form of `exception_landings` (`find-landing.py` output pasted in; the pads get their labels).
- The translator: a predicated bindless TEX (the title screen's grid shader, one draw a frame) failed with "unresolved
  bindless tex". The handle is built by MOV and LOP.OR from the driver's c2 tables under a predicate, with IPAs and
  arithmetic in between, and the resolver gave up at any instruction it did not know. It now stops only at an
  instruction that can write a predicate (`writes_predicate`: the SETPs, PSETP, CSETP, R2P, VOTE, SHFL, LOP / LOP3 with a
  predicate result). `tools/test_translate.c` has the shader (`check_predicated_tex`): it fails on the old translator.
Not done: past the tutorial question (the retro "Clip Picross" and the emulated-console parts have not been tried).

## PICROSS S NAMCO LEGENDARY edition

The 2024 edition after S+ (sort order 110): id `010015101def8000`, update `010015101def8800` v65536 (display version 1.0.1),
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

The October 2024 game after Namco Legendary (sort order 120): id `01004c602018a000`, update `01004c602018a800` v131072
(display version 1.0.2), main build ID `4c1cf04820df7f24166defcac3409e4a`, manifest `titles/picross-shield-hero.json`. It
imports and **plays** to its Main Menu (Picross / Mega, Color, Episode, Extra, Clip, Tutorials, Options), at 60 fps, seen
in screenshots from scripted presses; the title screen shows its image carousel. Nothing past the menu has been played.
Its modules come from a newer toolchain, which was the new thing:
- **RELR relocations.** All three modules have `DT_RELR` (tags 0x23-0x25, the compact relative relocations), which the
  loader ignored: the SDK's first constructor read a module offset as an address (`signal 11 accessing 0xb19020`).
  `runtime/loader.c` applies them now (an even entry is a word to add the base to, an odd one a bitmap of the 63 after).
- The generator took code pointers stored in data (thread entries, vtables, callbacks) as function starts from `.rela`'s
  RELATIVE entries only, so a thread entry was "no function at 700115db04". `tools/picross/gen_relr.py` runs
  `tools/gen.py` with the RELR entries added to `nso.py`'s `relocs` (a module without RELR is untouched: every other
  game's code is exactly gen.py's); `generate.sh` calls it. **The proper home is `nso.py`**, which is part of the library
  ID: folded in the next time the ID changes anyway (every game rebuilds then), and until then a rebuild through
  `runtime/gamebuild.c`, which calls `gen.py` directly, would not know RELR for this game.
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

## What to test and watch for

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
