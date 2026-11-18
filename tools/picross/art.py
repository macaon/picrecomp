#!/usr/bin/env python3
"""A game's hero background, from the player's own copy of the game (nothing of it is shipped).

Every PICROSS game is a Unity game, and its title screen sits on a soft colour wash in the game's own palette: the
texture Common_BG000 in Data/sharedassets5.assets of its RomFS (1280x720; PICROSS S2's is 2048x576). It is read out of
<game>/romfs.bin and saved as <game>/background.jpg, which the start screen shows muted behind that game. This needs
UnityPy (pip install UnityPy, see the README); without it a game just has no background.

    art.py GAME_FOLDER...     (adds the background to games that were added before it existed)"""

import os
import re
import struct
import sys
import tempfile
from pathlib import Path

WANT = "Common_BG000"
WIDTH = 1280  # (the saved image is at most this wide: it is a muted backdrop)


def romfs_files(path):
    """(name, offset, size) of every file in a RomFS image (the Switch's format: header, directory and file tables)."""
    with open(path, "rb") as f:
        head = f.read(0x50)
        _, _, _, dir_off, dir_size, _, _, file_off, file_size, data_off = struct.unpack_from("<QQQQQQQQQQ", head)
        f.seek(dir_off)
        dirs = f.read(dir_size)
        f.seek(file_off)
        files = f.read(file_size)
    found = []

    def walk(at, prefix):
        _, _, child_dir, child_file, _, name_len = struct.unpack_from("<IIIIII", dirs, at)
        name = dirs[at + 24:at + 24 + name_len].decode("utf-8", "replace")
        here = prefix + (name + "/" if name else "")
        k = child_file
        while k != 0xFFFFFFFF:
            sibling, = struct.unpack_from("<I", files, k + 4)
            off, size = struct.unpack_from("<QQ", files, k + 8)
            n, = struct.unpack_from("<I", files, k + 0x1C)
            found.append((here + files[k + 0x20:k + 0x20 + n].decode("utf-8", "replace"), data_off + off, size))
            k = sibling
        d = child_dir
        while d != 0xFFFFFFFF:
            walk(d, here)
            d, = struct.unpack_from("<I", dirs, d + 4)

    walk(0, "")
    return found


def extract_background(game):
    """Writes <game>/background.jpg. True when it did; False when UnityPy or the texture isn't there."""
    game = Path(game)
    romfs = game / "romfs.bin"
    try:
        import UnityPy
    except ImportError:
        print("UnityPy is not installed: no background for this game", flush=True)
        return False
    if not romfs.is_file():
        return False
    entries = {name: (off, size) for name, off, size in romfs_files(romfs)}
    names = sorted((n for n in entries if re.fullmatch(r"Data/sharedassets\d+\.assets", n)),
                   key=lambda n: (n != "Data/sharedassets5.assets", n))  # (where it is in every game so far)
    with tempfile.TemporaryDirectory(prefix=".art-", dir=game) as work, romfs.open("rb") as image:
        for asset in names:
            for name in (asset, asset + ".resS"):  # (a texture's pixels are in the .resS beside its .assets)
                if name in entries:
                    off, size = entries[name]
                    image.seek(off)
                    (Path(work) / os.path.basename(name)).write_bytes(image.read(size))
            try:
                env = UnityPy.load(str(Path(work) / os.path.basename(asset)))
            except Exception:
                continue
            for obj in env.objects:
                if obj.type.name != "Texture2D":
                    continue
                tex = obj.read()
                if tex.m_Name != WANT:
                    continue
                picture = tex.image.convert("RGB")
                if picture.width > WIDTH:
                    picture = picture.resize((WIDTH, round(picture.height * WIDTH / picture.width)))
                picture.save(game / "background.jpg", quality=88)
                return True
    return False


if __name__ == "__main__":
    for folder in sys.argv[1:]:
        print(f"{folder}: {'background.jpg written' if extract_background(folder) else 'no background'}")
