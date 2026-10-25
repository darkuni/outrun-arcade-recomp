#!/usr/bin/env python3
"""Minimal Xbox 360 STFS (CON/LIVE/PIRS) extractor with SHA-1 block verification."""
import struct, sys, os, hashlib, json

BLOCK = 0x1000

def be24(b): return (b[0] << 16) | (b[1] << 8) | b[2]
def le24(b): return b[0] | (b[1] << 8) | (b[2] << 16)

class STFS:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.data = self.f.read()
        d = self.data
        self.magic = d[0:4]
        self.header_size = struct.unpack('>I', d[0x340:0x344])[0]
        self.content_type = struct.unpack('>I', d[0x344:0x348])[0]
        self.title_id = struct.unpack('>I', d[0x360:0x364])[0]
        self.media_id = struct.unpack('>I', d[0x354:0x358])[0]
        self.version = struct.unpack('>I', d[0x358:0x35C])[0]
        self.base_version = struct.unpack('>I', d[0x35C:0x360])[0]
        vd = d[0x379:0x379 + 0x24]
        self.block_sep = vd[2]
        self.ft_block_count = struct.unpack('<H', vd[3:5])[0]
        self.ft_block_num = le24(vd[5:8])
        self.top_hash = vd[8:28]
        self.alloc_blocks = struct.unpack('>I', vd[28:32])[0]
        self.unalloc_blocks = struct.unpack('>I', vd[32:36])[0]
        self.display_name = d[0x411:0x411 + 0x80].decode('utf-16-be', 'ignore').split('\x00')[0]
        self.title_name = d[0x1691:0x1691 + 0x80].decode('utf-16-be', 'ignore').split('\x00')[0]
        self.sex = (~self.block_sep) & 1  # 0 = female (1 table), 1 = male (2 tables)
        self.step = (0xAB, 0x718F) if self.sex == 0 else (0xAC, 0x723A)
        self.first_ht = (self.header_size + 0xFFF) & 0xFFFFF000
        self.bad_hashes = 0

    def backing_block(self, n):
        sx = self.sex
        r = (((n + 0xAA) // 0xAA) << sx) + n
        if n < 0xAA: return r
        if n < 0x70E4: return r + (((n + 0x70E4) // 0x70E4) << sx)
        return (1 << sx) + r + (((n + 0x70E4) // 0x70E4) << sx)

    def block_addr(self, n):
        return (self.backing_block(n) << 12) + self.first_ht

    def l0_hash_block(self, n):
        if n < 0xAA: return 0
        num = (n // 0xAA) * self.step[0]
        num += ((n // 0x70E4) + 1) << self.sex
        if n // 0x70E4 == 0: return num
        return num + (1 << self.sex)

    def hash_entry(self, n):
        addr = (self.l0_hash_block(n) << 12) + self.first_ht + (n % 0xAA) * 0x18
        # Male packages: use table 1 if table 0 entry looks empty and table 1 doesn't
        e = self.data[addr:addr + 0x18]
        if self.sex == 1:
            e2 = self.data[addr + 0x1000:addr + 0x1018]
            if e[:20] == b'\0' * 20 and e2[:20] != b'\0' * 20:
                e = e2
        return e[:20], e[20], be24(e[21:24])

    def read_block(self, n, verify=True):
        a = self.block_addr(n)
        blk = self.data[a:a + BLOCK]
        if verify:
            h, _, _ = self.hash_entry(n)
            if hashlib.sha1(blk.ljust(BLOCK, b'\0')).digest() != h:
                self.bad_hashes += 1
        return blk

    def chain(self, start, count, consecutive):
        blocks, n = [], start
        for i in range(count):
            blocks.append(n)
            if consecutive: n += 1
            else: n = self.hash_entry(n)[2]
        return blocks

    def file_table(self):
        raw = b''.join(self.read_block(b) for b in self.chain(self.ft_block_num, self.ft_block_count, False))
        entries = []
        for i in range(0, len(raw), 0x40):
            e = raw[i:i + 0x40]
            nl = e[0x28] & 0x3F
            if nl == 0: continue
            entries.append(dict(
                index=i // 0x40,
                name=e[:nl].decode('latin-1'),
                consecutive=bool(e[0x28] & 0x40),
                is_dir=bool(e[0x28] & 0x80),
                valid_blocks=le24(e[0x29:0x2C]),
                alloc_blocks=le24(e[0x2C:0x2F]),
                start=le24(e[0x2F:0x32]),
                parent=struct.unpack('>h', e[0x32:0x34])[0],
                size=struct.unpack('>I', e[0x34:0x38])[0]))
        return entries

    def extract(self, out):
        ents = self.file_table()
        byidx = {e['index']: e for e in ents}
        def path_of(e):
            parts = [e['name']]
            p = e['parent']
            while p != -1 and p in byidx:
                parts.append(byidx[p]['name']); p = byidx[p]['parent']
            return os.path.join(*reversed(parts))
        manifest = []
        for e in ents:
            p = os.path.join(out, path_of(e))
            if e['is_dir']:
                os.makedirs(p, exist_ok=True); continue
            os.makedirs(os.path.dirname(p), exist_ok=True)
            nb = (e['size'] + BLOCK - 1) // BLOCK
            data = b''.join(self.read_block(b) for b in self.chain(e['start'], nb, e['consecutive']))[:e['size']]
            with open(p, 'wb') as fo: fo.write(data)
            manifest.append(dict(path=path_of(e), size=e['size'], sha1=hashlib.sha1(data).hexdigest()))
        return manifest

if __name__ == '__main__':
    s = STFS(sys.argv[1]); out = sys.argv[2]
    info = dict(magic=s.magic.decode('latin-1'), display_name=s.display_name, title_name=s.title_name,
                title_id='%08X' % s.title_id, media_id='%08X' % s.media_id, version=s.version,
                base_version=s.base_version, content_type='%08X' % s.content_type, header_size=hex(s.header_size),
                package_sex='male' if s.sex else 'female', alloc_blocks=s.alloc_blocks)
    m = s.extract(out)
    info['files'] = m; info['bad_block_hashes'] = s.bad_hashes
    print(json.dumps(info, indent=2))
    with open(os.path.join(out, '..', 'package_info.json'), 'w') as fo: json.dump(info, fo, indent=2)
