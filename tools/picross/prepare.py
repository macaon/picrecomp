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
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]


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
        home = work / "home"
        key_dir = home / ".switch"
        key_dir.mkdir(parents=True)
        # Only forward NCA keys used for this title by hactool.
        entries = []
        for line in args.prod_keys.read_text(encoding="utf-8-sig").splitlines():
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
        env = {**os.environ, "HOME": str(home), "XDG_CONFIG_HOME": str(home)}

        def hactool(step, *arguments):
            print(step, flush=True)
            result = subprocess.run(
                [tool, *map(str, arguments)], cwd=work, env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            if result.returncode:
                raise ValueError(f"{step} failed (hactool exit {result.returncode})")

        base, update, prepared = (work / name for name in ("base", "update", "prepared"))
        prepared.mkdir(mode=0o700)
        hactool("Reading base NSP", "-t", "pfs0", f"--pfs0dir={base}", args.base.resolve())
        hactool("Reading update NSP", "-t", "pfs0", f"--pfs0dir={update}", args.update.resolve())
        base_metas, update_metas = read_metas(base, hactool), read_metas(update, hactool)
        found = next((m["id"] for m in base_metas if m["kind"] == META_APPLICATION), None)
        title = next((t for t in load_manifests() if t["base_id"] == found), None)
        if title is None:
            raise ValueError(f"Title {found} has no manifest in titles/")
        print(f"Title: {title['name']}", flush=True)
        base_meta = pick_meta(base_metas, META_APPLICATION, title["base_id"], 0)
        update_meta = pick_meta(update_metas, META_PATCH, title["update_id"], title["update_version"])
        base_nca = content_nca(base, base_meta, CONTENT_PROGRAM)
        update_nca = content_nca(update, update_meta, CONTENT_PROGRAM)
        if not base_nca or not update_nca:
            raise ValueError("The program NCA is missing or has the wrong size")
        # A common ticket holds the encrypted title key expected by hactool.
        tickets = []
        for folder in (base, update):
            for ticket in folder.glob("*.tik"):
                raw = ticket.read_bytes()
                if len(raw) < 0x2B0 or raw[:4] != b"\x04\x00\x01\x00" or raw[0x281] != 0:
                    continue
                rights_id = raw[0x2A0:0x2B0].hex()
                tickets.append(f"{rights_id} = {raw[0x180:0x190].hex()}\n")
        (key_dir / "title.keys").write_text("".join(tickets), encoding="ascii")
        romfs = prepared / "romfs.bin"
        exefs = prepared / "exefs"
        hactool("Applying update and extracting game data", "-t", "nca", f"--basenca={base_nca}",
                f"--exefsdir={exefs}", f"--section1={romfs}", update_nca)
        for name in ("main", "main.npdm", "subsdk0", "sdk"):
            if not (exefs / name).is_file():
                raise ValueError(f"Missing extracted ExeFS file: {name}")
        with (exefs / "main").open("rb") as stream:
            header = stream.read(0x60)
        if header[:4] != b"NSO0" or header[0x40:0x60].rstrip(b"\0").hex() != title["main_build_id"]:
            raise ValueError("Extracted executable has build ID %s, the manifest expects %s" %
                             (header[0x40:0x60].rstrip(b"\0").hex(), title["main_build_id"]))
        if not romfs.is_file() or romfs.stat().st_size == 0:
            raise ValueError("hactool did not produce an updated RomFS image")
        (prepared / "title.json").write_text(json.dumps(title, indent=2) + "\n")
        control = content_nca(base, base_meta, CONTENT_CONTROL)
        if control:  # the card image the Switch shows (a JPEG in the control data)
            icons = work / "control"
            try:
                hactool("Reading the icon", "-t", "nca", f"--romfsdir={icons}", control)
                shutil.copyfile(icons / "icon_AmericanEnglish.dat", prepared / "icon.jpg")
            except (ValueError, OSError):
                print("No icon found; the start screen will show the name only", flush=True)
        prepared.rename(output)
    print(f"Prepared game data in {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("update", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--prod-keys", type=Path, required=True)
    parser.add_argument("--hactool", help="Override the automatically located extraction tool")
    try:
        prepare(parser.parse_args())
    except (OSError, ValueError, ET.ParseError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
