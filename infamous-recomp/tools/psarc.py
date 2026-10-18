"""Minimal PSARC (v1.x, zlib) reader. psarc.py <archive> [list | extract <substring> <outdir>]"""
import struct, zlib, sys, os
class Psarc:
    def __init__(s, path):
        s.f = open(path, 'rb'); h = s.f.read(32)
        magic, ver, comp, s.toc_len, s.ent_sz, s.n, s.blk, flags = struct.unpack('>4sI4sIIIII', h)
        assert magic == b'PSAR' and comp == b'zlib', (magic, comp)
        toc = s.f.read(s.toc_len - 32)
        s.ents = []
        for i in range(s.n):
            e = toc[i*s.ent_sz:(i+1)*s.ent_sz]
            s.ents.append((struct.unpack('>I', e[16:20])[0], int.from_bytes(e[20:25], 'big'), int.from_bytes(e[25:30], 'big')))
        bs = 2 if s.blk <= 0x10000 else 3 if s.blk <= 0x1000000 else 4
        z = toc[s.n*s.ent_sz:]; s.zs = [int.from_bytes(z[i:i+bs], 'big') for i in range(0, len(z) - len(z) % bs, bs)]
        s.names = [''] + s.read(0).decode('utf-8', 'replace').split('\n')
    def read(s, i, limit=None):
        zi, size, off = s.ents[i]; out = bytearray(); s.f.seek(off)
        while len(out) < size and (limit is None or len(out) < limit):
            zs = s.zs[zi] or s.blk; b = s.f.read(zs); zi += 1
            if b[:2] in (b'\x78\xda', b'\x78\x9c', b'\x78\x01', b'\x78\x5e') and zs != min(s.blk, size - len(out)):
                try: b = zlib.decompress(b)
                except zlib.error: pass
            elif b[:2] in (b'\x78\xda', b'\x78\x9c'):
                try: b = zlib.decompress(b)
                except zlib.error: pass
            out += b
        return bytes(out[:size])
if __name__ == '__main__':
    p = Psarc(sys.argv[1])
    if sys.argv[2] == 'list':
        for i in range(1, p.n): print(i, p.ents[i][1], p.names[i] if i < len(p.names) else '?')
    else:
        os.makedirs(sys.argv[4], exist_ok=True)
        for i in range(1, p.n):
            nm = p.names[i]
            if sys.argv[3] in nm:
                d = p.read(i); o = os.path.join(sys.argv[4], nm.strip('/').replace('/', '_'))
                open(o, 'wb').write(d); print('wrote', o, len(d))
