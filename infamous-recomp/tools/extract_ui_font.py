"""Build assets/ui_font.bin (in-game system-dialog font) from the game's own data.

Reads lang_<language>.xpp from USRDIR/cache/psarc/install1/infamous1.psarc_s and takes:
  font_main_latin   (oid 133857): 141-glyph table, atlas font_main_latin_00.dds 1024x512 A8R8G8B8 swizzled
  font_icons        (oid 133855): 21-glyph table, atlas font_icons_00.dds 512x256 DXT5 (PS buttons, logo)
PACK (.xpp) notes: offsets inside records are relative to the end of the 0x164-byte header; GPU textures are
stored bottom-up (flip). Glyph = >HHHHHhhHHH {code, x, y, w, h, xoff, yoff, advance, 0, page}.
Font record (type 0x15C): {oid, 0, texture oid, f32 scale, u16 line, u16 0, u32 table, u32 count}.

Output (little-endian): "IFUIFNT1", u32 W, H, n_text, text_line, n_icon, icon_line, white_x, white_y,
then (n_text + n_icon) x {u32 code, i16 x, y, w, h, xoff, yoff, adv, 0}, then W*H RGBA8.
Text glyphs live at (0,0) as white+coverage alpha, icons at (0,512) as RGBA, an 8x8 white block at (1008,1008).

usage: python extract_ui_font.py [language=english]   (run from infamous-recomp/)
"""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psarc import Psarc
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
ARC = os.path.join(ROOT, '..', 'Infamous (Europe) (Fr,De,Es,It,Nl)', 'PS3_GAME', 'USRDIR', 'cache', 'psarc', 'install1', 'infamous1.psarc_s')
lang = sys.argv[1] if len(sys.argv) > 1 else 'english'
p = Psarc(ARC); d = p.read(p.names.index('/lang_%s.xpp' % lang))
assert d[:4] == b'PACK'
B = 0x164

def find_font(tex_oid):
    """font record = {.., 0xFFFFFFFF?, tex oid, f32 scale, u16 line, 0, table, count} -- locate by the texture oid."""
    k = 0
    while True:
        k = d.find(struct.pack('>I', tex_oid), k + 1)
        if k < 0: raise SystemExit('font record for texture oid %d not found' % tex_oid)
        scale = struct.unpack('>f', d[k+4:k+8])[0]
        line, z, tab, n = struct.unpack('>HHII', d[k+8:k+20])
        if 0.001 < scale < 1 and 10 <= line <= 400 and z == 0 and 0 < n < 2000 and tab + B + 20 * n <= len(d):
            g = [struct.unpack('>HHHHHhhHHH', d[tab+B+20*i: tab+B+20*i+20]) for i in range(n)]
            if all(e[3] < 1100 and e[4] < 600 for e in g): return line, g

def find_tex(tex_oid):
    """texture record: {0x?, oid, u32 w, u32 h, ..., +0x24 data offset, +0x28 GCM format word}"""
    k = 0
    while True:
        k = d.find(struct.pack('>I', tex_oid), k + 1)
        if k < 0: raise SystemExit('texture %d not found' % tex_oid)
        w, h = struct.unpack('>II', d[k+4:k+12])
        if w in (256, 512, 1024, 2048) and h in (128, 256, 512, 1024):
            off, fmt = struct.unpack('>II', d[k+0x20:k+0x28])
            return w, h, off + B, (fmt >> 8) & 0xFF

def unswizzle_argb(off, W, H):
    raw = np.frombuffer(d[off:off+W*H*4], np.uint8).reshape(-1, 4)
    xs, ys = np.meshgrid(np.arange(W), np.arange(H)); idx = np.zeros_like(xs); bit = 0
    lw, lh = W.bit_length()-1, H.bit_length()-1
    for i in range(max(lw, lh)):
        if i < lw: idx |= ((xs >> i) & 1) << bit; bit += 1
        if i < lh: idx |= ((ys >> i) & 1) << bit; bit += 1
    return raw[idx.ravel()].reshape(H, W, 4)[::-1]

def dxt5(off, W, H):
    out = np.zeros((H, W, 4), np.uint8); p = off
    def c565(c): return [((c>>11)&31)*255//31, ((c>>5)&63)*255//63, (c&31)*255//31]
    for by in range(H//4):
        for bx in range(W//4):
            b = d[p:p+16]; p += 16
            a0, a1 = b[0], b[1]; ab = int.from_bytes(b[2:8], 'little')
            al = [a0, a1] + ([(a0*(7-i)+a1*i)//7 for i in range(1,7)] if a0 > a1 else [(a0*(5-i)+a1*i)//5 for i in range(1,5)] + [0,255])
            c0, c1 = struct.unpack('<HH', b[8:12]); ci = struct.unpack('<I', b[12:16])[0]
            C0, C1 = c565(c0), c565(c1)
            C = [C0, C1, [(2*a+bb)//3 for a, bb in zip(C0, C1)], [(a+2*bb)//3 for a, bb in zip(C0, C1)]]
            for i in range(16):
                y, x = divmod(i, 4)
                out[by*4+y, bx*4+x, :3] = C[(ci >> (2*i)) & 3]; out[by*4+y, bx*4+x, 3] = al[(ab >> (3*i)) & 7]
    return out[::-1]

TXT_TEX, ICO_TEX = 120626, 120625     # font_main_latin_00.dds, font_icons_00.dds (oid = manifest line - 1)
tl, tg = find_font(TXT_TEX); il, ig = find_font(ICO_TEX)
tw, th, toff, tfmt = find_tex(TXT_TEX); iw, ih, ioff, ifmt = find_tex(ICO_TEX)
print('text font: %d glyphs line %d, atlas %dx%d fmt 0x%02X @0x%X' % (len(tg), tl, tw, th, tfmt, toff))
print('icon font: %d glyphs line %d, atlas %dx%d fmt 0x%02X @0x%X' % (len(ig), il, iw, ih, ifmt, ioff))
assert tfmt == 0x85 and ifmt == 0x88, 'unexpected texture formats'
W = H = 1024
atlas = np.zeros((H, W, 4), np.uint8)
txt = unswizzle_argb(toff, tw, th)
cov = txt[..., 3] if txt[..., 3].mean() > txt[..., 0].mean() else txt[..., 0]
atlas[:th, :tw, :3] = 255; atlas[:th, :tw, 3] = cov
atlas[512:512+ih, :iw] = dxt5(ioff, iw, ih)
atlas[1008:1016, 1008:1016] = 255
recs = b''.join(struct.pack('<Ihhhhhhhh', c, x, y, w, h, xo, yo, adv, 0) for (c, x, y, w, h, xo, yo, adv, z, pg) in tg)
recs += b''.join(struct.pack('<Ihhhhhhhh', c, x, y + 512, w, h, xo, yo, adv, 0) for (c, x, y, w, h, xo, yo, adv, z, pg) in ig)
out = os.path.join(ROOT, 'assets', 'ui_font.bin')
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'wb') as f:
    f.write(b'IFUIFNT1' + struct.pack('<8I', W, H, len(tg), tl, len(ig), il, 1012, 1012) + recs + atlas.tobytes())
print('wrote', out, os.path.getsize(out), 'bytes')
try:
    from PIL import Image; Image.fromarray(atlas).save(os.path.join(ROOT, 'assets', 'ui_font_preview.png'))
except Exception as e: print('(no preview:', e, ')')
