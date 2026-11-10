"""Measures Tom's world textures (Art/Textures: ground, stone, cliff; made as albedos by his AI friend and turned into
full sets in his NoorMaterial) and writes what the shaders need to know about them:
  Tools/WwGroundMeans.txt   one "r g b" line per ground layer, in the shader's order (display values 0..1)
  Tools/WwRockMeans.txt     the mean brightness of each stone look and each cliff face, two lines
Run by Tools/ImportTextures.cmd before the engine imports them (system Python with Pillow)."""
import glob, os, sys
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
art = os.path.join(here, '..', 'Art', 'Textures')


def means(folder, count):
    files = sorted(glob.glob(os.path.join(art, folder, '*_albedo.png')))
    if len(files) != count:
        sys.exit('%s: %d albedo textures, %d wanted' % (folder, len(files), count))
    out = []
    for f in files:
        r, g, b = Image.open(f).convert('RGB').resize((1, 1), Image.BOX).getpixel((0, 0))
        out.append((r / 255.0, g / 255.0, b / 255.0))
    return out


ground = means('Ground', 16)
with open(os.path.join(here, 'WwGroundMeans.txt'), 'w') as f:
    for c in ground:
        f.write('%.4f %.4f %.4f\n' % c)
with open(os.path.join(here, 'WwRockMeans.txt'), 'w') as f:
    for folder in ('Stone', 'Cliff'):
        f.write(', '.join('%.3f' % ((c[0] + c[1] + c[2]) / 3) for c in means(folder, 6)) + '\n')
print('Measured 16 ground layers, 6 stone looks and 6 cliff faces')
