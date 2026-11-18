import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nso import Nso
from pad_labels import label_pads


def patch(module, decl, *edits):
    """Apply (old, new) edits, and declare what they call, in the one generated chunk of `module` (a directory) that
    holds the first `old`. Chunk numbers move whenever the function partition does, so the chunk is found by content."""
    hits = []
    for path in sorted(module.glob("*.c")):
        src = path.read_text()
        if edits[0][1] in src:
            return  # already applied
        if edits[0][0] in src:
            hits.append((path, src))
    if len(hits) != 1:
        raise ValueError(f"Unexpected generated code in {module} for: {edits[0][0][:60]!r}")
    path, src = hits[0]
    for old, new in edits:
        if src.count(old) != 1:
            raise ValueError(f"Unexpected generated code in {path} for: {old[:60]!r}")
        src = src.replace(old, new)
    path.write_text(src.replace('#include "simd.h"\n', '#include "simd.h"\n' + decl + '\n', 1))


root = Path(sys.argv[1]).resolve()
sites = json.loads(Path(sys.argv[2]).read_text()).get("exception_landings")  # the title's manifest (titles/*.json)
main, sdk = root / "lift/main", root / "lift/sdk"
def groups(sites, kind):
    """The manifest's sites of a kind: one group, or a list of them (a title can need several)."""
    found = (sites or {}).get(kind, [])
    return found if isinstance(found, list) else [found]


# A "call" group ({"return", "landing"}, a blr; plus "callee" for a direct bl) resumes at the landing pad itself (the
# unwinder has set x0 and x1): the pad gets its label first (pad_labels.py). With "resume" (S, S+, Kemono: the pad is one
# jump to a shared handler) it goes there. "catch" groups are the older form of a direct call, whose pad code is
# labelled by editing the text.
label_pads(main, root / "exefs/main", 0x7000000000, "main",
           [int(c["landing"], 16) for c in groups(sites, "call") if "resume" not in c])
for call in groups(sites, "call"):
    A = {k: v[2:] for k, v in call.items()}
    after = f'goto L_{A["resume"]};' if "resume" in A else f'goto L_{A["landing"]};'
    if "callee" in A:  # a direct call (bl): the call itself, with the landing pad registered
        patch(main, '_Bool rt_invoke_with_landing(Cpu *c, uint64_t pc, void (*call)(Cpu *));',
              (f'c->x[30] = 0x{A["return"]}ull; f_{A["callee"]}(c);',
               f'c->x[30] = 0x{A["return"]}ull;\n'
               f'    if (rt_invoke_with_landing(c, 0x{A["landing"]}ull, f_{A["callee"]})) {{ {after} }}'))
        continue
    patch(main, '_Bool rt_call_with_landing(Cpu *c, uint64_t pc);',
          (f'c->x[30] = 0x{A["return"]}ull; rt_dispatch(c);',
           f'c->x[30] = 0x{A["return"]}ull;\n'
           f'    if (rt_call_with_landing(c, 0x{A["landing"]}ull)) {{ {after} }}'))


def sdk_resume_function(path):
    """(the unwinder's resume, as its generated name; the C before its `return;` or None). The resume is libunwind's
    register-restore jump: a newer sdk ends it with `mov sp, x16; ret`, found by those two words; an older one (Kemono
    Friends Picross) sets sp earlier and ends `ldr d31, [x0, #0x208]; ldp x0, x1, [x0]; ret`. The sdk is the same in most
    PICROSS games (same build ID, same address) but not all: it is found by its code. The function after the ret is named,
    the generated code being cut there."""
    n = Nso(path)
    t0, tl = n.text
    words = lambda o, k: struct.unpack_from('<' + 'I' * k, n.image, o)
    hits = [o for o in range(t0, t0 + tl - 8, 4) if words(o, 2) == (0x9100021F, 0xD65F03C0)]
    if len(hits) == 1:
        return f"f_{0x700c000000 + hits[0] + 8:x}", '    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n'  # (the ret, then 4 bytes of padding)
    hits = [o for o in range(t0, t0 + tl - 12, 4) if words(o, 3) == (0xFD41041F, 0xA9400400, 0xD65F03C0)]
    if len(hits) != 1:
        raise ValueError(f"{len(hits)} places in the sdk look like the unwinder's resume, expected one")
    return f"f_{0x700c000000 + hits[0] + 12:x}", ''


resume, before = sdk_resume_function(root / "exefs/sdk")
patch(sdk, '_Noreturn void rt_resume_landing(Cpu *c);',
      (before + '    return;\n}\nvoid ' + resume, before + '    rt_resume_landing(c);\n}\nvoid ' + resume))
for catch in groups(sites, "catch"):
    B = {k: v[2:] for k, v in catch.items()}
    patch(main, '_Bool rt_invoke_with_landing(Cpu *c, uint64_t pc, void (*call)(Cpu *));',
          (f'c->x[30] = 0x{B["return"]}ull; f_{B["callee"]}(c);',
           f'c->x[30] = 0x{B["return"]}ull;\n'
           f'    if (rt_invoke_with_landing(c, 0x{B["landing"]}ull, f_{B["callee"]})) goto L_{B["landing"]};'),
          ('    return;\n    c->x[20] = (uint64_t)(0ull | c->x[0]);\n'
           '    (void)(subs32(c, (uint32_t)c->x[1], 0x1ull));\n'
           f'    if (COND_NE(c)) {{ goto L_{B["target"]}; }}',
           f'    return;\nL_{B["landing"]}:;\n    c->x[20] = (uint64_t)(0ull | c->x[0]);\n'
           '    (void)(subs32(c, (uint32_t)c->x[1], 0x1ull));\n'
           f'    if (COND_NE(c)) {{ goto L_{B["target"]}; }}'))
