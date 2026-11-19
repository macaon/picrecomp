#!/usr/bin/env python3
"""Add a PICROSS game to the library: prepare its files, generate its C code and compile it into its own library.

What the start screen runs. It prints "@step n/N text" when a step starts and "@progress done/total" while one is
under way (runtime/importer.c reads them); an error ends it with "@error text"."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import prepare

ROOT = Path(__file__).resolve().parents[2]
STEPS = 10
PREPARED = ("Looking for the game's packages", "Reading the base package", "Reading the update package",
            "Reading the game's metadata", "Applying the update and extracting the game data",
            "Reading the game's icon", "Saving the game files")  # (steps 1-7: what the preparing does)


def progress(done, total):
    print(f"@progress {done}/{total}", flush=True)


def need(tool, what):
    if not shutil.which(tool):
        raise ValueError(f"{tool} is needed to {what} (see the Build section of the README)")


def generate(game):
    python = ROOT / "tools" / "venv" / "bin" / "python"
    if not python.is_file():
        raise ValueError("The Python tools are not set up (tools/venv, see the Build section of the README)")
    sizes = {m: (game / "exefs" / m).stat().st_size for m in ("main", "subsdk0", "sdk")}
    weights, before = {}, 0.0
    for m, size in sizes.items():  # each module's share of the work, by the size of its executable
        weights[m] = (before, size / sum(sizes.values()))
        before += size / sum(sizes.values())
    process = subprocess.Popen(["bash", str(ROOT / "tools/picross/generate.sh"), str(game)], cwd=ROOT,
                               env={**os.environ, "PYTHON": str(python)}, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    tail = []
    for line in process.stdout:
        tail = (tail + [line.strip()])[-6:]
        name, _, rest = line.partition(": progress ")
        if rest and name in weights:  # gen.py: "<module>: progress <chunk>/<chunks> files <n>"
            done, _, total = rest.split()[0].partition("/")
            start, share = weights[name]
            progress(int(1000 * (start + share * int(done) / max(int(total), 1))), 1000)
    if process.wait():
        shutil.rmtree(game / "generated", ignore_errors=True)
        raise ValueError("Generating the game code failed: " + " | ".join(t for t in tail if t))


def compile_game(game):
    sources = sum(1 for _ in (game / "generated").glob("*/*.c"))
    log = game / "build.log"
    with log.open("w") as out:
        process = subprocess.Popen(["make", "game", f"GAME={game}", f"-j{os.cpu_count() or 4}"], cwd=ROOT,
                                   stdout=out, stderr=subprocess.STDOUT)
        while process.poll() is None:  # the objects written so far are the progress (not the entry point's: tools/)
            done = sum(1 for m in ("main", "subsdk0", "sdk") for _ in (game / "obj" / m).glob("*.o"))
            progress(min(done, sources - 1), sources)  # (the last object and the link are still to come at "all done")
            time.sleep(0.5)
    if process.returncode or not (game / "picgame.so").is_file():
        raise ValueError(f"Compiling the game code failed (see {log})")
    shutil.rmtree(game / "obj", ignore_errors=True)


def build_ids(game):
    """The build IDs of a prepared game's three executables (a NSO's header, 0x40): what its generated code depends on."""
    ids = []
    for module in ("main", "subsdk0", "sdk"):
        with (game / "exefs" / module).open("rb") as stream:
            stream.seek(0x40)
            ids.append(stream.read(0x20))
    return ids


def swap_in(new, old):
    """Replace the installed game <old> by the finished <new> (beside it): the old one is only deleted once the new one
    stands in its place."""
    spare = old.with_name(old.name + ".replaced")
    shutil.rmtree(spare, ignore_errors=True)
    old.rename(spare)
    try:
        new.rename(old)
    except OSError:
        spare.rename(old)
        raise
    shutil.rmtree(spare, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", type=Path, required=True, help="the folder with the game's .nsp files (the base game, and its update if any)")
    parser.add_argument("--games", type=Path, required=True, help="the library: the game goes to <games>/<name>")
    parser.add_argument("--prod-keys", type=Path)
    parser.add_argument("--hactool")
    parser.add_argument("--keep-generated", action="store_true",
                        help="keep the generated C (about 640 MB a game): a rebuild after a runtime change then skips the generating")
    args = parser.parse_args()
    prepare.STEPS = STEPS
    try:
        need("clang", "compile the game code")
        need("make", "compile the game code")
        need("bash", "generate the game code")
        title = prepare.resolve_folder(args)
        game = installed = args.output
        update = False
        if (installed / "picgame.so").is_file():
            # a game that is in the library, imported again (another version, or the same: found out below): built beside
            # it, swapped in when it is done
            update = True
            game = args.output = installed.with_name("." + installed.name + ".new")  # (a dot: the start screen skips it)
            shutil.rmtree(game, ignore_errors=True)
        if (game / "exefs" / "main").is_file() and (game / "title.json").is_file():  # added before, never finished
            for n, text in enumerate(PREPARED, 1):
                prepare.step(n, text + " (already done)")
        else:
            prepare.prepare(args)
        if (game / "picgame.so").is_file():
            raise ValueError(f"{title['name']} is already in the library")
        if update:
            def info(folder):
                return json.loads((folder / "title.json").read_text()) if (folder / "title.json").is_file() else {}
            have, now = info(installed), info(game)
            if have.get("main_build_id") == now.get("main_build_id") and have.get("update_version") == now.get("update_version"):
                shutil.rmtree(game, ignore_errors=True)
                raise ValueError(f"{title['name']} is already in the library" +
                                 (f" (version {have['display_version']})" if have.get("display_version") else ""))
        if update and build_ids(game) == build_ids(installed):  # only the data changed: the game code is still right
            prepare.step(8, "Generating the game code (not needed: the code is unchanged)")
            prepare.step(9, "Compiling the game code (not needed)")
            for name in ("picgame.so", "picgame.so.id", "build.log"):
                if (installed / name).exists():
                    shutil.copy2(installed / name, game / name)  # (copied: the installed game stays whole until the swap)
        else:
            prepare.step(8, "Generating the game code")
            if not (game / "generated").is_dir():
                generate(game)
            prepare.step(9, "Compiling the game code")
            compile_game(game)
        prepare.step(10, "Finishing")
        if not args.keep_generated:  # the library is all the game needs (runtime/gamebuild.c does the same)
            shutil.rmtree(game / "generated", ignore_errors=True)
        if update:
            swap_in(game, installed)
            game = installed
        print(f"{'Updated' if update else 'Added'} {title['name']} in {game}", flush=True)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"@error {error}", flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
