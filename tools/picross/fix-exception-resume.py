import sys
from pathlib import Path

def patch(path, old, new):
    src = path.read_text()
    if new in src:
        return
    if src.count(old) != 1:
        raise ValueError(f"Unexpected generated code in {path}")
    path.write_text(src.replace(old, new))

root = Path(sys.argv[1]).resolve()
main = root / "lift/main/main_643.c"
sdk = root / "lift/sdk/sdk_159.c"
patch(main, '#include "simd.h"\n',
      '#include "simd.h"\n_Bool rt_call_with_landing(Cpu *c, uint64_t pc);\n')
patch(main, 'c->x[30] = 0x7001b40d84ull; rt_dispatch(c);',
      'c->x[30] = 0x7001b40d84ull;\n'
      '    if (rt_call_with_landing(c, 0x7001b40eb8ull)) goto L_7001b40ec8;')
patch(sdk, '#include "simd.h"\n',
      '#include "simd.h"\n_Noreturn void rt_resume_landing(Cpu *c);\n')
patch(sdk, '    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    return;\n}\nvoid f_700c55f200',
      '    c->sp = (uint64_t)(c->x[16] + 0x0ull);\n    rt_resume_landing(c);\n}\nvoid f_700c55f200')

outer = root / "lift/main/main_539.c"
patch(outer, '#include "simd.h"\n',
      '#include "simd.h"\n_Bool rt_invoke_with_landing(Cpu *c, uint64_t pc, void (*call)(Cpu *));\n')
patch(outer, 'c->x[30] = 0x7001510f9cull; f_7001511030(c);',
      'c->x[30] = 0x7001510f9cull;\n'
      '    if (rt_invoke_with_landing(c, 0x7001510fa8ull, f_7001511030)) goto L_7001510fa8;')
patch(outer, '    return;\n    c->x[20] = (uint64_t)(0ull | c->x[0]);\n'
      '    (void)(subs32(c, (uint32_t)c->x[1], 0x1ull));\n'
      '    if (COND_NE(c)) { goto L_7001511020; }',
      '    return;\nL_7001510fa8:;\n    c->x[20] = (uint64_t)(0ull | c->x[0]);\n'
      '    (void)(subs32(c, (uint32_t)c->x[1], 0x1ull));\n'
      '    if (COND_NE(c)) { goto L_7001511020; }')
