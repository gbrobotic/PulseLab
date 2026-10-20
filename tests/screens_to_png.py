import sys, glob, os
from PIL import Image, ImageDraw
src, out = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(os.path.join(src, "*.pgm")))
S = 4                       # 4x zoom
cell_w, cell_h = 128*S + 24, 64*S + 44
cols = 2
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols*cell_w, rows*cell_h), (235, 235, 235))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(files):
    im = Image.open(f)
    big = Image.new("RGB", (128*S, 64*S), (8, 10, 14))
    px = im.load(); bd = ImageDraw.Draw(big)
    for y in range(64):
        for x in range(128):
            if px[x, y]:
                bd.rectangle([x*S, y*S, x*S+S-2, y*S+S-2], fill=(210, 235, 255))
    cx, cy = (i % cols)*cell_w + 12, (i // cols)*cell_h + 30
    sheet.paste(big, (cx, cy))
    name = os.path.basename(f)[:-4].split("_", 1)[1].replace("_", " ")
    d.text((cx, cy - 20), name, fill=(30, 30, 30))
sheet.save(out)
print(out, sheet.size)
