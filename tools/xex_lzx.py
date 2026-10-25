import struct,sys,hashlib,subprocess,math
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
d=open(sys.argv[1],'rb').read()
pe_off=struct.unpack('>I',d[8:12])[0]; so=struct.unpack('>I',d[16:20])[0]; nh=struct.unpack('>I',d[20:24])[0]
hdr={struct.unpack('>I',d[24+i*8:28+i*8])[0]:struct.unpack('>I',d[28+i*8:32+i*8])[0] for i in range(nh)}
ffi=hdr[0x3ff]; ffi_size,enc,comp=struct.unpack('>IHH',d[ffi:ffi+8])
window=struct.unpack('>I',d[ffi+8:ffi+12])[0]
blk_size=struct.unpack('>I',d[ffi+12:ffi+16])[0]; blk_hash=d[ffi+16:ffi+36]
image_size=struct.unpack('>I',d[so+4:so+8])[0]
for kn,k in (('retail',bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')),('devkit',bytes(16))):
    dec=lambda key,data,mode: (lambda c:c.update(data)+c.finalize())(Cipher(algorithms.AES(key),mode).decryptor())
    sess=dec(k,d[so+0x150:so+0x160],modes.ECB())
    pay=d[pe_off:]; raw=dec(sess,pay[:len(pay)//16*16],modes.CBC(bytes(16)))
    if hashlib.sha1(raw[:blk_size]).digest()==blk_hash: print('key',kn); break
else: sys.exit('no key matched block hash')
out=bytearray(); pos=0; size=blk_size; nblocks=0
while size:
    blk=raw[pos:pos+size]; nblocks+=1
    nxt=struct.unpack('>I',blk[0:4])[0]; nhash=blk[4:24]
    p=24
    while True:
        cs=struct.unpack('>H',blk[p:p+2])[0]; p+=2
        if cs==0: break
        out+=blk[p:p+cs]; p+=cs
    pos+=size; size=nxt
    if size and hashlib.sha1(raw[pos:pos+size]).digest()!=nhash: sys.exit('hash mismatch block %d'%nblocks)
open('stream.lzx','wb').write(out)
wb=int(math.log2(window)); print('blocks',nblocks,'lzx bytes',len(out),'window bits',wb,'image size',hex(image_size))
subprocess.run(['./lzxtool','stream.lzx',sys.argv[2],str(wb),str(image_size)],check=False)
