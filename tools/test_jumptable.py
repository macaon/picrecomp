"""Compact jump tables (gen.Module._jump_table_compact, gen.adr_feeds_br), on a hand-assembled text - no game files.

Usage: PYTHONPATH=tools tools/venv/bin/python tools/test_jumptable.py

The shape is LLVM's, as seen in PICROSS S (main 0x279b20, 0x56d920, 0x1174b4c): the case blocks start right after the
br, the byte table holds (case - first case) / 4, and the bound is either a cmp/b.hi well above the table load or an
`and idx, x, #15` right at it.
"""
import struct
import sys
from types import SimpleNamespace

from gen import Module, adr_feeds_br

BASE = 0x7000000000
NOP, RET = 0xD503201F, 0xD65F03C0
TABLE = [2, 0, 1, 1, 3, 0]  # 6 entries: cases 0..3 reached through offsets 0..3


def adr(rd, off):
    return 0x10000000 | (off & 3) << 29 | ((off >> 2) & 0x7FFFF) << 5 | rd


def build(guard_distance, masked):
    """Words of one function: [guard] nops idx-copy adrp add adr ldrb add br, 4 cases, a default; plus its table."""
    w = []
    if masked:
        w.append(0x92400C4C | 2 << 5)  # and x12, x2, #0xf  (x12 = index)
        idx, cnt = 12, 16
    else:
        w += [0x7100001F | 5 << 10 | 1 << 5, 0]  # cmp w1, #5 ; b.hi default (patched below)
        w += [NOP] * guard_distance
        w.append(0x2A0103E9)  # mov w9, w1  (the index the load uses is a copy of the guarded one)
        idx, cnt = 9, 6
    w.append(0xB000000A)  # adrp x10, +1 page (the table, at BASE + 0x1000)
    w.append(0x9100014A)  # add x10, x10, #0
    adr_at = len(w)
    w.append(0)  # adr x11, cases (patched)
    w.append(0x38606800 | idx << 16 | 10 << 5 | 12)  # ldrb w12, [x10, xIDX]
    w.append(0x8B000800 | 12 << 16 | 11 << 5 | 11)  # add x11, x11, x12, lsl #2
    w += [NOP] * 10  # (LLVM schedules the case block's setup between the add and the br)
    w.append(0xD61F0000 | 11 << 5)  # br x11
    br_at = len(w) - 1
    cases = len(w)
    w.append(RET)
    w += [RET] * 3
    default = len(w)
    w.append(RET)
    w[adr_at] = adr(11, (cases - adr_at) * 4)
    if not masked:
        w[1] = 0x54000008 | ((default - 1) & 0x7FFFF) << 5  # b.hi default
    table = bytes(TABLE if not masked else TABLE + [0] * 10)
    assert len(table) >= cnt
    return w, br_at, cases, table


def classic(filler):
    """cmp w8, #5 ; b.hi default ; filler ; adrp/add table ; ldrsw x8, [x9, x8, lsl #2] ; add x8, x8, x9 ; nops ; br x8.
    PICROSS S main 0x7000fa4348: the compiler put 17 vector setup instructions between the guard and the br."""
    w = [0x7100001F | 5 << 10 | 8 << 5, 0]  # cmp w8, #5 ; b.hi default (patched below)
    w += [NOP] * filler
    w += [0xB0000009, 0x91000129]  # adrp x9, +1 page (BASE + 0x1000) ; add x9, x9, #0
    w += [0xB8A07800 | 8 << 16 | 9 << 5 | 8]  # ldrsw x8, [x9, x8, lsl #2]
    w += [0x8B090108]  # add x8, x8, x9
    w += [NOP] * 6
    w.append(0xD61F0100)  # br x8
    br_at = len(w) - 1
    cases = len(w)
    w += [RET] * 6
    default = len(w)
    w.append(RET)
    w[1] = 0x54000008 | ((default - 1) & 0x7FFFF) << 5
    table = struct.pack('<6i', *[4 * (cases + k) - 0x1000 for k in range(6)])
    return w, br_at, cases, table


def module(words, table):
    text = struct.pack(f'<{len(words)}I', *words)
    image = bytearray((text.ljust(0x1000, b'\0') + table).ljust(0x2000, b'\0'))  # (Unicorn maps whole pages)
    m = Module.__new__(Module)
    m.nso = SimpleNamespace(image=image, text=(0, len(text)))
    m.base, m.lo, m.hi = BASE, BASE, BASE + len(text)
    m.words = words
    from collections import Counter
    from lifter import Lifter
    m.lifter, m.cache, m.stats, m.mu = Lifter(BASE, 'c'), {}, Counter(), None
    m._eh = []  # the whole text is one compiler function
    return m


def main():
    for masked, dist in ((False, 3), (False, 19), (True, 0)):
        words, br_at, cases, table = build(dist, masked)
        m = module(words, table)
        got = m.jump_table(BASE + br_at * 4, BASE, m.hi)
        want = sorted({BASE + 4 * (cases + v) for v in (TABLE if not masked else TABLE + [0] * 10)})
        assert got == want, (masked, dist, got and [hex(t) for t in got], [hex(t) for t in want])
        print(f'ok: {"mask" if masked else f"guard {dist + 3} insns up"}: {len(got)} cases')
    for filler in (3, 16):
        words, br_at, cases, table = classic(filler)
        m = module(words, table)
        got = m.jump_table(BASE + br_at * 4, BASE, m.hi)
        want = [BASE + 4 * (cases + k) for k in range(6)]
        assert got == want, (filler, got and [hex(t) for t in got], [hex(t) for t in want])
        print(f'ok: guard {br_at - 1} insns before the br: {len(got)} cases')
    # a b.hi that belongs to another switch, 22 instructions up, must not be taken for this table's bound (S2 main
    # 0x7000109d28: it resolved the br to one target)
    words, br_at, cases, table = build(0, True)
    pre = [0x7100001F | 0 << 10 | 1 << 5, 0] + [NOP] * 20  # cmp w1, #0 ; b.hi <end> ; nops
    pre[1] = 0x54000008 | ((len(pre) + len(words) - 1 - 1) & 0x7FFFF) << 5
    m = module(pre + words, table)
    got = m.jump_table(BASE + (len(pre) + br_at) * 4, BASE, m.hi)
    want = sorted({BASE + 4 * (len(pre) + cases + v) for v in TABLE + [0] * 10})
    assert got == want and not m.table_exact, (got and [hex(t) for t in got], [hex(t) for t in want])
    print(f'ok: another switch\'s guard is ignored: {len(got)} cases, size guessed')
    # the adr -> br distance that split the function at its cases: 13 insns apart (float), 7 (hash), 13 (decoder)
    words, br_at, cases, table = build(19, False)
    adr_at = next(i for i, w in enumerate(words) if (w & 0x9F000000) == 0x10000000)
    m = module(words, table)
    assert adr_feeds_br(m, adr_at, 11), 'the case block would be split off as a callback'
    print('ok: adr_feeds_br')


if __name__ == '__main__':
    sys.exit(main())
