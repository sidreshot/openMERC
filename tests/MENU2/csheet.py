# OpenMerc MENU2: contact sheet of the 4:3 canvas part of captures: csheet.py out.png scale in1 in2 ...
import sys
from PIL import Image
out, sc, files = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
ims = []
for p in files:
    im = Image.open(p); W, H = im.size
    fit = min(W / 640, H / 480); f = int(fit) if fit >= 2 else fit
    x0 = int((W - 640 * f) / 2); y0 = int((H - 480 * f) / 2)
    c = im.crop((x0, y0, x0 + int(640 * f), y0 + int(480 * f)))
    ims.append(c.resize((int(c.width * sc / f), int(c.height * sc / f))))
cols = 4; w, h = ims[0].size
sheet = Image.new('RGB', (w * cols, h * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % cols) * w, (i // cols) * h))
sheet.save(out)
