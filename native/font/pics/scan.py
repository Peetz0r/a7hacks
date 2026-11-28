import sys as _sys
_HELP = """usage: python3 scan.py          (step 1 of 5, see README)
Finds lettering in every picture of cat.pkl (the camera's pictures, unpacked) with ocr.py and writes the
lines found to scan.pkl. Reads $UNIVERS (the camera's Univers as a .ttf, default univers.ttf).

This help text was written by an LLM"""
if __name__ == '__main__' and _sys.argv[1:] in (['--help'], ['-h']):
    print(_HELP)
    _sys.exit(0)
import sys,pickle; sys.path.insert(0,'.')
from uxc import *; from ocr import *
cat=pickle.load(open('cat.pkl','rb'))
seen={}; res=[]
for k,r in enumerate(cat):
    if r['h_'] in seen: continue
    seen[r['h_']]=1
    p=r['pic']
    if p.shape[0]*p.shape[1]>400*300: continue
    try: L=[l for l in lines(p) if accept(l)]
    except Exception as e: L=[]
    if L:
        res.append(dict(fn=r['fn'],res=r['res'],h_=r['h_'],lines=[dict(text=l['text'],score=float(l['score']),light=l['light'],H=l['H'],
            boxes=[(c['x0'],c['y0'],c['x1'],c['y1'],c['ch'],float(c['score'])) for c in l['chars']]) for l in L]))
    if k%1000==0: print(k,len(res),flush=True)
pickle.dump(res,open('scan.pkl','wb')); print('done',len(res))
