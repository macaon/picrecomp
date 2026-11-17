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
main, sdk = root / "lift/main", root / "lift/sdk"
patch(main, '_Bool rt_call_with_landing(Cpu *c, uint64_t pc);',
      ('c->x[30] = 0x7001b40d84ull; rt_dispatch(c);',
       'c->x[30] = 0x7001b40d84ull;\n'
       '    if (rt_call_with_landing(c, 0x7001b40eb8ull)) goto L_7001b40ec8;'))
patch(sdk, '_Noreturn void rt_resume_landing(Cpu *c);',
      ('    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    return;\n}\nvoid f_700c55f200',
       '    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    rt_resume_landing(c);\n}\nvoid f_700c55f200'))
patch(main, '_Bool rt_invoke_with_landing(Cpu *c, uint64_t pc, void (*call)(Cpu *));',
      ('c->x[30] = 0x7001510f9cull; f_7001511030(c);',
       'c->x[30] = 0x7001510f9cull;\n'
       '    if (rt_invoke_with_landing(c, 0x7001510fa8ull, f_7001511030)) goto L_7001510fa8;'),
      ('    return;\n    c->x[20] = (uint64_t)(0ull | c->x[0]);\n'
       '    (void)(subs32(c, (uint32_t)c->x[1], 0x1ull));\n'
       '    if (COND_NE(c)) { goto L_7001511020; }',
       '    return;\nL_7001510fa8:;\n    c->x[20] = (uint64_t)(0ull | c->x[0]);\n'
       '    (void)(subs32(c, (uint32_t)c->x[1], 0x1ull));\n'
       '    if (COND_NE(c)) { goto L_7001511020; }'))
