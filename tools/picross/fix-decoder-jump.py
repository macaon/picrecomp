import sys
from pathlib import Path
from gen import Module
w = Path(sys.argv[1]).resolve()
base = 0x7000000000
m = Module('main', w / 'exefs/main', base, 'c')
assert m.nso.build_id == '260797018fefb8459d1bf07815e71c23'
start, end = base + 0x1174bbc, base + 0x1174dac
targets = sorted({start + 4 * v for v in m.nso.image[0x252d15a:0x252d16a]})
assert targets and all(start <= t <= end for t in targets if t < end)
labels, _, tables = m.scan(start, end)
body = '\n'.join(m.emit_function(start, end, labels | set(targets), tables, {})[2:])
body = body.replace("__attribute__((musttail)) ", "")
s = (w / 'lift/main/main_437.c').read_text()
a = s.index('void f_7001174b4c(Cpu *CPU_RESTRICT c)')
b = s.index('void f_7001174bbc(Cpu *CPU_RESTRICT c)', a)
e = s.index('void f_7001174dac(Cpu *CPU_RESTRICT c)', b)
branch = s[a:b].split('L_7001174bb8:;', 1)[0]
dispatch = '\n'.join(f'    case 0x{t:x}ull: goto L_{t:x};' for t in targets if t < end)
replacement = (
    'static void picross_decoder_cases(Cpu *c, uint64_t target) {\n'
    '    switch (target) {\n' + dispatch + '\n'
    '    default: c->pc = target; return rt_dispatch(c);\n    }\n'
    + body + '\n' + branch + '    if (c->x[14] == 0x7001174dacull) return f_7001174dac(c); return picross_decoder_cases(c, c->x[14]);\n}\n'
    'void f_7001174bbc(Cpu *c) { picross_decoder_cases(c, 0x7001174bbcull); }\n'
)
(w / 'lift/main/main_437.c').write_text(s[:a] + replacement + s[e:])
