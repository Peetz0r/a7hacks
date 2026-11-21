import sys,pickle,subprocess,os,tempfile; sys.path.insert(0,'.')
import numpy as np
from PIL import Image
from uxc import *; from ocr import *
from concurrent.futures import ThreadPoolExecutor
cat=pickle.load(open('cat.pkl','rb')); by={r['h_']:r for r in cat}
S=pickle.load(open('scan.pkl','rb'))
WL='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-/%'
tmp=tempfile.mkdtemp()
def norm(s): return s.upper().replace('0','O').replace('1','I').replace('L','I').replace('J','I')
def sim(a,b):
    import difflib; return difflib.SequenceMatcher(None,norm(a),norm(b)).ratio()
def job(arg):
    k,li,p,l=arg
    g,a=split(p)
    comps=components(p,l['light'])
    boxes={(b[0],b[1],b[2],b[3]) for b in l['boxes']}
    m=np.zeros(p.shape,bool)
    for c in comps:
        if (c['x0'],c['y0'],c['x1'],c['y1']) in boxes: m[c['y0']:c['y1'],c['x0']:c['x1']]|=c['mask']
    ys,xs=np.nonzero(m); m=m[ys.min():ys.max()+1, xs.min():xs.max()+1]
    H=m.shape[0]; sc=max(2,int(48/max(H,1)))
    im=Image.fromarray(np.where(m,0,255).astype(np.uint8)).resize((m.shape[1]*sc,m.shape[0]*sc),Image.NEAREST)
    pad=Image.new('L',(im.size[0]+40,im.size[1]+40),255); pad.paste(im,(20,20))
    fn=os.path.join(tmp,'%d_%d.png'%(k,li)); pad.save(fn)
    r=subprocess.run(['tesseract',fn,'-','--psm','7','-c','tessedit_char_whitelist='+WL],capture_output=True,text=True)
    os.remove(fn)
    t=r.stdout.strip().replace(' ','')
    return k,li,t
jobs=[]
for k,s in enumerate(S):
    p=by[s['h_']]['pic']
    for li,l in enumerate(s['lines']): jobs.append((k,li,p,l))
with ThreadPoolExecutor(8) as ex:
    for n,(k,li,t) in enumerate(ex.map(job,jobs)):
        S[k]['lines'][li]['tess']=t; S[k]['lines'][li]['sim']=sim(t,S[k]['lines'][li]['text'])
        if n%1000==0: print(n,len(jobs),flush=True)
pickle.dump(S,open('scan2.pkl','wb')); print('done')
