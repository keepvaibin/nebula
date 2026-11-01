"""check_normalmap_gradient.py: NormalMapBase::createGradTexture writes LA8 rows by a formula for the picture the game
showed when it wrote big-endian IA8 rows and ran TDDraw::tileConversion16 (as decompiled) on them. This simulates the
old path (rows, the tile conversion, the GX IA8 tile read) for random ramps and checks every texel of the formula."""
import random
W=H=256
I=[random.randrange(256) for _ in range(W)]; A=[random.randrange(256) for _ in range(H)]
# old: BE u16 linear buffer
buf=bytearray(W*H*2)
for y in range(H):
    for x in range(W):
        v=(A[y]<<8)|I[x]; buf[2*(y*W+x)]=v>>8; buf[2*(y*W+x)+1]=v&255
# tileConversion16 as decompiled
tile=bytearray(0x800); dest=0
for y in range(0,H,4):
    off=0
    for x in range(0,W,4):
        for r in range(4):
            for c in range(4):
                L=(y+r)*W+(x+c); v=(buf[2*L]<<8)|buf[2*L+1]
                tile[off]=v&255; off+=1
    buf[dest:dest+32]=tile[:32]; dest+=32
# GX IA8 decode (4x4 tiles, A then I)
old={}
t=0
for ty in range(H//4):
    for tx in range(W//4):
        for j in range(16):
            r,c=j//4,j%4; o=32*t+2*j
            old[(tx*4+c,ty*4+r)]=(buf[o+1],buf[o])  # (I,A)
        t+=1
# new formula
bad=0
for y in range(H):
    for x in range(W):
        tl=(y//4)*(W//4)+x//4; j=(y%4)*4+x%4
        if tl < H//4:
            f=2*j; s=f+1
            a=I[f%4 if f<16 else 4+(f-16)%4]; i=I[s%4 if s<16 else 4+(s-16)%4]
        else:
            w=tl*16+j; a=A[w//W]; i=I[w%W]
        if old[(x,y)]!=(i,a): bad+=1
print('mismatches',bad)
raise SystemExit(1 if bad else 0)
