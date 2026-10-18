import numpy as np
from PIL import Image, ImageDraw, ImageFont
S=4
FONT = (lambda os: next((p for p in (                     # KBM_FONT overrides; Windows falls back to Arial Bold
    os.environ.get('KBM_FONT'),
    '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'Windows', 'Fonts', 'DejaVuSans-Bold.ttf'),
    os.path.join(os.environ.get('WINDIR', 'C:\\Windows'), 'Fonts', 'DejaVuSans-Bold.ttf'),
    os.path.join(os.environ.get('WINDIR', 'C:\\Windows'), 'Fonts', 'arialbd.ttf'),
    '/System/Library/Fonts/Supplemental/Arial Bold.ttf',
) if p and os.path.isfile(p)), 'DejaVuSans-Bold.ttf'))(__import__('os'))
CAP_FILL=(30,34,42,255); CAP_EDGE=(205,210,218,255); CAP_SHADE=(18,20,26,255); TXT=(255,255,255,255)
ACC=(236,196,40,255)
def font(px): return ImageFont.truetype(FONT, px)
def fit_text(dr, txt, maxw, maxh):
    px=int(maxh)
    while px>6:
        f=font(px); b=dr.textbbox((0,0),txt,font=f)
        if b[2]-b[0]<=maxw and b[3]-b[1]<=maxh: return f,b
        px-=2
    f=font(6); return f, dr.textbbox((0,0),txt,font=f)
def keycap(dr, x0,y0,x1,y1, txt, tscale=0.62):
    r=int(min(x1-x0,y1-y0)*0.22)
    dr.rounded_rectangle([x0,y0+2*S,x1,y1],radius=r,fill=CAP_SHADE)           # depth
    dr.rounded_rectangle([x0,y0,x1,y1-2*S],radius=r,fill=CAP_FILL,outline=CAP_EDGE,width=int(1.6*S))
    if txt:
        w=x1-x0; h=(y1-2*S)-y0
        f,b=fit_text(dr,txt,w*0.80,h*tscale)
        tw,th=b[2]-b[0],b[3]-b[1]
        dr.text((x0+(w-tw)/2-b[0], y0+(h-th)/2-b[1]), txt, font=f, fill=TXT)
def mouse(dr, x0,y0,x1,y1, hl):
    w=x1-x0; h=y1-y0
    mw=min(w*0.62,h*0.62); mh=h*0.92
    cx=(x0+x1)/2; top=y0+(h-mh)/2; bot=top+mh; l=cx-mw/2; r=cx+mw/2
    rad=mw*0.48
    dr.rounded_rectangle([l,top,r,bot],radius=rad,fill=CAP_FILL,outline=CAP_EDGE,width=int(1.6*S))
    split=top+mh*0.42
    if hl in ('L','R'):
        # fill the button half
        mask=Image.new('L',(int(x1-x0)+2,int(y1-y0)+2),0)
        md=ImageDraw.Draw(mask)
        md.rounded_rectangle([l-x0,top-y0,r-x0,bot-y0],radius=rad,fill=255)
        md.rectangle([0,split-y0,mask.size[0],mask.size[1]],fill=0)
        if hl=='L': md.rectangle([cx-x0,0,mask.size[0],mask.size[1]],fill=0)
        else: md.rectangle([0,0,cx-x0,mask.size[1]],fill=0)
        acc=Image.new('RGBA',mask.size,ACC)
        dr._image.paste(acc,(int(x0),int(y0)),mask)
        dr.rounded_rectangle([l,top,r,bot],radius=rad,outline=CAP_EDGE,width=int(1.6*S))
    dr.line([l+S,split,r-S,split],fill=CAP_EDGE,width=int(1.3*S))
    dr.line([cx,top+S,cx,split],fill=CAP_EDGE,width=int(1.3*S))
    ww=mw*0.16; wy0=top+mh*0.12; wy1=top+mh*0.30
    dr.rounded_rectangle([cx-ww/2,wy0,cx+ww/2,wy1],radius=ww/2,fill=ACC if hl=='M' else CAP_EDGE)
    if hl=='move':
        a=min(w,h)*0.09; m=S*2
        for (px,py,dx,dy) in ((l-m,(top+bot)/2,-1,0),(r+m,(top+bot)/2,1,0)):
            dr.polygon([(px+dx*a,py),(px,py-a*0.8),(px,py+a*0.8)],fill=ACC)
def cluster(dr, x0,y0,x1,y1, labels):
    # one key on top (centre), three below
    w=x1-x0; h=y1-y0; g=1.2*S
    kw=(w-2*g)/3; kh=(h-g)/2
    kw=min(kw,kh*1.15)
    tot=3*kw+2*g; ox=x0+(w-tot)/2
    keycap(dr, ox+kw+g, y0, ox+2*kw+g, y0+kh, labels[0], 0.70)
    for i,t in enumerate(labels[1:]):
        keycap(dr, ox+i*(kw+g), y0+kh+g, ox+i*(kw+g)+kw, y1, t, 0.70)
GL={ # code: (kind, arg)
 0xF080:('key','C'), 0xF081:('key','F'), 0xF082:('key','E'), 0xF083:('key','SPACE'),
 0xF084:('mouse','R'), 0xF085:('key','Q'), 0xF086:('mouse','L'), 0xF087:('key','R'),
 0xF088:('key','ESC'), 0xF089:('key','TAB'), 0xF08A:('cluster',['W','A','S','D']), 0xF08B:('key','V'),
 0xF08C:('mouse','move'), 0xF08D:('mouse','M'),
 0xF090:('cluster',['↑','←','↓','→']), 0xF091:('key','→'), 0xF092:('key','CTRL'),
 0xF093:('key','←'), 0xF094:('key','↓'),
}
CELLS={0xF080:(61,162,52,52),0xF081:(114,162,52,52),0xF082:(167,162,52,52),0xF083:(220,162,52,52),0xF084:(408,0,60,52),
 0xF085:(0,109,60,52),0xF086:(61,109,60,52),0xF087:(122,109,60,52),0xF088:(183,109,60,52),0xF089:(244,109,60,52),
 0xF08A:(305,109,60,52),0xF08B:(366,109,60,52),0xF08C:(427,53,60,52),0xF08D:(0,162,60,52),0xF090:(273,162,52,52),
 0xF091:(326,162,52,52),0xF092:(379,162,52,52),0xF093:(432,106,52,52),0xF094:(432,159,52,52)}
def build(vis_orig):
    out=vis_orig.copy()
    for code,(x,y,w,h) in CELLS.items():
        kind,arg=GL[code]
        im=Image.new('RGBA',(w*S,h*S),(0,0,0,0)); dr=ImageDraw.Draw(im)
        m=2*S; x0,y0,x1,y1=m,m,w*S-m,h*S-m
        if kind=='key':
            if len(arg)>2:  # wide label: full cell
                keycap(dr,x0,y0+3*S,x1,y1-3*S,arg,0.55)
            else:
                s=min(x1-x0,y1-y0); cx=(x0+x1)/2; cy=(y0+y1)/2
                keycap(dr,cx-s/2,cy-s/2,cx+s/2,cy+s/2,arg,0.62)
        elif kind=='mouse': mouse(dr,x0,y0,x1,y1,arg)
        else: cluster(dr,x0,y0,x1,y1,arg)
        small=np.array(im.resize((w,h),Image.LANCZOS))
        # premultiplied-safe: keep RGB where alpha>0
        out[y:y+h,x:x+w]=small
    return out
