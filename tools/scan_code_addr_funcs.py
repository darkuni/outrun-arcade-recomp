#!/usr/bin/env python3
"""Find functions whose address is built in code (lis rX,hi + addi/ori rY,rX,lo)
and passed around as a pointer, but that the analyzer never registered.
usage: scan_code_addr_funcs.py <basefile.exe> <generated_dir>"""
import struct, sys, glob, re
b = open(sys.argv[1], 'rb').read(); B = 0x82000000
def w(a):
    o = a - B
    return struct.unpack('>I', b[o:o+4])[0] if 0 <= o <= len(b) - 4 else 0
e = struct.unpack('<I', b[0x3c:0x40])[0]; ns = struct.unpack('<H', b[e+6:e+8])[0]; oh = struct.unpack('<H', b[e+20:e+22])[0]
text = []
for i in range(ns):
    s = b[e+24+oh+i*40:e+24+oh+i*40+40]; vs, va = struct.unpack('<II', s[8:16])
    if struct.unpack('<I', s[36:40])[0] & 0x20: text.append((B+va, B+va+vs))
funcs, labels = set(), set()
for f in glob.glob(sys.argv[2] + '/*.cpp'):
    t = open(f).read()
    funcs |= {int(x, 16) for x in re.findall(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', t, re.M)}
    labels |= {int(x, 16) for x in re.findall(r'^loc_([0-9A-F]+):', t, re.M)}
in_text = lambda v: any(lo <= v < hi for lo, hi in text)
term = lambda x: x in (0x4E800020, 0x4E800420, 0) or ((x >> 26) == 18 and (x & 1) == 0)
found = set()
for lo, hi in text:
    for a in range(lo, hi - 4, 4):
        i = w(a)
        if (i >> 26) != 15 or ((i >> 16) & 31) != 0:   # lis rD,imm  (addis rD,0,imm)
            continue
        rd = (i >> 21) & 31; hi16 = i & 0xFFFF
        for k in range(1, 9):
            j = w(a + 4 * k); op = j >> 26
            if op in (14, 24) and ((j >> 16) & 31) == rd:   # addi / ori rY, rD, lo
                lo16 = j & 0xFFFF
                v = ((hi16 << 16) + (lo16 - 0x10000 if (op == 14 and lo16 & 0x8000) else lo16)) & 0xFFFFFFFF
                # Skip jump tables: lis/addi of a table base feeding lwzx/mtctr/bctr,
                # or a target whose first words are themselves code addresses.
                is_table = in_text(w(v)) or any(
                    (w(a + 4 * m) >> 26) == 31 and ((w(a + 4 * m) >> 1) & 0x3FF) in (23, 467)  # lwzx / mtspr(ctr)
                    for m in range(k + 1, k + 4))
                if (in_text(v) and v % 4 == 0 and v not in funcs and v not in labels and term(w(v - 4))
                        and w(v) != 0 and not is_table):
                    found.add(v)
                break
            if ((j >> 21) & 31) == rd and op not in (36, 37, 38, 44, 45, 47, 54, 55):  # rD overwritten
                break
print('# %d code-built addresses of unregistered function starts' % len(found), file=sys.stderr)
for v in sorted(found): print('0x%08X' % v)
