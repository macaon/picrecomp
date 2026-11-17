#!/usr/bin/env python3
"""Prepare PICROSS S v524288 from a local base and update NSP."""

import argparse
import os
from pathlib import Path
import re
import shutil
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


def program_nca(directory, title_id, kind, version):
    programs = []
    for metadata in directory.glob("*.cnmt.xml"):
        root = ET.parse(metadata).getroot()
        if root.findtext("Type") != kind or root.findtext("Id", "").lower() != title_id:
            continue
        if int(root.findtext("Version", "-1")) != version:
            continue
        for entry in root.findall("Content"):
            content_id = entry.findtext("Id", "")
            if entry.findtext("Type") == "Program" and re.fullmatch(r"[a-fA-F0-9]{32}", content_id):
                candidate = directory / f"{content_id.lower()}.nca"
                if candidate.is_file() and candidate.stat().st_size == int(entry.findtext("Size", "-1")):
                    programs.append(candidate)
    if len(programs) != 1:
        raise ValueError(f"Expected one {kind} v{version} PICROSS S program in NSP metadata")
    return programs[0]


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
        base_nca = program_nca(base, "0x0100ba0003eea000", "Application", 0)
        update_nca = program_nca(update, "0x0100ba0003eea800", "Patch", 524288)
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
        if header[:4] != b"NSO0" or header[0x40:0x60].rstrip(b"\0").hex() != "260797018fefb8459d1bf07815e71c23":
            raise ValueError("Extracted executable has an unsupported build ID")
        if not romfs.is_file() or romfs.stat().st_size == 0:
            raise ValueError("hactool did not produce an updated RomFS image")
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
