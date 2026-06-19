from PIL import Image, ImageFont, ImageDraw

F = "/projects/sandbox/_fontwork/m6x11.ttf"
SIZE = 16
ATLAS_W = 256
GAP = 1

# Exact char list the UI uses (order irrelevant for explicit map, but cover all)
charset = list("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
charset += list("({[<!@#$%^&*?_+-=;,") + ['"'] + list("/~>]}).:'") + ['\\','|']

f = ImageFont.truetype(F, SIZE)
asc, desc = f.getmetrics()
canvasH = asc + desc + 4

# render each glyph -> binary, find ink bbox
glyphs = {}
gMinY, gMaxY = 999, -1
for ch in charset:
    im = Image.new("L", (SIZE*2, canvasH), 0)
    ImageDraw.Draw(im).text((0,0), ch, fill=255, font=f)
    px = im.load()
    W,H = im.size
    minx,miny,maxx,maxy = 999,999,-1,-1
    for y in range(H):
        for x in range(W):
            if px[x,y] >= 128:
                minx=min(minx,x); maxx=max(maxx,x); miny=min(miny,y); maxy=max(maxy,y)
    if maxx<0:  # empty glyph (e.g. space won't be here)
        glyphs[ch]=(im,0,0,-1,-1); continue
    glyphs[ch]=(im,minx,miny,maxx,maxy)
    gMinY=min(gMinY,miny); gMaxY=max(gMaxY,maxy)

bandH = gMaxY - gMinY + 1
print(f"ascent={asc} descent={desc} band rows {gMinY}..{gMaxY} bandH={bandH}")

# layout into atlas (256 wide, rows of bandH+GAP), 1px gap between glyphs
entries=[]  # (char, u, v, w, h)
x=GAP; y=GAP; rowh=bandH
placed={}
for ch in charset:
    im,minx,miny,maxx,maxy = glyphs[ch]
    w = (maxx-minx+1) if maxx>=0 else 3
    if x + w + GAP > ATLAS_W:
        x=GAP; y += bandH + GAP
    entries.append((ch, x, y, w, bandH))
    placed[ch]=(im,minx,w)
    x += w + GAP
atlasH_used = y + bandH + GAP
# round up to power of two
H=32
while H < atlasH_used: H*=2
print(f"atlas {ATLAS_W}x{H} (used {atlasH_used}), glyphs={len(entries)}")

# build RGBA atlas
atlas = Image.new("RGBA", (ATLAS_W, H), (0,0,0,0))
ap = atlas.load()
for (ch,u,v,w,h) in entries:
    im,minx,_ = placed[ch]
    sp = im.load()
    for yy in range(h):
        for xx in range(w):
            if sp[minx+xx, gMinY+yy] >= 128:
                ap[u+xx, v+yy] = (255,255,255,255)

# space width: use advance of 'n' minus a bit, or ~ a small value
spacew = max(3, int(f.getlength(" ")))
print("space width:", spacew)

# ---- verify: re-render a test string from the atlas+entries as ASCII ----
emap={e[0]:e for e in entries}
def draw_str(s):
    rows=['']*bandH
    for ch in s:
        if ch==' ':
            for i in range(bandH): rows[i]+=' '*spacew
            continue
        if ch not in emap: continue
        _,u,v,w,h=emap[ch]
        for i in range(bandH):
            line=''
            for xx in range(w):
                line += '#' if ap[u+xx,v+i][3] else '.'
            rows[i]+=line+'.'
    return rows
print("=== verify render 'Final Fight 3 %@' ===")
for r in draw_str("Final Fight 3 %@"):
    print(r)

# ---- emit C ----
data = atlas.tobytes()  # RGBA bytes, row-major
assert len(data)==ATLAS_W*H*4
with open("/projects/sandbox/_fontwork/font_m6x11.c","w") as o:
    o.write("/* Generated from m6x11 (Daniel Linssen) at size 16.\n")
    o.write("   Atlas %dx%d RGBA8, explicit glyph map (no auto-parse).\n" % (ATLAS_W,H))
    o.write("   Font: m6x11 by Daniel Linssen - free to use (attribution). */\n")
    o.write('#include "types.h"\n#include "font.h"\n\n')
    o.write("unsigned char _FontData_m6x11[%d] _ALIGN(16) = {\n" % (ATLAS_W*H*4))
    for i in range(0,len(data),16):
        o.write("    "+",".join("0x%02X"%b for b in data[i:i+16])+",\n")
    o.write("};\n\n")
    o.write("extern const FontMapEntryT _FontMap_m6x11[] = {\n")
    for (ch,u,v,w,h) in entries:
        o.write("    { %3d, %3d, %3d, %2d, %2d },\n" % (ord(ch),u,v,w,h))
    o.write("};\n\n")
    o.write("extern const int _FontMap_m6x11_count = %d;\n" % len(entries))
    o.write("extern const int _FontTex_m6x11_w = %d;\n" % ATLAS_W)
    o.write("extern const int _FontTex_m6x11_h = %d;\n" % H)
    o.write("extern const int _Font_m6x11_spacew = %d;\n" % spacew)
    o.write("extern const int _Font_m6x11_lineh = %d;\n" % bandH)
    o.write("extern const int _Font_m6x11_maxw = %d;\n" % max(e[3] for e in entries))
print("wrote font_m6x11.c, bytes:", ATLAS_W*H*4)
