# Prepares Noor's ground textures for import (run by Tools/ImportAssets.cmd with the system Python; needs Pillow).
# Reads Thornwake's Assets/Terrain/Noor/*.png (512 px; the source is only read) and, as Thornwake's GroundTextures.cs
# does at load time, resizes each to 256 px (Lanczos) and takes its mean colour. Writes
#   Saved/WwAssets/Ground/T_Ground_<nn>_<name>.png   the 256 px layers, in the shader's order
#   Tools/WwGroundMeans.txt                            one "r g b" line per layer (display values, 0..1)
import os
import sys
from PIL import Image

FILES = ['grass', 'dirt', 'sand', 'rock', 'river_bed', 'sea_bed', 'forest_floor', 'gravel',
         'mud', 'wet_mud', 'moss', 'scree', 'snow', 'swamp_peat', 'ice', 'volcanic_rock']
here = os.path.dirname(os.path.abspath(__file__))
source = os.path.join(os.environ.get('WW_THORNWAKE', r'F:\Codex\ThornwakeNEW'), 'Assets', 'Terrain', 'Noor')
target = os.path.join(here, '..', 'Saved', 'WwAssets', 'Ground')
os.makedirs(target, exist_ok=True)
means = []
for index, name in enumerate(FILES):
    path = os.path.join(source, name + '.png')
    if not os.path.isfile(path):
        sys.exit('Missing ' + path)
    image = Image.open(path).convert('RGBA')
    if image.width != 256:
        image = image.resize((256, 256), Image.LANCZOS)
    r = g = b = 0
    pixels = image.load()
    for y in range(0, 256, 2):
        for x in range(0, 256, 2):
            p = pixels[x, y]
            r += p[0]; g += p[1]; b += p[2]
    n = 128 * 128 * 255.0
    means.append('%.6f %.6f %.6f' % (r / n, g / n, b / n))
    image.save(os.path.join(target, 'T_Ground_%02d_%s.png' % (index, name)))
with open(os.path.join(here, 'WwGroundMeans.txt'), 'w', newline='\n') as f:
    f.write('\n'.join(means) + '\n')
print('Prepared %d ground layers' % len(FILES))
