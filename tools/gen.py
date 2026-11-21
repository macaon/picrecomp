"""Generate C for a whole NSO module.

Usage: gen.py <name> <nso> <base> <outdir> [--hle hle_symbols.txt] [--chunk N] [--simd asm|c] [--shift N]

--simd: FP/SIMD/CRC32 as AArch64 inline asm (default on arm64 hosts) or portable C (default elsewhere).
--shift: the module runs N below <base> (runtime/cpu.h GUEST_SHIFT, 0x6000000000 on Android); functions, labels and
         patches (tools/patches.py) keep the names and addresses of the unshifted layout.
"""
import argparse
import bisect
import os
import platform
import re
import struct
import sys
import time
from collections import Counter

import unicorn as uc
from unicorn import arm64_const as A

import lsda
from analyze import eh_frame_functions
from lifter import Insn, Lifter, Unsupported, bits, emit_asm_batch, hx, sext
from nso import Nso, R_AARCH64_RELATIVE

SCRATCH_MEM = 0x7300000000


OPTNONE_INSNS = 3000  # functions with jump tables above this many instructions are compiled unoptimized


def sext26(w):
    v = w & 0x03FFFFFF
    return v - 0x04000000 if v & 0x02000000 else v


SHIFT = 0  # --shift


def fname(a):
    return f'f_{a + SHIFT:x}'


def label(a):
    return f'L_{a + SHIFT:x}'


def sanitize(sym):
    return 'hle_' + re.sub(r'[^A-Za-z0-9_]', '_', sym)


