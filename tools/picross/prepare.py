#!/usr/bin/env python3
"""Prepare a PICROSS game (a titles/*.json manifest) from a local base and update NSP."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

import art


ROOT = Path(__file__).resolve().parents[2]
STEPS = 7


def step(number, text):
    """One line the start screen reads from the log: which step of STEPS is running."""
    print(f"@step {number}/{STEPS} {text}", flush=True)
    time.sleep(float(os.environ.get("PICRECOMP_STEP_DELAY", "0")))  # (test: slows it down to be seen)


def pfs0_names(path):
    """The file names inside a PFS0 (NSP) package, from its header alone."""
    with path.open("rb") as stream:
        head = stream.read(16)
        if head[:4] != b"PFS0":
            return []
        count, strings = struct.unpack_from("<II", head, 4)
        table = stream.read(count * 0x18 + strings)
    names = []
    for k in range(count):
        offset = struct.unpack_from("<I", table, k * 0x18 + 0x10)[0]
        end = table.index(b"\0", count * 0x18 + offset)
        names.append(table[count * 0x18 + offset:end].decode("ascii", "replace"))
    return names


def pfs0_read(path, name):
    """The bytes of one file inside a PFS0 (NSP) package, or None."""
    with path.open("rb") as stream:
        head = stream.read(16)
        if head[:4] != b"PFS0":
            return None
        count, strings = struct.unpack_from("<II", head, 4)
        table = stream.read(count * 0x18 + strings)
        for k in range(count):
            offset, size, name_at = struct.unpack_from("<QQI", table, k * 0x18)
            end = table.index(b"\0", count * 0x18 + name_at)
            if table[count * 0x18 + name_at:end].decode("ascii", "replace") == name:
                stream.seek(16 + count * 0x18 + strings + offset)
                return stream.read(size)
    return None


def package_version(nsp):
    """The version of an update package, without decrypting anything: from the package's own .cnmt.xml, else the [vNNN] in
    its file name; None if neither says. (prepare() checks it against the real metadata once the package is opened.)"""
    for name in pfs0_names(nsp):
        if name.endswith(".cnmt.xml"):
            found = re.search(rb"<Version>(\d+)</Version>", pfs0_read(nsp, name) or b"")
            if found:
                return int(found.group(1))
    found = re.search(r"\[v(\d+)\]", nsp.name)
    return int(found.group(1)) if found else None


def find_packages(folder, metas=None):
    """(manifest, base .nsp, update .nsp) for the one game whose packages are in the folder. A package's title ID is the
    start of its ticket's file name (a package without a ticket: its own
    content metadata, else the [ID] in its file name). With several updates of a game the newest one that has a manifest is used (a
    newer one that has none is mentioned, not used); a game that was never updated has no update package."""
    manifests = load_manifests()
    base_ids = {t["base_id"]: t["slug"] for t in manifests}
    update_ids = {t["update_id"]: t["slug"] for t in manifests if "update_id" in t}  # (a game never updated has none)
    bases, updates = {}, {}
    for nsp in sorted(folder.glob("*.nsp")):
        tids = [(name[:16].lower(), None) for name in pfs0_names(nsp) if name.endswith(".tik")]
        if not tids:  # a package with no ticket (standard crypto: nothing to unlock): its own metadata says what it is
            try:
                tids = [(m["id"], m["version"]) for m in (metas(nsp) if metas else [])]
            except (ValueError, OSError):
                tids = []
            if not tids:  # (no keys / extractor to read it with: the [ID] in the file name)
                named = re.search(r"\[([0-9A-Fa-f]{16})\]", nsp.name)
                tids = [(named.group(1).lower(), None)] if named else []
        for tid, version in tids:
            if tid in base_ids:
                bases[base_ids[tid]] = nsp
            elif tid in update_ids:
                updates.setdefault(update_ids[tid], []).append((version if version is not None else package_version(nsp), nsp))
    games = []
    for slug, base in bases.items():
        versions = {t.get("update_version"): t for t in manifests if t["slug"] == slug}
        if None in versions:  # (never updated: the base package is the game)
            games.append((versions[None], base, None))
            continue
        offered = [(v, nsp) for v, nsp in updates.get(slug, []) if v in versions]
        if offered:
            v, nsp = max(offered, key=lambda o: o[0])
            newer = [n for n, _ in updates[slug] if n is None or n > v]
            if newer:
                print(f"Note: an update newer than the supported v{v} is in the folder and is not used", flush=True)
            games.append((versions[v], base, nsp))
        elif updates.get(slug):
            raise ValueError("That update isn't supported yet (supported: %s)" % ", ".join(f"v{v}" for v in sorted(versions)))
    if len(games) != 1:
        raise ValueError("No supported game in that folder (it needs the base .nsp and the update .nsp)" if not games
                         else "More than one game in that folder; keep one game's packages there")
    return games[0]


def addon_indices(folder, base_id):
    """The add-on indices installed beside the game. A DLC package's title ID is the game's with the 12 low bits an index
    and the rest one above it (PICROSS S+ 0100D7B01AE58000: Picross e2 is 0100D7B01AE59001). The puzzles themselves are
    in the game's own RomFS: the package is only the entitlement (its data NCA is a banner text), which aoc:u reports."""
    want, found = (int(base_id, 16) >> 12) + 1, set()
    for nsp in sorted(folder.glob("*.nsp")):
        for name in pfs0_names(nsp):
            if name.endswith(".tik") and int(name[:16], 16) >> 12 == want and int(name[:16], 16) & 0xfff:
                found.add(int(name[:16], 16) & 0xfff)
    return sorted(found)


