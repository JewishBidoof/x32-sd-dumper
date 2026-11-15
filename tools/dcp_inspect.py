#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only DCP inventory and integrity experiment. Never emits firmware."""
import argparse, collections, hashlib, json, math, pathlib, struct, zlib
md5 = lambda b: hashlib.md5(b).digest()
sha = lambda b: hashlib.sha256(b).hexdigest()
def entropy(b):
    return -sum((n/len(b))*math.log2(n/len(b)) for n in collections.Counter(b).values()) if b else 0

def inspect(path):
    return inspect_bytes(pathlib.Path(path).read_bytes(), pathlib.Path(path).name)

def inspect_bytes(data, image_name="memory"):
    """Inspect bytes without writing or modifying an input image."""
    if len(data) < 128: raise ValueError('short header')
    rev = data[:88].split(b'\0')[0].decode('ascii')
    count, blocks = struct.unpack_from('<II', data, 88)
    if count > 10000 or blocks * 512 != len(data): raise ValueError('invalid count/declared length')
    ds = ((count+1)*128+511)//512*512
    directory = bytearray(data[:ds]); directory[96:112] = bytes(16); directory[112:128] = md5(b'DCP bootloader')
    whole = bytearray(data); whole[96:112] = bytes(16)
    entries=[]; cursor=ds
    for i in range(count):
        off=128*(i+1); e=data[off:off+128]
        name=e[:88].split(b'\0')[0].decode('ascii'); block,size=struct.unpack_from('<II',e,88)
        start=block*512; end=start+size; padded_end=start+((size+511)//512)*512
        if start < ds or padded_end > len(data): raise ValueError('member out of bounds')
        payload=data[start:end]; padded=data[start:padded_end]
        try: zlib.decompress(payload); zvalid=True
        except zlib.error: zvalid=False
        entries.append(dict(index=i,name=name,directory_offset=off,offset=start,size=size,padded_size=len(padded),
            contiguous=start==cursor,sha256=sha(payload),stored_prefix_md5=e[96:112].hex(),stored_full_md5=e[112:128].hex(),
            raw_prefix_matches=md5((payload[:512]+bytes(512))[:512])==e[96:112],raw_full_matches=md5(payload)==e[112:128],
            entropy=round(entropy(payload),6),padding_nonzero=sum(x!=0 for x in padded[size:]),zlib_valid=zvalid))
        cursor=padded_end
    return dict(file=image_name,sha256=sha(data),size=len(data),revision=rev,count=count,blocks=blocks,
        directory_size=ds,directory_md5_valid=md5(directory)==data[112:128],
        image_md5_valid=md5(b' - designed by Thomas Zint - '+whole)==data[96:112],
        closes_at_eof=cursor==len(data),members=entries)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('image'); a=p.parse_args()
    print(json.dumps(inspect(a.image),indent=2))
