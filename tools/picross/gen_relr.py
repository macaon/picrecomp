#!/usr/bin/env python3
"""tools/gen.py with the modules' RELR relocations counted (a newer toolchain's DT_RELR: PICROSS Records of The Shield
Hero's modules). The generator takes the code pointers stored in data (vtables, callbacks, thread entries) as function
starts from the RELATIVE entries of nso.py's `relocs`, which has the .rela tables only; this adds the RELR ones, with the
word at each address (the unrelocated pointer) as the addend, as a RELATIVE entry would have. A module without RELR is
untouched, so every other game's code is exactly gen.py's.

nso.py and gen.py are the game library's ID (an edit makes every installed game rebuild), hence a wrapper and not an edit
of nso.py: the right place for this is nso.py, the next time the library ID changes for another reason - and gen.py
called without this (runtime/gamebuild.c's rebuild) does not know RELR.

    gen_relr.py <the arguments of gen.py>"""
import runpy
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import nso  # noqa: E402

DT_RELRSZ, DT_RELR, R_AARCH64_RELATIVE = 35, 36, 1027


def relr_entries(n):
    """(offset, R_AARCH64_RELATIVE, 0, addend) for each word DT_RELR lists: an even entry is an address, an odd one a
    bitmap of the 63 words after the last."""
    start, size = n.dyn.get(DT_RELR), n.dyn.get(DT_RELRSZ)
    out = []
    if not start or not size:
        return out
    where = None
    for i in range(size // 8):
        x = n.u64(start + i * 8)
        if not x & 1:
            where = x
            out.append((where, R_AARCH64_RELATIVE, 0, n.u64(where)))
            where += 8
        elif where is not None:
            for j in range(63):
                if x >> (j + 1) & 1:
                    at = where + 8 * j
                    out.append((at, R_AARCH64_RELATIVE, 0, n.u64(at)))
            where += 63 * 8
    return out


_parse_mod0 = nso.Nso._parse_mod0


def parse_mod0(self):
    _parse_mod0(self)
    self.relocs = self.relocs + relr_entries(self)


nso.Nso._parse_mod0 = parse_mod0

if __name__ == "__main__":
    sys.argv[0] = str(TOOLS / "gen.py")
    runpy.run_path(sys.argv[0], run_name="__main__")