def find_hactool(override):
    if override:
        tool = shutil.which(override)
        if not tool:
            raise ValueError("The specified hactool executable was not found")
        return Path(tool).resolve()
    tool = ROOT / "build" / "tools" / "hactool"
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise ValueError("The bundled extractor has not been built. Run make hactool in the PicRecomp checkout.")
    return tool


def load_manifests():
    return [json.loads(p.read_text()) for p in sorted((ROOT / "titles").glob("*.json"))]


CONTENT_PROGRAM, CONTENT_CONTROL = 1, 3
META_APPLICATION, META_PATCH = 0x80, 0x81


def read_metas(directory, hactool):
    """The content metadata of every *.cnmt.nca in a package, read from the NCA itself.

    hactool's own .cnmt.xml is missing for some packages (PICROSS S3's), so the binary record is parsed here."""
    metas = []
    for nca in sorted(directory.glob("*.cnmt.nca")):
        out = directory / (nca.name + ".dir")
        hactool("Reading metadata", "-t", "nca", f"--section0dir={out}", nca)
        for record in out.glob("*.cnmt"):
            data = record.read_bytes()
            title, version, kind, _, ext, count = struct.unpack_from("<QIBBHH", data)
            contents = []
            for k in range(count):
                row = data[0x20 + ext + k * 0x38:0x20 + ext + (k + 1) * 0x38]
                contents.append((row[0x20:0x30].hex(), int.from_bytes(row[0x30:0x36], "little"), row[0x36]))
            metas.append({"id": f"{title:016x}", "version": version, "kind": kind, "contents": contents})
    return metas


def hactool_env(work, prod_keys):
    """A HOME for hactool under `work` holding only the keys the NCAs need (never the whole prod.keys): (home, environment)."""
    home = work / "home"
    key_dir = home / ".switch"
    key_dir.mkdir(parents=True)
    entries = []
    for line in Path(prod_keys).read_text(encoding="utf-8-sig").splitlines():
        name, _, value = line.partition("=")
        name = name.strip().lower()
        if not re.fullmatch(r"header_key|titlekek_\w+|key_area_key_application_\w+", name):
            continue
        value = value.strip()
        width = 64 if name == "header_key" else 32
        if not re.fullmatch(rf"[0-9a-fA-F]{{{width}}}", value):
            raise ValueError(f"Invalid value for {name} in prod.keys")
        entries.append(f"{name} = {value}\n")
    (key_dir / "prod.keys").write_text("".join(entries), encoding="ascii")
    return home, {**os.environ, "HOME": str(home), "XDG_CONFIG_HOME": str(home)}


