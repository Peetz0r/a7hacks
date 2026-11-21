import numpy as np
MOR=[]
for i in range(64):
    x=y=0
    for bit in range(3):
        x|=((i>>(2*bit))&1)<<bit; y|=((i>>(2*bit+1))&1)<<bit
    MOR.append((x,y))
def untile(raw,W,H):
    """tiled texture bytes -> array [row][col] of u16, row 0 = first row in memory order (bottom)"""
    a=np.frombuffer(raw,'<u2'); out=np.zeros((H,W),np.uint16); tw=W//8
    for t in range((H//8)*tw):
        ty,tx=divmod(t,tw)
        for i,(x,y) in enumerate(MOR): out[ty*8+y,tx*8+x]=a[t*64+i]
    return out
def tile(img):
    H,W=img.shape; a=np.zeros(H*W,np.uint16); tw=W//8
    for t in range((H//8)*tw):
        ty,tx=divmod(t,tw)
        for i,(x,y) in enumerate(MOR): a[t*64+i]=img[ty*8+y,tx*8+x]
    return a.tobytes()
