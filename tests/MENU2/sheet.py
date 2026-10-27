import sys
from PIL import Image
out=sys.argv[1]; files=sys.argv[2:]
ims=[Image.open(f).convert('RGB') for f in files]
w=max(i.width for i in ims)//2; h=max(i.height for i in ims)//2
cols=2; rows=(len(ims)+1)//2
sheet=Image.new('RGB',(w*cols,h*rows))
for k,i in enumerate(ims):
    sheet.paste(i.resize((i.width//2,i.height//2)),( (k%cols)*w,(k//cols)*h))
sheet.save(out)
