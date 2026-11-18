"""find-landing.py GAME_FOLDER MODULE PC: the exception call sites whose landing pad is PC.

A new title's C# exceptions need their landing sites in its manifest (titles/*.json, "exception_landings"). The runtime
names the one it is missing: "Guest unwind has no continuation at <pc>". The compiler's own exception table (LSDA, found
through .eh_frame_hdr) says which call jumps there: the range printed holds the call to patch; the return address of
that bl / blr is the manifest's "return" (a `blr` is a "call" group; a direct `bl` followed by the function's ret and
the landing pad is a "catch" group: see tools/picross/fix-exception-resume.py). Checked on PICROSS S+'s two sites.

    find-landing.py .picrecomp/runtime/games/<game> main 0x700020af54"""
import sys, struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nso import Nso
game, mod, target = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
n = Nso(f'{game}/exefs/{mod}')
base = {'main': 0x7000000000, 'subsdk0': 0x7008000000, 'sdk': 0x700c000000}[mod]
img = n.image
def uleb(o):
    r = s = 0
    while True:
        b = img[o]; o += 1; r |= (b & 0x7f) << s; s += 7
        if not b & 0x80: return r, o
def sleb(o):
    r = s = 0
    while True:
        b = img[o]; o += 1; r |= (b & 0x7f) << s; s += 7
        if not b & 0x80:
            if b & 0x40: r -= 1 << s
            return r, o
def rd(enc, o, datarel=0):
    if enc == 0xff: return None, o
    f = enc & 0x0f; app = enc & 0x70; p = o
    if f == 0x00: v = struct.unpack_from('<Q', img, o)[0]; o += 8
    elif f == 0x01: v, o = uleb(o)
    elif f == 0x02: v = struct.unpack_from('<H', img, o)[0]; o += 2
    elif f == 0x03: v = struct.unpack_from('<I', img, o)[0]; o += 4
    elif f == 0x04: v = struct.unpack_from('<Q', img, o)[0]; o += 8
    elif f == 0x09: v, o = sleb(o)
    elif f == 0x0a: v = struct.unpack_from('<h', img, o)[0]; o += 2
    elif f == 0x0b: v = struct.unpack_from('<i', img, o)[0]; o += 4
    elif f == 0x0c: v = struct.unpack_from('<q', img, o)[0]; o += 8
    else: raise ValueError(hex(enc))
    if app == 0x10: v += p
    elif app == 0x30: v += datarel
    return v, o
h = n.eh_start
ver, ptr_enc, cnt_enc, tab_enc = img[h:h+4]
o = h + 4
_, o = rd(ptr_enc, o, h)
cnt, o = rd(cnt_enc, o, h)
tab = o
want = target - base
best = None
for i in range(cnt):
    a, b = struct.unpack_from('<ii', img, tab + i * 8)
    loc, fde = h + a, h + b
    if loc <= want: best = (loc, fde)
    else: break
loc, fde = best
o = fde
length = struct.unpack_from('<I', img, o)[0]; o += 4
cie_ptr = struct.unpack_from('<I', img, o)[0]; cie = o - cie_ptr; o += 4
# CIE
c = cie + 4 + 4 + 1  # length, id, version
aug = bytes(img[c:img.index(b'\0', c)]); c += len(aug) + 1
_, c = uleb(c); _, c = sleb(c)
_, c = uleb(c)  # return reg (version 1: a byte, uleb is the same for < 128)
fde_enc = 0; lsda_enc = 0xff
if aug.startswith(b'z'):
    _, c = uleb(c)
    for ch in aug[1:]:
        if ch == ord('R'): fde_enc = img[c]; c += 1
        elif ch == ord('L'): lsda_enc = img[c]; c += 1
        elif ch == ord('P'):
            pe = img[c]; c += 1; _, c = rd(pe, c, h)
pc_begin, o = rd(fde_enc, o, h); pc_range, o = rd(fde_enc & 0x0f, o)
alen, o = uleb(o)
lsda, _ = rd(lsda_enc, o, h)
print(f'function {base+pc_begin:#x}..{base+pc_begin+pc_range:#x}, lsda {lsda:#x}')
if not lsda: sys.exit()
o = lsda
lp = img[o]; o += 1
lpstart = pc_begin
if lp != 0xff: lpstart, o = rd(lp, o, h)
tt = img[o]; o += 1
if tt != 0xff: _, o = uleb(o)
cs_enc = img[o]; o += 1
cs_len, o = uleb(o); end = o + cs_len
rows = []
while o < end:
    s, o = rd(cs_enc, o); l, o = rd(cs_enc, o); lpad, o = rd(cs_enc, o); act, o = uleb(o)
    rows.append((lpstart + s, l, lpstart + lpad if lpad else 0, act))
for s, l, lpad, act in rows:
    if lpad and base + lpad == target:
        print(f'call site {base+s:#x}..{base+s+l:#x} (the last bl / blr in it, plus 4, is the manifest return) -> pad {base+lpad:#x} action {act}')
