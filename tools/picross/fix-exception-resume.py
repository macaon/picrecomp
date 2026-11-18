import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nso import Nso


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
if sites and "call" in sites:
    call = sites["call"]
    A = {k: v[2:] for k, v in call.items()}
    patch(main, '_Bool rt_call_with_landing(Cpu *c, uint64_t pc);',
          (f'c->x[30] = 0x{A["return"]}ull; rt_dispatch(c);',
           f'c->x[30] = 0x{A["return"]}ull;\n'
           f'    if (rt_call_with_landing(c, 0x{A["landing"]}ull)) goto L_{A["resume"]};'))


def sdk_resume_function(path):
    """The function after the sdk's one `mov sp, x16; ret` (the unwinder's resume), as its generated name. The sdk is
    the same in most PICROSS games (same build ID, same address) but not all: it is found by its code."""
    n = Nso(path)
    t0, tl = n.text
    hits = [o for o in range(t0, t0 + tl - 8, 4)
            if struct.unpack_from('<II', n.image, o) == (0x9100021F, 0xD65F03C0)]
    if len(hits) != 1:
        raise ValueError(f"{len(hits)} places in the sdk look like the unwinder's resume, expected one")
    return f"f_{0x700c000000 + hits[0] + 8:x}"  # (the ret, then 4 bytes of padding)


patch(sdk, '_Noreturn void rt_resume_landing(Cpu *c);',
      ('    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    return;\n}\nvoid ' + sdk_resume_function(root / "exefs/sdk"),
       '    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    rt_resume_landing(c);\n}\nvoid ' + sdk_resume_function(root / "exefs/sdk")))
if sites and "catch" in sites:
    catch = sites["catch"]
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
