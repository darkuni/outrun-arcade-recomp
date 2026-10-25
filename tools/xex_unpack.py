#!/usr/bin/env python3
"""Decrypt + decompress an XEX2 (basic compression) to its PE basefile."""
import struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
KEYS = {'retail': bytes.fromhex('20B185A59D28FDC340583FBB0896BF91'), 'devkit': bytes(16)}
def aes(key, data, mode):
    c = Cipher(algorithms.AES(key), mode).decryptor(); return c.update(data) + c.finalize()
d = open(sys.argv[1], 'rb').read()
pe_off, sec_off, nh = struct.unpack('>I', d[8:12])[0], struct.unpack('>I', d[16:20])[0], struct.unpack('>I', d[20:24])[0]
hdr = {struct.unpack('>I', d[24+i*8:28+i*8])[0]: struct.unpack('>I', d[28+i*8:32+i*8])[0] for i in range(nh)}
ffi = hdr[0x3ff]; ffi_size, enc, comp = struct.unpack('>IHH', d[ffi:ffi+8])
image_size = struct.unpack('>I', d[sec_off+4:sec_off+8])[0]
enc_key = d[sec_off+0x150:sec_off+0x160]
payload = d[pe_off:]
for name, k in KEYS.items():
    sess = aes(k, enc_key, modes.ECB()) if enc else None
    raw = aes(sess, payload[:len(payload)//16*16], modes.CBC(bytes(16))) if enc else payload
    if comp == 1:
        out = bytearray(); pos = 0
        for i in range((ffi_size - 8) // 8):
            ds, zs = struct.unpack('>II', d[ffi+8+i*8:ffi+16+i*8])
            out += raw[pos:pos+ds]; out += bytes(zs); pos += ds
    elif comp == 0:
        out = bytearray(raw[:image_size])
    else:
        sys.exit('compression type %d not handled' % comp)
    if out[:2] == b'MZ':
        print('key:', name, 'image bytes:', hex(len(out)), 'expected:', hex(image_size))
        open(sys.argv[2], 'wb').write(out); break
else:
    sys.exit('no key produced an MZ header')
