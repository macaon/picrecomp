import json
import sys
from pathlib import Path


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
sites = json.loads(Path(sys.argv[2]).read_text())["exception_landings"]  # the title's manifest (titles/*.json)
call, catch = sites["call"], sites["catch"]
main, sdk = root / "lift/main", root / "lift/sdk"
A = {k: v[2:] for k, v in call.items()}
B = {k: v[2:] for k, v in catch.items()}
patch(main, '_Bool rt_call_with_landing(Cpu *c, uint64_t pc);',
      (f'c->x[30] = 0x{A["return"]}ull; rt_dispatch(c);',
       f'c->x[30] = 0x{A["return"]}ull;\n'
       f'    if (rt_call_with_landing(c, 0x{A["landing"]}ull)) goto L_{A["resume"]};'))
# (the sdk module is the same in every PICROSS game: same build ID, same addresses)
patch(sdk, '_Noreturn void rt_resume_landing(Cpu *c);',
      ('    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    return;\n}\nvoid f_700c55f200',
       '    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    rt_resume_landing(c);\n}\nvoid f_700c55f200'))
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