class Module:
    def __init__(self, name, path, base, simd='asm'):
        self.name = name
        self.nso = Nso(path)
        self.base = base
        n = self.nso
        self.lo = base + n.text[0]
        self.hi = base + n.text[0] + n.text[1]
        self.words = struct.unpack(f'<{n.text[1] // 4}I', bytes(n.image[n.text[0]:n.text[0] + n.text[1]]))
        self.lifter = Lifter(base, simd)
        self.cache = {}
        self.stats = Counter()
        self.mu = None
        self._landings = None
        self.resume_rets = self.find_resume_rets() if name == 'sdk' else set()

    # ------------------------------------------------------------ exceptions

    def landings(self):
        """{a call's return address: the landing pad it can unwind into}, from the compiler's exception tables (lsda.py).
        Every module: the Unity code in main throws and catches C# exceptions, and the newer SDKs' own code catches too
        (PICROSS S CAPCOM CLASSICS edition). The guest unwinder jumps to a pad by longjmp-ing to the C frame of the call
        (rt_invoke_with_landing), which therefore has to wrap every such call; the pad is a label there."""
        if self._landings is None:
            self._landings = {}
            for begin, end, rows in lsda.all_call_sites(self.nso, self.base):
                for start, length, pad, _ in rows:
                    if not pad:
                        continue
                    for a in range(start, start + length, 4):
                        if self.in_text(a) and self.lift(a).kind in ('bl', 'blr'):
                            self._landings[a + 4] = pad
            self.pads = sorted(set(self._landings.values()))
        return self._landings

    def find_resume_rets(self):
        """The `ret` that ends the sdk's unwinder resume (libunwind's register-restore jump: a newer sdk ends it
        `mov sp, x16; ret`, an older one `ldr d31, [x0, #0x208]; ldp x0, x1, [x0]; ret`): there the guest unwind goes on
        at the landing pad (rt_resume_landing) instead of returning. Found by its code: the sdk differs between games."""
        w, found = self.words, []
        for i in range(len(w) - 2):
            if w[i] == 0x9100021F and w[i + 1] == 0xD65F03C0:
                found.append(self.lo + 4 * (i + 1))
            elif w[i] == 0xFD41041F and w[i + 1] == 0xA9400400 and w[i + 2] == 0xD65F03C0:
                found.append(self.lo + 4 * (i + 2))
        if len(found) != 1:
            print(f'warning: {len(found)} places in the sdk look like the unwinder\'s resume, expected one: C# exceptions '
                  f'will not resume', file=sys.stderr)
            return set()
        return set(found)

    def in_text(self, a):
        return self.lo <= a < self.hi and a % 4 == 0

    def word(self, a):
        return self.words[(a - self.lo) // 4]

    def lift(self, a):
        ins = self.cache.get(a)
        if ins is None:
            try:
                ins = self.lifter.lift(self.word(a), a)
            except Unsupported as e:
                self.stats['unsupported'] += 1
                ins = Insn('trap', f'rt_udf(c, {hx(a)}); /* {e} */')
            self.cache[a] = ins
        return ins

    def boundaries(self):
        n, b = self.nso, self.base
        starts = set()
        for a in eh_frame_functions(n):
            starts.add(b + a)
        for name, info, shndx, value, size in n.syms:
            if shndx and (info & 0xF) == 2:
                starts.add(b + value)
        for off, typ, sym, add in n.relocs:
            if typ == R_AARCH64_RELATIVE:
                starts.add(b + add)
        starts = sorted(a for a in starts if self.in_text(a))
        return starts

    # ------------------------------------------------------------ jump tables

    def emu(self):
        if self.mu is None:
            mu = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
            n = self.nso
            mu.mem_map(self.base, len(n.image))
            mu.mem_write(self.base, bytes(n.image))
            mu.mem_map(SCRATCH_MEM, 0x100000)
            mu.reg_write(A.UC_ARM64_REG_CPACR_EL1, 0x300000)
            self.mu = mu
        return self.mu

    def true_function(self, a):
        """[start, end) of the .eh_frame function containing a (the compiler's function, before splitting)."""
        if not hasattr(self, '_eh'):
            self._eh = sorted(self.base + x for x in eh_frame_functions(self.nso) if self.in_text(self.base + x))
        i = bisect.bisect_right(self._eh, a) - 1
        if i < 0:
            return self.lo, self.hi
        return self._eh[i], self._eh[i + 1] if i + 1 < len(self._eh) else self.hi

    def jump_table(self, br_pc, entry, end):
        """Resolve `br xN` targets from a bounds-checked jump table, or None. Targets may lie outside the piece
        entry..end when the function was split (another function branches into its middle): anywhere in the
        compiler's function (.eh_frame) holding the br is accepted; scan() makes those targets boundaries."""
        tlo, thi = self.true_function(br_pc)
        self._vlo, self._vhi = min(entry, tlo), max(end, thi)
        r = self._jump_table_fallthrough(br_pc, entry, end)
        if r is None:
            r = self._jump_table_taken(br_pc, entry, end)
        self.table_exact = r is not None  # (the entry count is the compare's: every index was emulated)
        if r is None:
            r = self._jump_table_compact(br_pc, entry)
        return r

    def _indexes_table(self, frm, br_pc, regs):
        """Is the register that indexes a table load in frm..br still the compared index (one of regs, or a plain copy of
        it)? Tells a table's own guard from another switch's compare 20 instructions up, whose register only feeds the
        index through arithmetic (a bit reader's size, S2 main 0x7000109d28)."""
        md = getattr(self, '_cs', None)
        if md is None:
            import capstone
            md = self._cs = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
            md.detail = True
        from capstone import arm64
        direct = set(regs)

        def num(ins, r):
            n = ins.reg_name(r) or ''
            return int(n[1:]) if n[:1] in 'xw' and n[1:].isdigit() else None
        for a in range(frm, br_pc, 4):
            for ins in md.disasm(struct.pack('<I', self.word(a)), a, 1):
                for op in ins.operands:
                    if op.type == arm64.ARM64_OP_MEM and op.mem.index and num(ins, op.mem.index) in direct:
                        return True
                _, written = ins.regs_access()
                dst = {num(ins, r) for r in written} - {None}
                src = [op for op in ins.operands if op.type == arm64.ARM64_OP_REG and num(ins, op.reg) in direct]
                if ins.mnemonic in ('mov', 'uxtw', 'sxtw') and len(ins.operands) == 2 and src and \
                        ins.operands[1].type == arm64.ARM64_OP_REG and num(ins, ins.operands[1].reg) in direct:
                    direct |= dst
                else:
                    direct -= dst
        return False

    def _index_regs(self, start, at, idx):
        """The index register plus registers holding the same value in start..at: sources it was
        copied/masked from, and copies of those (the table lookup sometimes recomputes the index
        from another copy after the bounds check)."""
        def copy_src(w):
            if (w & 0x7F800000) == 0x12000000:  # AND (imm)
                return bits(w, 5, 5)
            if (w & 0x7FE0FFE0) == 0x2A0003E0:  # MOV (ORR with zr)
                return bits(w, 16, 5)
            if (w & 0x7F800000) in (0x53000000, 0x13000000):  # UBFM/SBFM (uxtb, uxth, sxtw...)
                return bits(w, 5, 5)
            return None
        regs = {idx}
        changed = True
        while changed:
            changed = False
            for a in range(start, at, 4):
                w = self.word(a)
                rd, src = w & 31, copy_src(w)
                if src is None or src >= 31 or rd >= 31:
                    continue
                for x, y in ((rd, src), (src, rd)):
                    if x in regs and y not in regs:
                        regs.add(y)
                        changed = True
        return sorted(regs)

    def _set_index(self, mu, regs, i):
        for r in regs:
            mu.reg_write(A.UC_ARM64_REG_X0 + r if r < 29 else A.UC_ARM64_REG_X29, i)

    def _after_last_write(self, start, br_pc, reg):
        """Where the index of a table is set for the emulation: after the instructions at the start of start..br that
        overwrite the compared register without reading it. The block may recompute it from another register (an
        `and x8, x20, #0xff` first thing: Piczle Cross), and an index set before that is overwritten: every index then
        reaches the same single target. From the first instruction that reads it, the register is the index."""
        md = getattr(self, '_cs', None)
        if md is None:
            import capstone
            md = self._cs = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
            md.detail = True

        def has(ins, regs):
            return any((ins.reg_name(r) or '')[1:] == str(reg) and (ins.reg_name(r) or '')[:1] in 'xw' for r in regs)
        at = start
        for a in range(start, br_pc, 4):
            for ins in md.disasm(struct.pack('<I', self.word(a)), a, 1):
                read, written = ins.regs_access()
                if has(ins, read):
                    return at
                if has(ins, written):
                    at = a + 4
        return at

    def _materialized(self, mu, start):
        """The registers a jump table's emulation starts from: every register a known constant, then the address
        materialization (ADRP/ADR/ADD-imm) replayed up to `start`, so table bases computed before loops or branches are
        known. The same for every index of the table: done once, the context restored for each index. Table bases are
        made near the function's start, so the replay reaches back 256 KB at most: a binary with unwind info for few of
        its functions (Piczle Cross) has "functions" of megabytes here, and replaying all of it took minutes a table."""
        for r in range(29):
            mu.reg_write(A.UC_ARM64_REG_X0 + r, SCRATCH_MEM + 0x80000)
        mu.reg_write(A.UC_ARM64_REG_SP, SCRATCH_MEM + 0x80000)
        a2 = max(self._vlo, start - 0x40000)
        while a2 < start:
            w2 = self.word(a2)
            if (w2 & 0x1F000000) == 0x10000000 or (w2 & 0xFF800000) == 0x91000000:
                mu.emu_start(a2, 0, count=1)
            a2 += 4
        return mu.context_save()

    def _emulate_table(self, br_pc, entry, end, start, idx, count, set_at):
        """Run start..br for each index value (set at address set_at), collect targets."""
        mu = self.emu()
        rn = bits(self.word(br_pc), 5, 5)

        def step_to(frm, to):
            pc = frm
            for _ in range(64):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        targets = []
        try:
            ctx = self._materialized(mu, start)
            for i in range(count):
                mu.context_restore(ctx)
                if set_at != start and not step_to(start, set_at):
                    return None
                self._set_index(mu, [idx], i)
                if not step_to(set_at, br_pc):
                    return None
                t = mu.reg_read(A.UC_ARM64_REG_X0 + rn if rn < 29 else
                                (A.UC_ARM64_REG_X29 if rn == 29 else A.UC_ARM64_REG_X30))
                if not (self._vlo <= t < self._vhi) or t % 4:
                    return None
                targets.append(t)
        except uc.UcError:
            return None
        return sorted(set(targets))

    def _jump_table_taken(self, br_pc, entry, end):
        """Form: cmp idx, #N ; b.ls/b.lo table ... table: <address math> ; br"""
        # start of the block holding the br
        s = br_pc
        while s - 4 >= entry and br_pc - (s - 4) <= 4 * 12 and self.lift(s - 4).kind in ('op', 'asm'):
            s -= 4
        for g in range(entry, end, 4):
            w = self.word(g)
            if (w & 0xFF000010) != 0x54000000 or bits(w, 0, 4) not in (9, 3):
                continue
            t = g + sext(bits(w, 5, 19), 19) * 4
            if t != s:
                continue
            cw = self.word(g - 4) if g - 4 >= entry else 0
            if (cw & 0x7F80001F) != 0x7100001F or bits(cw, 22, 1):
                continue
            limit = bits(cw, 10, 12)
            count = limit + 1 if bits(w, 0, 4) == 9 else limit
            if count <= 0 or count > 4096:
                continue
            idx = bits(cw, 5, 5)
            return self._emulate_table(br_pc, entry, end, s, idx, count, self._after_last_write(s, br_pc, idx))
        return None

    def _jump_table_fallthrough(self, br_pc, entry, end):
        rn = bits(self.word(br_pc), 5, 5)
        # find the guarding b.hi / b.hs and its cmp, scanning backwards
        a = br_pc - 4
        guard = cmp = None
        while a >= entry and br_pc - a <= 4 * 40:  # (the compiler schedules vector setup between the guard and the br)
            w = self.word(a)
            if (w & 0xFF000010) == 0x54000000 and bits(w, 0, 4) in (8, 2):
                guard = a
                break
            if self.lift(a).kind in ('b', 'bl', 'blr', 'br', 'ret', 'trap'):
                return None
            a -= 4
        if guard is None:
            return None
        a = guard - 4
        while a >= entry and guard - a <= 4 * 8:
            w = self.word(a)
            # SUBS (imm) with Rd = zr, i.e. CMP #imm
            if (w & 0x7F80001F) == 0x7100001F:
                cmp = a
                break
            if self.lift(a).kind != 'op':
                return None
            a -= 4
        if cmp is None:
            return None
        w = self.word(cmp)
        if bits(w, 22, 1):
            return None
        limit = bits(w, 10, 12)
        count = limit + 1 if bits(self.word(guard), 0, 4) == 8 else limit
        if count <= 0 or count > 4096:
            return None
        idx = bits(w, 5, 5)
        # window start: back from cmp to the first control-flow instruction
        start = cmp
        while start - 4 >= entry and cmp - (start - 4) <= 4 * 12 and self.lift(start - 4).kind in ('op', 'asm'):
            start -= 4
        idx_regs = self._index_regs(start, cmp, idx)
        # The compare must be this table's: the table load is indexed by the compared register (a b.hi 20 instructions up
        # is often another switch's)
        if not self._indexes_table(guard + 4, br_pc, idx_regs):
            return None
        mu = self.emu()
        targets = []

        def step_to(frm, to):
            # single-step: Unicorn's `until` is unreliable mid translation-block
            pc = frm
            for _ in range(64):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        try:
            ctx = self._materialized(mu, start)
            for i in range(count):
                mu.context_restore(ctx)
                if not step_to(start, cmp):
                    return None
                self._set_index(mu, idx_regs, i)
                if not step_to(cmp, br_pc):
                    return None
                t = mu.reg_read(A.UC_ARM64_REG_X0 + rn if rn < 29 else
                                (A.UC_ARM64_REG_X29 if rn == 29 else A.UC_ARM64_REG_X30))
                if not (self._vlo <= t < self._vhi) or t % 4:
                    return None
                targets.append(t)
        except uc.UcError:
            return None
        return sorted(set(targets))

    def _jump_table_compact(self, br_pc, entry):
        """LLVM's compact table read straight from the image (no emulation, no index value needed):
        adrp xS ; add xT, xS, #t ; adr xA, cases ; ldrb/ldrh wO, [xT, xI] ; add xB, xA, xO, lsl #2 ; br xB.
        Entry count: the guard (cmp idx, #N ; b.hi/b.hs) anywhere earlier in the function - PICROSS S has them 17-19
        instructions up, beyond the other resolvers' window - or an `and idx, x, #2^k-1` (no guard at all).
        A wrong guess only loses precision: functions with tables keep the all-instruction fallback."""
        lo = max(entry, br_pc - 4 * 96)

        def back(frm, pred):
            for a in range(frm - 4, lo - 4, -4):
                if pred(self.word(a)):
                    return a
            return None
        rb = bits(self.word(br_pc), 5, 5)
        add = back(br_pc, lambda w: (w & 0xFFE0FC00) == 0x8B000800 and bits(w, 0, 5) == rb)
        if add is None:
            return None
        wa = self.word(add)
        ldr = back(add, lambda w: (w & 0xFFE0FC00) in (0x38606800, 0x78607800) and bits(w, 0, 5) == bits(wa, 16, 5))
        adr = back(add, lambda w: (w & 0x9F000000) == 0x10000000 and bits(w, 0, 5) == bits(wa, 5, 5))
        if ldr is None or adr is None:
            return None
        wl = self.word(ldr)
        esize = 1 if (wl & 0xFFE0FC00) == 0x38606800 else 2
        addi = back(ldr, lambda w: (w & 0xFFC00000) == 0x91000000 and bits(w, 0, 5) == bits(wl, 5, 5))
        if addi is None:
            return None
        wi = self.word(addi)
        adrp = back(addi, lambda w: (w & 0x9F000000) == 0x90000000 and bits(w, 0, 5) == bits(wi, 5, 5))
        if adrp is None:
            return None
        table = pcrel(self.word(adrp), adrp) + bits(wi, 10, 12)
        regs = self._index_regs(max(entry, ldr - 4 * 64), ldr, bits(wl, 16, 5))
        counts = []
        for a in range(ldr - 4, lo - 4, -4):
            w = self.word(a)
            if (w & 0xFF000010) == 0x54000000 and bits(w, 0, 4) in (8, 2) and a - 4 >= entry:
                c = self.word(a - 4)
                if (c & 0x7F80001F) == 0x7100001F and not bits(c, 22, 1) and bits(c, 5, 5) in regs:
                    counts.append(bits(c, 10, 12) + (1 if bits(w, 0, 4) == 8 else 0))
                    break
        for a in range(ldr - 4, lo - 4, -4):
            w = self.word(a)  # and idx, x, #(2^k - 1): the mask is the bound
            if (w & 0x7F800000) == 0x12000000 and bits(w, 0, 5) in regs and bits(w, 16, 6) == 0 \
                    and bits(w, 10, 6) < (63 if bits(w, 22, 1) else 31):
                counts.append(1 << (bits(w, 10, 6) + 1))
                break
        count = min(counts, default=0)
        off = table - self.base
        img = self.nso.image
        if not 0 < count <= 4096 or off < 0 or off + count * esize > len(img):
            return None
        cases = pcrel(self.word(adr), adr)
        ts = sorted({cases + 4 * int.from_bytes(img[off + i * esize:off + (i + 1) * esize], 'little')
                     for i in range(count)})
        return ts if all(self._vlo <= t < self._vhi for t in ts) else None

    # ------------------------------------------------------------ functions

    def computed_target(self, br_pc, entry):
        """True if the br register comes from address arithmetic (a table) rather than a load."""
        rn = bits(self.word(br_pc), 5, 5)
        a = br_pc - 4
        while a >= entry and br_pc - a <= 4 * 16:
            w = self.word(a)
            if self.lift(a).kind not in ('op', 'asm'):
                return False
            if (w & 31) == rn:
                if (w & 0x1F000000) == 0x0B000000:  # ADD/SUB (shifted or extended register)
                    return True
                if (w & 0x9F000000) == 0x10000000:  # ADR
                    return True
                return False
            a -= 4
        return False

    def scan(self, entry, end):
        """Collect labels and external entries referenced by a function."""
        labels, ext, tables = {entry}, set(), {}
        self.landings()
        labels.update(p for p in self.pads if entry <= p < end)  # (landing pads: entered by the guest unwinder, not a branch)
        terminated = False
        for a in range(entry, end, 4):
            ins = self.lift(a)
            if ins.kind in ('b', 'bcond'):
                t = ins.target
                if entry <= t < end:
                    labels.add(t)
                elif self.in_text(t):
                    ext.add(t)
            elif ins.kind == 'bl':
                if self.in_text(ins.target):
                    ext.add(ins.target)
            elif ins.kind == 'br':
                jt = self.jump_table(a, entry, end)
                if jt:
                    tables[a] = jt if self.table_exact else Guessed(jt)
                    labels.update(t for t in jt if entry <= t < end)
                    ext.update(t for t in jt if not entry <= t < end)
                    self.stats['jumptables'] += 1
                elif self.computed_target(a, entry):
                    # unresolved table jump: allow jumping to any instruction of the function
                    tables[a] = 'all'
                    self.stats['br_all_labels'] += 1
                else:
                    self.stats['br_dispatch'] += 1
            terminated = ins.kind in ('b', 'br', 'ret', 'trap')
        if not terminated and end < self.hi:
            ext.add(end)
        return labels, ext, tables

    def emit_function(self, entry, end, labels, tables, hle):
        # A huge function with the all-instruction fallback (thousands of labels, one big switch) sends clang's
        # optimizer into the hours (main 1.0.0: f_7000a7c130, f_7001389610 - over an hour each at -O1): unoptimized
        # those compile in seconds.
        huge = bool(tables) and (end - entry) // 4 > OPTNONE_INSNS and entry not in hle
        attr = '__attribute__((optnone, noinline)) ' if huge else ''
        if huge:
            self.stats['optnone'] += 1
        out = [f'{attr}void {fname(entry)}(Cpu *CPU_RESTRICT c) {{']
        if entry in hle:
            out.append(f'    {sanitize(hle[entry])}(c);')
            out.append('}')
            return out
        out.append(f'    FN_ENTER({hx(entry)});')
        batch = []
        refs = set()

        def flush():
            if batch:
                out.append('    ' + emit_asm_batch(batch))
                batch.clear()

        def jump(t):
            if entry <= t < end:
                return f'goto {label(t)};'
            if not self.in_text(t):
                return f'c->pc = {hx(t)}; __attribute__((musttail)) return rt_dispatch(c);'
            refs.add(t)
            return f'__attribute__((musttail)) return {fname(t)}(c);'

        # A function with a jump table gets an all-instruction fallback: a case the table missed still reaches the right
        # instruction instead of aborting. Not a big function whose tables are all exact (the compare's own count,
        # every index emulated): that switch made one 2,900-instruction function take eight minutes to compile.
        all_labels = any(jt == 'all' or isinstance(jt, Guessed) for jt in tables.values()) or \
            (bool(tables) and (end - entry) // 4 <= SAFETY_NET_MAX)
        if all_labels:
            labels = set(range(entry, end, 4))
        last = None
        for a in range(entry, end, 4):
            ins = self.lift(a)
            if a in labels:
                flush()
                out.append(f'{label(a)}:;')
            if a + SHIFT in PATCHES:  # hand-written replacement for one instruction (tools/patches.py)
                flush()
                out.append('    ' + PATCHES[a + SHIFT](self, a + SHIFT))
                last = 'op'
                continue
            k = ins.kind
            if k == 'asm':
                if len(batch) >= 24:
                    flush()
                batch.append(ins.asm)
                try:
                    emit_asm_batch(batch)
                except Unsupported:
                    batch.pop()
                    flush()
                    batch.append(ins.asm)
                last = k
                continue
            flush()
            if k == 'op' or k == 'trap':
                out.append('    ' + ins.code)
            elif k == 'b':
                out.append('    ' + jump(ins.target))
            elif k == 'bcond':
                out.append(f'    if ({ins.cond}) {{ {jump(ins.target)} }}')
            elif k in ('bl', 'blr') and (pad := self.landings().get(a + 4)) is not None and entry <= pad < end:
                # a call that can unwind into a landing pad of this function: the unwinder resumes there
                go = f'goto {label(pad)};'
                if k == 'blr':
                    out.append(f'    c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; if (rt_call_with_landing(c, {hx(pad)})) {go}')
                elif self.in_text(ins.target):
                    refs.add(ins.target)
                    out.append(f'    c->x[30] = {hx(a + 4)}; if (rt_invoke_with_landing(c, {hx(pad)}, {fname(ins.target)})) {go}')
                else:
                    out.append(f'    c->x[30] = {hx(a + 4)}; c->pc = {hx(ins.target)}; if (rt_call_with_landing(c, {hx(pad)})) {go}')
                self.stats['wrapped_calls'] += 1
            elif k == 'bl':
                t = ins.target
                if self.in_text(t):
                    refs.add(t)
                    out.append(f'    c->x[30] = {hx(a + 4)}; {fname(t)}(c);')
                else:
                    out.append(f'    c->x[30] = {hx(a + 4)}; c->pc = {hx(t)}; rt_dispatch(c);')
            elif k == 'blr':
                out.append(f'    c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; rt_dispatch(c);')
            elif k == 'br':
                jt = tables.get(a)
                if jt == 'all':
                    out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    continue
                if jt:
                    cases = ' '.join(f'case {hx(t)}: {jump(t)}' for t in jt)
                    out.append(f'    switch ({ins.target}) {{ {cases} default: break; }}')
                    if all_labels:
                        out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    else:
                        out.append(f'    c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
                    continue
                out.append(f'    c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
            elif k == 'ret':
                out.append('    rt_resume_landing(c);' if a in self.resume_rets else '    return;')
            last = k
        flush()
        if last not in ('b', 'br', 'ret', 'trap'):
            out.append('    ' + jump(end) if end < self.hi else f'    rt_udf(c, {hx(end)});')
        if all_labels:
            out.append('L_dispatch:')
            cases = ' '.join(f'case {hx(t)}: goto {label(t)};' for t in range(entry, end, 4))
            out.append(f'    switch (c->pc) {{ {cases} default: break; }}')
            out.append('    __attribute__((musttail)) return rt_dispatch(c);')
        out.append('}')
        self._refs = refs
        return out


from patches import PATCHES


SAFETY_NET_MAX = 1500  # instructions: bigger functions with exact tables go without the all-instruction fallback


class Guessed(list):
    """Targets of a jump table whose entry count was guessed (gen.Module._jump_table_compact)."""


def pcrel(w, pc):
    """Target of an ADR / ADRP word at pc."""
    imm = sext(bits(w, 5, 19) << 2 | bits(w, 29, 2), 21)
    return (pc & ~0xFFF) + (imm << 12) if w & 0x80000000 else pc + imm


def adr_feeds_br(m, i, rd, window=16):
    """ADR whose register reaches a `br` within a few instructions: the base of a compact jump table's cases
    (LLVM: adr xB, cases ; ldrb/ldrh wO, [table, idx] ; add xB, xB, xO, lsl #2 ; br xB), code inside the same
    function - not a callback. Treating it as a function start split the function at its cases, so its table
    couldn't be resolved (main: 71 tables resolved, 1832 left to the all-instruction fallback). The window must
    cover the code LLVM schedules in between: PICROSS S has 13 instructions from the ADR to the br."""
    regs = {rd}
    for k in range(i + 1, min(i + 1 + window, len(m.words))):
        w = m.words[k]
        if (w & 0xFFFFFC1F) == 0xD61F0000:  # br xN
            return ((w >> 5) & 31) in regs
        if (w & 0x7F200000) == 0x0B000000 and ((w >> 5) & 31) in regs:  # add (shifted register) from a tracked reg
            regs.add(w & 31)
        elif (w & 31) in regs and (w & 0x7F200000) != 0x0B000000:
            regs.discard(w & 31)  # (overwritten by something else)
    return False


def write_if_changed(path, text):
    """Leaves unchanged chunks untouched so make only recompiles what a patch affected."""
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path, 'w') as f:
        f.write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('nso')
    ap.add_argument('base', type=lambda s: int(s, 0))
    ap.add_argument('outdir')
    ap.add_argument('--hle')
    ap.add_argument('--chunk', type=int, default=1500)  # functions per file at most
    ap.add_argument('--chunk-bytes', type=int, default=1500000)  # and about this much C (even compile times)
    ap.add_argument('--big-fn', type=int, default=100000)  # bytes of C from which a function gets its own file
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--simd', choices=('asm', 'c'),
                    default='asm' if platform.machine().lower() in ('arm64', 'aarch64') else 'c')
    ap.add_argument('--shift', type=lambda s: int(s, 0), default=0)
    args = ap.parse_args()
    global SHIFT
    SHIFT = args.shift

    t0 = time.time()
    m = Module(args.name, args.nso, args.base - SHIFT, args.simd)
    starts = m.boundaries()
    hle = {}
    if args.hle and os.path.exists(args.hle):
        want = {l.strip() for l in open(args.hle) if l.strip() and not l.startswith('#')}
        for name, info, shndx, value, size in m.nso.syms:
            if shndx and name in want:
                hle[m.base + value] = name

    # Partition text into functions; any branch/call target outside its function becomes a new
    # boundary (splitting the containing function) until a fixpoint is reached.
    start_set = set(starts)
    for i, w in enumerate(m.words):
        if (w & 0xFC000000) == 0x94000000:
            t = m.lo + i * 4 + sext26(w) * 4
            if m.in_text(t):
                start_set.add(t)
    # Code addresses materialized in registers (callbacks, vtables built at runtime): ADR, or
    # ADRP followed shortly by ADD on the same register.
    adrp_regs = {}
    for i, w in enumerate(m.words):
        pc = m.lo + i * 4
        if (w & 0x9F000000) == 0x10000000:  # ADR
            imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
            if imm & (1 << 20):
                imm -= 1 << 21
            if m.in_text(pc + imm) and not adr_feeds_br(m, i, w & 31):
                start_set.add(pc + imm)
        elif (w & 0x9F000000) == 0x90000000:  # ADRP
            imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
            if imm & (1 << 20):
                imm -= 1 << 21
            adrp_regs[w & 31] = ((pc & ~0xFFF) + (imm << 12), i)
        elif (w & 0xFFC00000) == 0x91000000:  # ADD (imm, 64-bit, no shift)
            rn, rd = (w >> 5) & 31, w & 31
            if rn in adrp_regs and i - adrp_regs[rn][1] <= 8:
                t = adrp_regs[rn][0] + ((w >> 10) & 0xFFF)
                if m.in_text(t):
                    start_set.add(t)
    scanned = {}
    rounds = 0
    while True:
        rounds += 1
        order = sorted(start_set)
        new = set()
        for i, a in enumerate(order):
            end = order[i + 1] if i + 1 < len(order) else m.hi
            key = (a, end)
            if key not in scanned:
                scanned[key] = m.scan(a, end)
            new |= scanned[key][1] - start_set
        if not new:
            break
        start_set |= new
    entries = {}
    for i, a in enumerate(order):
        end = order[i + 1] if i + 1 < len(order) else m.hi
        labels, ext, tables = scanned[(a, end)]
        entries[a] = (end, labels, tables)
    print(f'{args.name}: {len(starts)} boundaries, {len(entries)} functions, {rounds} rounds, {dict(m.stats)}, '
          f'{time.time() - t0:.0f}s', file=sys.stderr)

    os.makedirs(args.outdir, exist_ok=True)
    order = sorted(entries)
    if args.limit:
        order = order[:args.limit]
    nchunks = 0
    ci = 0
    pending = None  # (address, lines, refs) of a big function that starts the next file
    while ci < len(order) or pending:
        body, refs, size, fns = [], set(), 0, []
        while (pending or ci < len(order)) and len(fns) < args.chunk and size < args.chunk_bytes:
            if pending:
                a, lines, r = pending
                pending = None
            else:
                a = order[ci]
                ci += 1
                end, labels, tables = entries[a]
                lines = m.emit_function(a, end, labels, tables, hle)
                r = getattr(m, '_refs', set())
                m._refs = set()
            n = sum(len(l) + 1 for l in lines)
            if n > args.big_fn and fns:  # (a big function gets a file of its own: they compile in parallel)
                pending = (a, lines, r)
                break
            body += lines
            size += n
            fns.append(a)
            refs |= r
            if n > args.big_fn:
                break
        path = os.path.join(args.outdir, f'{args.name}_{nchunks:03d}.c')
        text = ['#include "cpu.h"\n' + ('#include "simd.h"\n' if m.lifter.sc else '')]
        text += [f'void {fname(r)}(Cpu *c);\n' for r in sorted(refs)]
        text += [f'void {sanitize(hle[a])}(Cpu *c);\n' for a in sorted(set(hle) & set(fns))]
        text += ['\n'.join(body), '\n']
        write_if_changed(path, ''.join(text))
        nchunks += 1
        # (the start screen's setup reads these for its progress bar: runtime/gamebuild.c)
        print(f'{args.name}: progress {ci}/{len(order)} files {nchunks}', file=sys.stderr, flush=True)
    for fn in os.listdir(args.outdir):  # an earlier run's extra chunks
        mm = re.fullmatch(re.escape(args.name) + r'_(\d{3})\.c', fn)
        if mm and int(mm.group(1)) >= nchunks:
            os.remove(os.path.join(args.outdir, fn))
    import io
    with io.StringIO() as f:
        f.write('#include "cpu.h"\n')
        for a in order:
            f.write(f'void {fname(a)}(Cpu *c);\n')
        f.write(f'const uint64_t {args.name}_fn_addrs[] = {{\n')
        f.write(',\n'.join(hx(a) for a in order))
        f.write('};\n')
        f.write(f'const GuestFn {args.name}_fn_ptrs[] = {{\n')
        f.write(',\n'.join(fname(a) for a in order))
        f.write('};\n')
        f.write(f'const uint64_t {args.name}_fn_count = {len(order)};\n')
        write_if_changed(os.path.join(args.outdir, f'{args.name}_table.c'), f.getvalue())
    print(f'{args.name}: wrote {nchunks} chunks, {time.time() - t0:.0f}s total', file=sys.stderr)


if __name__ == '__main__':
    main()
