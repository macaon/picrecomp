import sys
from pathlib import Path
from gen import Module

work = Path(sys.argv[1]).resolve()
base = 0x7000000000
module = Module('main', work / 'exefs/main', base, 'c')
assert module.nso.build_id == '260797018fefb8459d1bf07815e71c23'
start, end = base + 0x56d95c, base + 0x56da98
targets = {start + 4 * v for v in module.nso.image[0x24e97ab:0x24e97bb]}
assert len(targets) == 16 and all(start <= t < end for t in targets)
labels, _, tables = module.scan(start, end)
code = module.emit_function(start, end, labels | targets, tables, {})
dispatch = '\n'.join(f'    case 0x{t:x}ull: goto L_{t:x};' for t in sorted(targets))
body = '\n'.join(code[2:])
path = work / 'lift/main/main_106.c'
source = (work / 'lift/main/main_106.c').read_text()
a = source.index('void f_700056d920(Cpu *CPU_RESTRICT c)')
b = source.index('void f_700056d95c(Cpu *CPU_RESTRICT c)', a)
e = source.index('void f_700056da98(Cpu *CPU_RESTRICT c)', b)
branch = source[a:b]
branch = branch[:branch.index('    c->pc = c->x[13]; goto L_dispatch;')]
replacement = (
    'static void picross_hash_tail(Cpu *c, uint64_t target) {\n'
    '    switch (target) {\n' + dispatch + '\n'
    '    default: c->pc = target; return rt_dispatch(c);\n'
    '    }\n' + body + '\n'
    + branch + '    return picross_hash_tail(c, c->x[13]);\n}\n'
    'void f_700056d95c(Cpu *c) { picross_hash_tail(c, 0x700056d95cull); }\n'
)
path.write_text(source[:a] + replacement + source[e:])
