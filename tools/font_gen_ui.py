from PIL import Image, ImageFont, ImageDraw
FONT="/projects/sandbox/_fontwork/m5x7.ttf"; SIZE=16; NAME="ui"; ATLAS_W=256; GAP=1
charset=list("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
charset+=list("({[<!@#$%^&*?_+-=;,")+['"']+list("/~>]}).:'")+['\\','|']
f=ImageFont.truetype(FONT,SIZE); asc,desc=f.getmetrics(); canvasH=asc+desc+4
glyphs={}; gMinY,gMaxY=999,-1
for ch in charset:
    im=Image.new("L",(SIZE*2,canvasH),0); ImageDraw.Draw(im).text((0,0),ch,fill=255,font=f); px=im.load(); W,H=im.size
    mnx,mny,mxx,mxy=999,999,-1,-1
    for y in range(H):
        for x in range(W):
            if px[x,y]>=128: mnx=min(mnx,x);mxx=max(mxx,x);mny=min(mny,y);mxy=max(mxy,y)
    glyphs[ch]=(im,mnx,mny,mxx,mxy)
    if mxx>=0: gMinY=min(gMinY,mny);gMaxY=max(gMaxY,mxy)
bandH=gMaxY-gMinY+1; print("band",gMinY,gMaxY,"bandH",bandH)
entries=[];x=GAP;y=GAP
place={}
for ch in charset:
    im,mnx,mny,mxx,mxy=glyphs[ch]; w=(mxx-mnx+1) if mxx>=0 else 3
    if x+w+GAP>ATLAS_W: x=GAP;y+=bandH+GAP
    entries.append((ch,x,y,w,bandH)); place[ch]=(im,mnx,w); x+=w+GAP
used=y+bandH+GAP; H=16
while H<used:H*=2
print("atlas",ATLAS_W,"x",H,"used",used,"glyphs",len(entries))
atlas=Image.new("RGBA",(ATLAS_W,H),(0,0,0,0)); ap=atlas.load()
for (ch,u,v,w,h) in entries:
    im,mnx,_=place[ch]; sp=im.load()
    for yy in range(h):
        for xx in range(w):
            if sp[mnx+xx,gMinY+yy]>=128: ap[u+xx,v+yy]=(255,255,255,255)
spacew=max(3,int(f.getlength(" "))); maxw=max(e[3] for e in entries)
emap={e[0]:e for e in entries}
def ds(s):
    rows=['']*bandH
    for ch in s:
        if ch==' ':
            for i in range(bandH):rows[i]+=' '*spacew
            continue
        if ch not in emap:continue
        _,u,v,w,h=emap[ch]
        for i in range(bandH):
            rows[i]+=''.join('#' if ap[u+xx,v+i][3] else '.' for xx in range(w))+'.'
    return rows
print("=== verify 'Final Fight 3 ABCxyz' ===")
for r in ds("Final Fight 3 ABCxyz"): print(r)
data=atlas.tobytes()
with open("/projects/sandbox/_fontwork/font_%s.cpp"%NAME,"w") as o:
    o.write("/* UI font atlas - generated from m5x7 (Daniel Linssen, CC0) @ size %d.\n"%SIZE)
    o.write("   Atlas %dx%d RGBA8 + explicit glyph map. Regenerate via tools/font_gen_ui.py. */\n"%(ATLAS_W,H))
    o.write('#include "types.h"\n#include "font.h"\n\n')
    o.write("unsigned char _FontData_%s[%d] _ALIGN(16) = {\n"%(NAME,ATLAS_W*H*4))
    for i in range(0,len(data),16): o.write("    "+",".join("0x%02X"%b for b in data[i:i+16])+",\n")
    o.write("};\n\nextern const FontMapEntryT _FontMap_%s[] = {\n"%NAME)
    for (ch,u,v,w,h) in entries: o.write("    { %3d, %3d, %3d, %2d, %2d },\n"%(ord(ch),u,v,w,h))
    o.write("};\n\n")
    o.write("extern const int _FontMap_%s_count = %d;\n"%(NAME,len(entries)))
    o.write("extern const int _FontTex_%s_w = %d;\n"%(NAME,ATLAS_W))
    o.write("extern const int _FontTex_%s_h = %d;\n"%(NAME,H))
    o.write("extern const int _Font_%s_spacew = %d;\n"%(NAME,spacew))
    o.write("extern const int _Font_%s_lineh = %d;\n"%(NAME,bandH))
    o.write("extern const int _Font_%s_maxw = %d;\n"%(NAME,maxw))
print("wrote font_%s.cpp  spacew=%d lineh=%d maxw=%d"%(NAME,spacew,bandH,maxw))
