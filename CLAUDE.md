# PicRecomp: standing rules

Static recompilation of the **PICROSS S** series (Nintendo Switch) for PC. Derived from EoWRecomp, itself derived from
LASRecomp (MIT, https://github.com/peiikai/LASRecomp). It is not an emulator: the game's ARM64 code becomes C at build
time, and the runtime replaces Horizon and NVN. Architecture: `docs/ARCHITECTURE.md`. Current state and next step:
`docs/STATUS.md` (read it before starting). Code, comments and documentation are in English.

## Nintendo and publisher files
- **No file owned by Nintendo or the game's publisher goes into the repository**: no `extracted/`, `generated/`, keys,
  XCI / NSP / NCA / NSO, system fonts, textures, models, music, text or any other asset, not even "one file for a
  test". The `.gitignore` covers this; check `git status --ignored` before every commit.
  Exception: screenshots of the game running (`docs/screenshots/`, JPEG) to document progress, never extracted files.
- The user's own files (NSP, `prod.keys`) live in a folder of their own, outside the repository (the local path is in
  the agent's memory, not here). **Read-only.** Never copy them into the repo. Never ask for, look for or suggest
  sources for these files.
- Preparation: `tools/picross/prepare.py` (bundled hactool) writes `.picrecomp/prepared-game/` (ignored by git). A
  filtered copy of the keys, if one is ever needed, goes in the scratchpad, never in the repo.
- Pinned version: **PICROSS S, title 0100BA0003EEA000, update v524288**, main build ID in `docs/STATUS.md`. Patch and
  HLE addresses are valid only for it. Other titles get their own entry; never assume one title's addresses hold for
  another.

## Code
- Generated code (`generated/`) is never edited by hand. Changes go in `tools/lifter.py`, `tools/gen.py` or
  `tools/patches.py`.
- Every patch checks the original instruction word before applying (`expect()` in `tools/patches.py`).
- Follow the surrounding style (names, structure, comment density). Comments explain *why*: what the game does, what
  an address is, which log or test showed the problem.
- For every failure find the cause (Capstone, logs, Switchbrew, envytools) instead of working around it. A missing NVN
  feature or Maxwell instruction is implemented properly and gets a test in `tools/` (`test_translate`, `test_simd`,
  `test_lift`, `test_jumptable`).
- Debug environment variables use the prefix `PICRECOMP_`.

## Verification
- A fix is verified only after it has been seen working (screenshot, log or test), never on "it should work".
- A change to the renderer: say which backend was tested. Vulkan on Linux is the tested one; Windows has not been run.
- Measure instead of estimating (run the tools: `tools/nso.py`, `tools/analyze.py --census`, the logs).
- Move in steps; go to the next only when the current one is proven.

## Git
- Small, descriptive commits; one subject per commit. Commits are authored as `macaon` (the repository's own git
  config; do not change it).
- README: fan project, not affiliated with Nintendo, no game files, requires the user's own legal copy; credits to
  EoWRecomp, LASRecomp and every library used, with its license.

## End of session
- Update `docs/STATUS.md`: what works, what broke, the next step, and what the user should test and look for.
