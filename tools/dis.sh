#!/bin/bash
# dis.sh <basefile> <start_hex> <end_hex>
s=$((0x$2)); e=$((0x$3)); off=$((s-0x82000000))
dd if=$1 of=/tmp/dis.bin bs=4 skip=$((off/4)) count=$(((e-s)/4)) 2>/dev/null
powerpc-linux-gnu-objdump -D -b binary -m powerpc:common64 -EB --adjust-vma=$s /tmp/dis.bin | tail -n +8 | sed 's/^ *//'
