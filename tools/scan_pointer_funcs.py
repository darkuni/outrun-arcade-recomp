#!/usr/bin/env python3
"""Find functions reachable only through data pointers (vtables, callbacks).
usage: scan_pointer_funcs.py <basefile.exe> <generated_dir> > candidates.txt"""
import struct, sys, glob, re
b = open(sys.argv[1], 'rb').read(); B = 0x82000000
def w(a): return struct.unpack('>I', b[a-B:a-B+4])[0]
e = struct.unpack('<I', b[0x3c:0x40])[0]; ns = struct.unpack('<H', b[e+6:e+8])[0]; oh = struct.unpack('<H', b[e+20:e+22])[0]
secs = []
for i in range(ns):
    s = b[e+24+oh+i*40:e+24+oh+i*40+40]; name = s[:8].rstrip(b'\0').decode(); vs, va = struct.unpack('<II', s[8:16])
    secs.append((name, B+va, vs, struct.unpack('<I', s[36:40])[0]))
text = [(a, a+sz) for n, a, sz, f in secs if f & 0x20]
data = [(a, sz) for n, a, sz, f in secs if n in ('.rdata', '.data')]
funcs, labels = set(), set()
for f in glob.glob(sys.argv[2] + '/*.cpp'):
    t = open(f).read()
    funcs |= {int(x, 16) for x in re.findall(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', t, re.M)}
    labels |= {int(x, 16) for x in re.findall(r'^loc_([0-9A-F]+):', t, re.M)}
in_text = lambda v: any(lo <= v < hi for lo, hi in text)
cands = {}
for lo, sz in data:
    for a in range(lo, min(lo+sz, B+len(b))-3, 4):
        v = w(a)
        if v % 4 == 0 and in_text(v) and v not in funcs:
            cands.setdefault(v, 0); cands[v] += 1
out = []
for v in sorted(cands):
    prev, first = w(v-4), w(v)
    ends = prev in (0x4E800020, 0x4E800420, 0) or ((prev >> 26) == 18 and (prev & 1) == 0)
    if not ends or v in labels or first == 0: continue
    # needs a terminator within 512 instructions
    if any(w(v+4*i) in (0x4E800020, 0x4E800420) or ((w(v+4*i) >> 26) == 18 and (w(v+4*i) & 1) == 0) for i in range(512)):
        out.append(v)
print('# %d pointer targets outside known functions, %d plausible starts' % (len(cands), len(out)), file=sys.stderr)
for v in out: print('0x%08X' % v)
