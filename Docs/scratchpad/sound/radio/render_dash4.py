import struct
from PIL import Image
p = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP\DASH4.BMP"
im = Image.open(p).convert("RGB")
im = im.resize((im.width*3, im.height*3), Image.NEAREST)
im.save(r"S:\Repos\sim-copter-remake\Docs\scratchpad\sound\radio\dash4_x3.png")
print("saved", im.size)