def package_metas(nsp, tool, prod_keys):
    """The content metadata (read_metas) of one package, without extracting it: only its small *.cnmt.nca files are read
    out of the package and decrypted (they use standard crypto: the header key is enough, no ticket)."""
    with tempfile.TemporaryDirectory(prefix=".picrecomp-meta-") as temporary:
        work = Path(temporary)
        home, env = hactool_env(work, prod_keys)
        folder = work / "meta"
        folder.mkdir()
        for name in pfs0_names(nsp):
            if name.endswith(".cnmt.nca"):
                (folder / name).write_bytes(pfs0_read(nsp, name) or b"")

        def hactool(what, *arguments):
            result = subprocess.run([tool, *map(str, arguments)], cwd=work, env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode:
                raise ValueError(f"Reading the metadata of {nsp.name} failed (hactool exit {result.returncode})")

        return read_metas(folder, hactool)


def content_nca(directory, meta, content_type):
    found = [directory / f"{cid}.nca" for cid, size, ctype in meta["contents"]
             if ctype == content_type and (directory / f"{cid}.nca").is_file()
             and (directory / f"{cid}.nca").stat().st_size == size]
    return found[0] if len(found) == 1 else None


def pick_meta(metas, kind, title_id, version):
    found = [m for m in metas if m["kind"] == kind and m["id"] == title_id and m["version"] == version]
    if len(found) != 1:
        raise ValueError(f"Expected one {title_id} v{version} entry in the package metadata")
    return found[0]


def display_version(nacp):
    """The version string the Switch shows for the game (the NACP's DisplayVersion, 16 bytes at 0x3060), or ''."""
    try:
        raw = Path(nacp).read_bytes()[0x3060:0x3070]
    except OSError:
        return ""
    return raw.split(b"\0")[0].decode("ascii", "replace").strip()


def language_flags(nacp):
    """The languages the game has text for (the NACP's SupportedLanguageFlag, 4 bytes at 0x302C: a bit per language), or 0."""
    try:
        return struct.unpack_from("<I", Path(nacp).read_bytes(), 0x302C)[0]
    except (OSError, struct.error):
        return 0


def prepare(args):
    output = args.output.resolve()
    if output.exists() or output.is_symlink():
        raise ValueError(f"Output already exists: {output}")
    for path in (args.base, args.update, args.prod_keys):
        if path is not None and not path.is_file():
            raise ValueError(f"Input file not found: {path}")
    tool = find_hactool(args.hactool)
    with tempfile.TemporaryDirectory(prefix=".picrecomp-", dir=output.parent) as temporary:
        work = Path(temporary)
        home, env = hactool_env(work, args.prod_keys)
        key_dir = home / ".switch"

        def hactool(what, *arguments):
            result = subprocess.run(
                [tool, *map(str, arguments)], cwd=work, env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            if result.returncode:
                raise ValueError(f"Extraction failed at '{what}' (hactool exit {result.returncode}); are the keys complete?")

        base, update, prepared = (work / name for name in ("base", "update", "prepared"))
        prepared.mkdir(mode=0o700)
        step(2, "Reading the base package")
        hactool("base", "-t", "pfs0", f"--pfs0dir={base}", args.base.resolve())
        step(3, "Reading the update package" if args.update else "No update package for this game")
        if args.update:
            hactool("update", "-t", "pfs0", f"--pfs0dir={update}", args.update.resolve())
        step(4, "Reading the game's metadata")
        base_metas = read_metas(base, hactool)
        update_metas = read_metas(update, hactool) if args.update else []
        found = next((m["id"] for m in base_metas if m["kind"] == META_APPLICATION), None)
        candidates = [t for t in load_manifests() if t["base_id"] == found]
        if not candidates:
            raise ValueError(f"Title {found} has no manifest in titles/")
        base_meta = pick_meta(base_metas, META_APPLICATION, found, 0)
        if args.update:  # the manifest of the update's own version
            patches = [m for m in update_metas if m["kind"] == META_PATCH and m["id"] == candidates[0].get("update_id")]
            if len(patches) != 1:
                raise ValueError("The update package isn't this game's")
            title = next((t for t in candidates if t.get("update_version") == patches[0]["version"]), None)
            if title is None:
                raise ValueError(f"Update v{patches[0]['version']} of {candidates[0]['name']} isn't supported yet")
        else:
            title = next((t for t in candidates if "update_id" not in t), None)
            if title is None:
                raise ValueError(f"{candidates[0]['name']} needs its update package")
        print(f"Title: {title['name']}" + (f" (update v{title['update_version']})" if args.update else ""), flush=True)
        base_nca = content_nca(base, base_meta, CONTENT_PROGRAM)
        if args.update:
            update_meta = pick_meta(update_metas, META_PATCH, title["update_id"], title["update_version"])
            update_nca = content_nca(update, update_meta, CONTENT_PROGRAM)
        else:  # the game as it came: the base program is the one to extract
            update_nca = None
        if not base_nca or (args.update and not update_nca):
            raise ValueError("The program NCA is missing or has the wrong size")
        # A common ticket holds the encrypted title key expected by hactool.
        tickets = []
        for folder in (base, update) if args.update else (base,):
            for ticket in folder.glob("*.tik"):
                raw = ticket.read_bytes()
                if len(raw) < 0x2B0 or raw[:4] != b"\x04\x00\x01\x00" or raw[0x281] != 0:
                    continue
                rights_id = raw[0x2A0:0x2B0].hex()
                tickets.append(f"{rights_id} = {raw[0x180:0x190].hex()}\n")
        (key_dir / "title.keys").write_text("".join(tickets), encoding="ascii")
        romfs = prepared / "romfs.bin"
        exefs = prepared / "exefs"
        step(5, "Applying the update and extracting the game data")
        if update_nca:
            hactool("extract", "-t", "nca", f"--basenca={base_nca}", f"--exefsdir={exefs}", f"--section1={romfs}", update_nca)
        else:
            hactool("extract", "-t", "nca", f"--exefsdir={exefs}", f"--section1={romfs}", base_nca)
        for name in ("main", "main.npdm", "subsdk0", "sdk"):
            if not (exefs / name).is_file():
                raise ValueError(f"Missing extracted ExeFS file: {name}")
        with (exefs / "main").open("rb") as stream:
            header = stream.read(0x60)
        if header[:4] != b"NSO0" or header[0x40:0x50].hex() != title["main_build_id"]:
            raise ValueError("Extracted executable has build ID %s, the manifest expects %s" %
                             (header[0x40:0x50].hex(), title["main_build_id"]))
        if not romfs.is_file() or romfs.stat().st_size == 0:
            raise ValueError("hactool did not produce an updated RomFS image")
        # The control data (card image, and the NACP with the version the Switch shows): the update's when it has
        # one, else the base game's
        control = (content_nca(update, update_meta, CONTENT_CONTROL) if args.update else None) \
            or content_nca(base, base_meta, CONTENT_CONTROL)
        info = dict(title)
        if getattr(args, "addons", None):
            info["addons"] = args.addons
            print(f"Add-on content: {len(args.addons)} package(s)", flush=True)
        if control:
            icons = work / "control"
            step(6, "Reading the game's icon and artwork")
            try:
                hactool("icon", "-t", "nca", f"--romfsdir={icons}", control)
                shutil.copyfile(icons / "icon_AmericanEnglish.dat", prepared / "icon.jpg")
            except (ValueError, OSError):
                print("No icon found; the start screen will show the name only", flush=True)
            version = display_version(icons / "control.nacp")
            if version:
                info["display_version"] = version
            flags = language_flags(icons / "control.nacp")
            if flags:
                info["language_flags"] = str(flags)
        try:  # the colour wash behind the game's title screen, for the start screen (needs UnityPy; optional)
            art.extract_background(prepared)
        except Exception as error:  # (a background is a nicety: it never fails an import)
            print(f"No background: {error}", flush=True)
        (prepared / "title.json").write_text(json.dumps(info, indent=2) + "\n")  # (the manifest, and the game's own version)
        step(7, "Saving the game files")
        prepared.rename(output)
    print(f"Prepared game data in {output}", flush=True)


def resolve_folder(args):
    """--folder mode: fill in args.base, update, output and prod_keys from the folder."""
    if not args.games:
        raise ValueError("--folder needs --games")
    step(1, "Looking for the game's packages")
    if not args.prod_keys:  # beside the packages, else where Switch tools keep it
        args.prod_keys = next((k for k in (args.folder / "prod.keys", Path.home() / ".switch" / "prod.keys")
                               if k.is_file()), None)
    if not args.prod_keys:
        raise ValueError("No prod.keys beside the game packages or in ~/.switch")
    tool = find_hactool(args.hactool)  # (a package with no ticket is read through them: its metadata says which game)
    title, args.base, args.update = find_packages(args.folder, lambda nsp: package_metas(nsp, tool, args.prod_keys))
    args.output = args.games / title["slug"]
    args.addons = addon_indices(args.folder, title["base_id"])
    args.games.mkdir(parents=True, exist_ok=True)
    for stale in args.games.glob(".picrecomp-*"):  # (an import that was stopped)
        shutil.rmtree(stale, ignore_errors=True)
    return title


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, nargs="?")
    parser.add_argument("update", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--folder", type=Path, help="Find the game's two packages and prod.keys in this folder instead")
    parser.add_argument("--games", type=Path, help="With --folder: the folder the game is added to (as <games>/<slug>)")
    parser.add_argument("--prod-keys", type=Path)
    parser.add_argument("--hactool", help="Override the automatically located extraction tool")
    args = parser.parse_args()
    try:
        if args.folder:
            resolve_folder(args)
        elif not (args.base and args.update and args.output and args.prod_keys):
            raise ValueError("Give BASE UPDATE OUTPUT --prod-keys, or --folder and --games")  # (a game never updated: --folder)
        else:
            step(1, "Checking the files")
        prepare(args)
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"@error {error}", flush=True)
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
