"""Exception call-site tables (LSDA) of a guest module, found through .eh_frame_hdr.

call_sites(nso, base, pc) -> (function begin, function end, [(call-site start, length, landing pad or 0, action)]) for
the function that contains pc (guest addresses; rows with no landing pad have 0). Used by find-landing.py and
fix-exception-resume.py."""
import struct


def call_sites(n, base, target):
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
        f, app, p = enc & 0x0f, enc & 0x70, o
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
    ver, ptr_enc, cnt_enc, tab_enc = img[h:h + 4]
    o = h + 4
    _, o = rd(ptr_enc, o, h)
    cnt, o = rd(cnt_enc, o, h)
    tab, want, best = o, target - base, None
    for i in range(cnt):
        a, b = struct.unpack_from('<ii', img, tab + i * 8)
        if h + a <= want: best = (h + a, h + b)
        else: break
    loc, fde = best
    o = fde + 4
    cie = o - struct.unpack_from('<I', img, o)[0]
    o += 4
    c = cie + 4 + 4 + 1  # length, id, version
    aug = bytes(img[c:img.index(b'\0', c)])
    c += len(aug) + 1
    _, c = uleb(c); _, c = sleb(c); _, c = uleb(c)  # code / data alignment, return register
    fde_enc, lsda_enc = 0, 0xff
    if aug.startswith(b'z'):
        _, c = uleb(c)
        for ch in aug[1:]:
            if ch == ord('R'): fde_enc = img[c]; c += 1
            elif ch == ord('L'): lsda_enc = img[c]; c += 1
            elif ch == ord('P'):
                pe = img[c]; c += 1
                _, c = rd(pe, c, h)
    pc_begin, o = rd(fde_enc, o, h)
    pc_range, o = rd(fde_enc & 0x0f, o)
    _, o = uleb(o)
    lsda, _ = rd(lsda_enc, o, h)
    begin, end = base + pc_begin, base + pc_begin + pc_range
    if not lsda: return begin, end, []
    o = lsda
    lp = img[o]; o += 1
    lpstart = pc_begin
    if lp != 0xff: lpstart, o = rd(lp, o, h)
    tt = img[o]; o += 1
    if tt != 0xff: _, o = uleb(o)
    cs_enc = img[o]; o += 1
    cs_len, o = uleb(o)
    stop, rows = o + cs_len, []
    while o < stop:
        s, o = rd(cs_enc, o); l, o = rd(cs_enc, o); lpad, o = rd(cs_enc, o); act, o = uleb(o)
        rows.append((base + lpstart + s, l, base + lpstart + lpad if lpad else 0, act))
    return begin, end, rows
