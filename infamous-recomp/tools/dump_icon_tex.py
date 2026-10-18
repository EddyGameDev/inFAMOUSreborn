import os, sys, struct
sys.path.insert(0, 'tools')
from psarc import Psarc
ARC = os.path.join('..', 'Infamous (Europe) (Fr,De,Es,It,Nl)', 'PS3_GAME', 'USRDIR', 'cache', 'psarc', 'install1', 'infamous1.psarc_s')
p = Psarc(ARC); d = p.read(p.names.index('/lang_english.xpp'))
B = 0x164
oid = 120625
k = 0
while True:
    k = d.find(struct.pack('>I', oid), k + 1)
    w, h = struct.unpack('>II', d[k+4:k+12])
    if w in (256, 512, 1024, 2048) and h in (128, 256, 512, 1024):
        off, fmt = struct.unpack('>II', d[k+0x20:k+0x28]); break
print('tex', w, h, hex(fmt), 'rec@', hex(k), 'data@', hex(off + B), 'rec bytes', d[k-8:k+0x40].hex())
nb = w * h  # DXT5 = 1 byte per texel
os.makedirs('assets', exist_ok=True)
open('assets/font_icons_l0.bin', 'wb').write(d[off + B: off + B + nb * 2])
print('wrote', nb * 2)
