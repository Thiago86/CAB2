import sys, os, struct
track, outdir, base = sys.argv[1], sys.argv[2], 45000
f = open(track,'rb')
def sec(lba, n=1):
    out = bytearray()
    for i in range(n):
        f.seek((lba-base+i)*2352+16); out += f.read(2048)
    return bytes(out)
ip = sec(base,16)
open(os.path.join(outdir,'IP.BIN'),'wb').write(ip)
pvd = sec(base+16)
assert pvd[1:6]==b'CD001', pvd[:8]
print('VOLUME', pvd[40:72].decode().strip(), '| created', pvd[813:829].decode())
root = pvd[156:190]
def walk(lba, size, path):
    data = sec(lba, (size+2047)//2048); i=0
    while i < len(data):
        l = data[i]
        if l==0:
            i = (i//2048+1)*2048; continue
        r = data[i:i+l]; i+=l
        ext, sz = struct.unpack_from('<I',r,2)[0], struct.unpack_from('<I',r,10)[0]
        flags, nl = r[25], r[32]; name = r[33:33+nl]
        if name in (b'\x00',b'\x01'): continue
        name = name.decode('latin1').split(';')[0]
        if '/' in name or name in ('..','.') or '\\' in name: continue
        p = os.path.join(path,name)
        if flags & 2:
            os.makedirs(os.path.join(outdir,p),exist_ok=True); walk(ext,sz,p)
        else:
            print(f'{ext:7d} {sz:10d} {p}')
            with open(os.path.join(outdir,p),'wb') as o:
                left=sz; l2=ext
                while left>0:
                    n=min(left,2048); o.write(sec(l2)[:n]); left-=n; l2+=1
walk(struct.unpack_from('<I',root,2)[0], struct.unpack_from('<I',root,10)[0], '')
