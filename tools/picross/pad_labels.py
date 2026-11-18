"""Labels at exception landing pads in already generated code.

The lifter labels only branch targets, and a landing pad is entered by the guest unwinder, not by a branch: a patch that
resumes there (`goto L_<pad>`) needs the label. The lifter is part of the game library's ID (an edit makes every
installed game rebuild), so the label is added afterwards: the function that holds the pad is lifted again with the
lifter's own code and its pads (all of them, from the compiler's exception table) as extra labels, and the generated
text replaces the function's old text, which must be exactly what the lifter gives without the labels (the check that
it is the same code).

    label_pads(<generated/main>, <exefs/main>, 0x7000000000, "main", [pad, ...])"""
import bisect
import re
from pathlib import Path

import gen
import lsda
from nso import Nso


def label_pads(chunk_dir, nso_path, base, name, pads):
    if not pads:
        return
    chunk_dir = Path(chunk_dir)
    table = (chunk_dir / f"{name}_table.c").read_text()
    entries = sorted(int(x, 16) for x in re.findall(r"0x([0-9a-f]{9,})", table[table.index(f"{name}_fn_addrs[]"):table.index(f"{name}_fn_ptrs")]))
    gen.SHIFT = 0
    module = gen.Module(name, str(nso_path), base, "c")
    nso = Nso(str(nso_path))
    hle = {}
    done = set()
    for pad in pads:
        at = bisect.bisect_right(entries, pad) - 1
        entry, end = entries[at], entries[at + 1] if at + 1 < len(entries) else module.hi
        if entry in done:
            continue
        done.add(entry)
        _, _, rows = lsda.call_sites(nso, base, pad)
        here = {p for _, _, p, _ in rows if p and entry <= p < end}
        header = f"void f_{entry:x}(Cpu *CPU_RESTRICT c) {{"
        owner = next((p for p in sorted(chunk_dir.glob(f"{name}_[0-9]*.c")) if f"\n{header}\n" in p.read_text()), None)
        if owner is None:
            raise ValueError(f"No generated function f_{entry:x} for the landing pad {pad:#x}")
        text = owner.read_text()
        if all(f"\nL_{p:x}:;\n" in text for p in here):
            continue  # (labelled in an earlier run)
        labels, _, tables = module.scan(entry, end)
        plain = "\n".join(module.emit_function(entry, end, labels, tables, hle))
        if plain not in text:
            raise ValueError(f"f_{entry:x} in {owner.name} is not what the lifter gives (patched already, or another lifter): "
                             f"cannot add the labels for the landing pads")
        module._refs = set()
        labelled = "\n".join(module.emit_function(entry, end, labels | here, tables, hle))
        owner.write_text(text.replace(plain, labelled, 1))
