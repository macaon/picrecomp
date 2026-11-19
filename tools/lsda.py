"""Exception call-site tables (LSDA) of a guest module, found through .eh_frame_hdr.

The compiler says which calls of a function can unwind into a landing pad (a catch or a clean-up) of the same function:
the generator wraps those calls (gen.py), so the guest unwinder's jump to the pad has a C frame to return to.

    call_sites(nso, base, pc)  -> (function begin, function end, rows) for the function that contains pc
    all_call_sites(nso, base)  -> the same for every function, once each
rows: [(call-site start, length, landing pad or 0, action)], guest addresses."""
import struct

_tables = {}  # per module: the .eh_frame_hdr's sorted (function, FDE) table (a walk over every function asks for each)


def _uleb(img, o):
    r = s = 0
    while True:
        b = img[o]; o += 1; r |= (b & 0x7f) << s; s += 7
        if not b & 0x80: return r, o


def _sleb(img, o):
    r = s = 0
    while True:
        b = img[o]; o += 1; r |= (b & 0x7f) << s; s += 7
        if not b & 0x80:
            if b & 0x40: r -= 1 << s
            return r, o


def _read(img, enc, o, datarel=0):
    if enc == 0xff: return None, o
    f, app, p = enc & 0x0f, enc & 0x70, o
    if f == 0x00: v = struct.unpack_from('<Q', img, o)[0]; o += 8
    elif f == 0x01: v, o = _uleb(img, o)
    elif f == 0x02: v = struct.unpack_from('<H', img, o)[0]; o += 2
    elif f == 0x03: v = struct.unpack_from('<I', img, o)[0]; o += 4
    elif f == 0x04: v = struct.unpack_from('<Q', img, o)[0]; o += 8
    elif f == 0x09: v, o = _sleb(img, o)
    elif f == 0x0a: v = struct.unpack_from('<h', img, o)[0]; o += 2
    elif f == 0x0b: v = struct.unpack_from('<i', img, o)[0]; o += 4
    elif f == 0x0c: v = struct.unpack_from('<q', img, o)[0]; o += 8
    else: raise ValueError(hex(enc))
    if app == 0x10: v += p
    elif app == 0x30: v += datarel
    return v, o


def _table(n):
    img, h = n.image, n.eh_start
    if _tables.get(id(n), (None,))[0] is not n:
        ver, ptr_enc, cnt_enc, tab_enc = img[h:h + 4]
        o = h + 4
        _, o = _read(img, ptr_enc, o, h)
        cnt, o = _read(img, cnt_enc, o, h)
        _tables[id(n)] = (n, [(h + a, h + b) for a, b in (struct.unpack_from('<ii', img, o + i * 8) for i in range(cnt))])
    return _tables[id(n)][1]


def _function(n, base, fde):
    """(begin, end, rows) of the function one FDE describes."""
    img, h = n.image, n.eh_start
    o = fde + 4
    cie = o - struct.unpack_from('<I', img, o)[0]
    o += 4
    c = cie + 4 + 4 + 1  # length, id, version
    aug = bytes(img[c:img.index(b'\0', c)])
    c += len(aug) + 1
    _, c = _uleb(img, c); _, c = _sleb(img, c); _, c = _uleb(img, c)  # code / data alignment, return register
    fde_enc, lsda_enc = 0, 0xff
    if aug.startswith(b'z'):
        _, c = _uleb(img, c)
        for ch in aug[1:]:
            if ch == ord('R'): fde_enc = img[c]; c += 1
            elif ch == ord('L'): lsda_enc = img[c]; c += 1
            elif ch == ord('P'):
                pe = img[c]; c += 1
                _, c = _read(img, pe, c, h)
    pc_begin, o = _read(img, fde_enc, o, h)
    pc_range, o = _read(img, fde_enc & 0x0f, o)
    _, o = _uleb(img, o)
    lsda, _ = _read(img, lsda_enc, o, h)
    begin, end = base + pc_begin, base + pc_begin + pc_range
    if not lsda: return begin, end, []
    o = lsda
    lp = img[o]; o += 1
    lpstart = pc_begin
    if lp != 0xff: lpstart, o = _read(img, lp, o, h)
    tt = img[o]; o += 1
    if tt != 0xff: _, o = _uleb(img, o)
    cs_enc = img[o]; o += 1
    cs_len, o = _uleb(img, o)
    stop, rows = o + cs_len, []
    while o < stop:
        s, o = _read(img, cs_enc, o); l, o = _read(img, cs_enc, o); lpad, o = _read(img, cs_enc, o); act, o = _uleb(img, o)
        rows.append((base + lpstart + s, l, base + lpstart + lpad if lpad else 0, act))
    return begin, end, rows


def call_sites(n, base, target):
    entries = _table(n)
    want = target - base
    lo, hi = 0, len(entries)
    while lo < hi:  # the last function that starts at or before target
        mid = (lo + hi) // 2
        if entries[mid][0] <= want: lo = mid + 1
        else: hi = mid
    return _function(n, base, entries[lo - 1][1])


def all_call_sites(n, base):
    for _, fde in _table(n):
        yield _function(n, base, fde)
