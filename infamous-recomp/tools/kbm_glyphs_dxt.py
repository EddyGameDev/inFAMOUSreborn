import numpy as np, struct
def dxt5_decode(b, W, H):
    out = np.zeros((H, W, 4), np.uint8); p = 0
    def c565(c): return [((c>>11)&31)*255//31, ((c>>5)&63)*255//63, (c&31)*255//31]
    for by in range(H//4):
        for bx in range(W//4):
            blk = b[p:p+16]; p += 16
            a0, a1 = blk[0], blk[1]; ab = int.from_bytes(blk[2:8], 'little')
            al = [a0, a1] + ([(a0*(7-i)+a1*i)//7 for i in range(1,7)] if a0 > a1 else [(a0*(5-i)+a1*i)//5 for i in range(1,5)] + [0,255])
            c0, c1 = struct.unpack('<HH', blk[8:12]); ci = struct.unpack('<I', blk[12:16])[0]
            C0, C1 = c565(c0), c565(c1)
            C = [C0, C1, [(2*a+bb)//3 for a, bb in zip(C0, C1)], [(a+2*bb)//3 for a, bb in zip(C0, C1)]]
            for i in range(16):
                y, x = divmod(i, 4)
                out[by*4+y, bx*4+x, :3] = C[(ci >> (2*i)) & 3]; out[by*4+y, bx*4+x, 3] = al[(ab >> (3*i)) & 7]
    return out
def to565(c):
    r,g,b=[int(v) for v in c]
    return ((r*31+127)//255<<11)|((g*63+127)//255<<5)|((b*31+127)//255)
def from565(v): return np.array([((v>>11)&31)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31],np.float64)
def dxt5_encode(img):
    H,W,_=img.shape; out=bytearray()
    for by in range(H//4):
        for bx in range(W//4):
            blk=img[by*4:by*4+4,bx*4:bx*4+4].reshape(16,4).astype(np.float64)
            a=blk[:,3]; amax=int(a.max()); amin=int(a.min())
            if amax==amin:
                a0,a1=amax,amin; idx=[0]*16
            else:
                a0,a1=amax,amin  # a0>a1 -> 8-level mode
                pal=[a0,a1]+[(a0*(7-i)+a1*i)//7 for i in range(1,7)]
                idx=[int(np.argmin([abs(v-q) for q in pal])) for v in a]
            ab=0
            for i,v in enumerate(idx): ab|=v<<(3*i)
            out+=bytes([a0,a1])+ab.to_bytes(6,'little')
            rgb=blk[:,:3]; w8=a>8
            pts=rgb[w8] if w8.any() else rgb
            mean=pts.mean(0); cov=np.cov((pts-mean).T) if len(pts)>1 else np.eye(3)
            try:
                ev,evec=np.linalg.eigh(cov); axis=evec[:,-1]
            except Exception: axis=np.array([1,1,1])/3**.5
            proj=(pts-mean)@axis
            e0=mean+axis*proj.max(); e1=mean+axis*proj.min()
            e0=np.clip(e0,0,255); e1=np.clip(e1,0,255)
            c0=to565(e0); c1=to565(e1)
            if c0<c1: c0,c1=c1,c0
            if c0==c1:
                ci=0
            else:
                P=[from565(c0),from565(c1)]; P+= [(2*P[0]+P[1])/3,(P[0]+2*P[1])/3]
                ci=0
                for i in range(16):
                    d=[((rgb[i]-q)**2).sum() for q in P]; ci|=int(np.argmin(d))<<(2*i)
            out+=struct.pack('<HHI',c0,c1,ci)
    return bytes(out)
def tex_csum(b, n):
    h=2166136261; step=n//1024 if n>4096 else 4
    for i in range(0,n-3,step):
        h^=struct.unpack('<I',b[i:i+4])[0]; h=(h*16777619)&0xffffffff
    return h
