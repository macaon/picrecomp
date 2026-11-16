import sys
from pathlib import Path
from gen import Module
w = Path(sys.argv[1]).resolve()
base = 0x7000000000
m = Module('main', w / 'exefs/main', base, 'c')
assert m.nso.build_id == '260797018fefb8459d1bf07815e71c23'
start, end = base + 0x279bac, base + 0x279db8
targets = sorted({start + 4 * v for v in m.nso.image[0x24d8ef4:0x24d8efa]})
assert len(targets) == 6 and all(start <= t < end for t in targets)
labels, _, tables = m.scan(start, end)
body = '\n'.join(m.emit_function(start, end, labels | set(targets), tables, {})[2:])
body = body.replace("__attribute__((musttail)) ", "")
s = (w / 'lift/main/main_043.c').read_text()
a = s.index('void f_7000279b20(Cpu *CPU_RESTRICT c)')
b = s.index('void f_7000279bac(Cpu *CPU_RESTRICT c)', a)
e = s.index('void f_7000279db8(Cpu *CPU_RESTRICT c)', b)
branch = s[a:b].split('L_7000279ba8:;', 1)[0]
dispatch = '\n'.join(f'    case 0x{t:x}ull: goto L_{t:x};' for t in targets)
replacement = (
    'static void picross_float_cases(Cpu *c, uint64_t target) {\n'
    '    switch (target) {\n' + dispatch + '\n'
    '    default: c->pc = target; return rt_dispatch(c);\n    }\n'
    + body + '\n' + branch + '    return picross_float_cases(c, c->x[11]);\n}\n'
    'void f_7000279bac(Cpu *c) { picross_float_cases(c, 0x7000279bacull); }\n'
)
(w / 'lift/main/main_043.c').write_text(s[:a] + replacement + s[e:])
