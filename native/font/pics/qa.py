import sys as _sys
_HELP = """usage: python3 qa.py            (a check, not a step)
Before/after sheets of a fixed set of pictures for checking by eye: qa1.png. Reads scan2.pkl, cat.pkl and
overrides.pkl (if there).

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
import pickle,sys,random; sys.path.insert(0,'.')
import numpy as np, render
from PIL import Image, ImageDraw
from mkpics import fixed
from ocr import finalize2, split, confirmed_lines
S=pickle.load(open('scan2.pkl','rb')); cat=pickle.load(open('cat.pkl','rb'))
byh={}
for r in cat: byh.setdefault(r['h_'],r)
byres={(r['fn'],r['res']):r for r in cat}
render.LEVELS=8
import os
OV=pickle.load(open('overrides.pkl','rb')) if os.path.exists('overrides.pkl') else {}
def show(p,sc):
    g,a=split(p); bg=np.full(p.shape,70.0)
    v=(g*17*a+bg*(15-a))/15
    return Image.fromarray(v.astype('uint8')).resize((p.shape[1]*sc,p.shape[0]*sc),Image.NEAREST)
def showc(p,sc,bg=(40,50,90)):
    a=(p&15).astype(float)/15; ch=[((p>>k)&15).astype(float)*17 for k in (12,8,4)]
    v=np.stack([c*a+bg[i]*(1-a) for i,c in enumerate(ch)],-1)
    return Image.fromarray(v.astype('uint8'),'RGB').resize((p.shape[1]*sc,p.shape[0]*sc),Image.NEAREST)
def buildc(keys,out,sc=2,cols=2):
    tiles=[]
    sl={s['h_']:s for s in S}
    for k in keys:
        r=k if isinstance(k,dict) else byres.get(k)
        if r is None or (r['h_'] not in sl and r['h_'] not in OV): continue
        p=r['pic']; L=confirmed_lines(sl[r['h_']]['lines'] if r['h_'] in sl else [],p,OV.get(r['h_'],()))
        q=fixed(p,L) if L else p; a=showc(p,sc); b=showc(q,sc)
        t=Image.new('RGB',(a.size[0]*2+16,a.size[1]+14),(40,40,40)); t.paste(a,(0,14)); t.paste(b,(a.size[0]+16,14))
        ImageDraw.Draw(t).text((2,1),'%x %s'%(r['res'],' | '.join(l['text'] for l in L)),fill=(230,230,230))
        tiles.append(t)
    W=max(t.size[0] for t in tiles); rows=(len(tiles)+cols-1)//cols
    hs=[max(t.size[1] for t in tiles[i*cols:(i+1)*cols]) for i in range(rows)]
    sh=Image.new('RGB',(W*cols+10*cols,sum(hs)+4*rows),(25,25,25)); y=0
    for i in range(rows):
        for j,t in enumerate(tiles[i*cols:(i+1)*cols]): sh.paste(t,(j*(W+10),y))
        y+=hs[i]+4
    sh.save(out); print(out,sh.size,len(tiles))
def build(keys,out,sc=2,cols=2):
    tiles=[]
    sl={s['h_']:s for s in S}
    for k in keys:
        r=k if isinstance(k,dict) else byres.get(k)
        if r is None or (r['h_'] not in sl and r['h_'] not in OV): continue
        p=r['pic']; L=confirmed_lines(sl[r['h_']]['lines'] if r['h_'] in sl else [],p,OV.get(r['h_'],()))
        if not L: continue
        q=fixed(p,L); a=show(p,sc); b=show(q,sc)
        t=Image.new('L',(a.size[0]*2+16,a.size[1]+14),40); t.paste(a,(0,14)); t.paste(b,(a.size[0]+16,14))
        ImageDraw.Draw(t).text((2,1),'%x %s'%(r['res'],' | '.join(l['text'] for l in L)),fill=230)
        tiles.append(t)
    W=max(t.size[0] for t in tiles); rows=(len(tiles)+cols-1)//cols
    hs=[max(t.size[1] for t in tiles[i*cols:(i+1)*cols]) for i in range(rows)]
    sh=Image.new('L',(W*cols+10*cols,sum(hs)+4*rows),25); y=0
    for i in range(rows):
        for j,t in enumerate(tiles[i*cols:(i+1)*cols]): sh.paste(t,(j*(W+10),y))
        y+=hs[i]+4
    sh.save(out); print(out,sh.size,len(tiles))
if __name__=='__main__':
    C='image_cmn_43.uxc'
    keys=[(C,x) for x in (0x205c679,0x205df40,0x205df2e,0x205df8f,0x205dfd2,0x205cb20,0x205cb26,0x205cb30,0x205cb38,0x205c365,0x205c550,0x205da60,0x205c5d5,0x205df57,0x205c461,0x205c1c8,0x205c0be,0x205c130,0x205c4d1,0x205c4b3,0x205dccf,0x205d9a5,0x205c5cf,0x205c74f)]
    build(keys,'qa1.png')
