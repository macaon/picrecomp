"""find-landing.py GAME_FOLDER MODULE PC [all]: the exception call sites whose landing pad is PC.

A new title's C# exceptions need their landing sites in its manifest (titles/*.json, "exception_landings"). The runtime
names the one it is missing: "Guest unwind has no continuation at <pc>". The compiler's own exception table (LSDA, found
through .eh_frame_hdr) says which call jumps there: the range printed holds the call to patch; the return address of
that bl / blr is the manifest's "return" (see tools/picross/fix-exception-resume.py for the groups); every call in
the range is listed as a manifest entry. "all" lists every
call site of the function. Checked on PICROSS S+'s two sites.

    find-landing.py .picrecomp/runtime/games/<game> main 0x700020af54"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nso import Nso
import lsda

game, mod, target = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
n = Nso(f'{game}/exefs/{mod}')
base = {'main': 0x7000000000, 'subsdk0': 0x7008000000, 'sdk': 0x700c000000}[mod]
begin, end, rows = lsda.call_sites(n, base, target)
print(f'function {begin:#x}..{end:#x}')
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
for start, length, pad, act in rows:
    if pad == target or (len(sys.argv) > 4 and pad):
        print(f'call site {start:#x}..{start + length:#x} -> pad {pad:#x} action {act}')
        # every call in the range can be the one that throws: a manifest entry for each (a direct call names its callee)
        for ins in md.disasm(bytes(n.image[start - base:start - base + length]), start):
            if ins.mnemonic == 'bl':
                print(f'    {{"return": "{ins.address + 4:#x}", "landing": "{pad:#x}", "callee": "{int(ins.op_str[1:], 16):#x}"}}')
            elif ins.mnemonic == 'blr':
                print(f'    {{"return": "{ins.address + 4:#x}", "landing": "{pad:#x}"}}')
