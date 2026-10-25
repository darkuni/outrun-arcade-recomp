#!/usr/bin/env python3
"""Report generated functions that goto labels they don't define (won't compile)."""
import re, glob, sys
bad = 0
for f in sorted(glob.glob(sys.argv[1] + '/*.cpp')):
    cur = None; gotos = set(); labs = set()
    def flush():
        global bad
        d = gotos - labs
        if cur and d: bad += 1; print(f, cur, sorted(d)[:4])
    for line in open(f):
        if line.startswith('DEFINE_REX_FUNC('):
            flush(); cur = line[16:30]; gotos = set(); labs = set()
        gotos.update(re.findall(r'goto (loc_[0-9A-F]+);', line))
        m = re.match(r'^(loc_[0-9A-F]+):', line)
        if m: labs.add(m.group(1))
    flush()
print('functions with undefined labels:', bad)
