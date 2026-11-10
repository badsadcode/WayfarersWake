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

# The plant sprites (Thornwake Assets/Flora, 256 px, bottom-aligned), in the order of Flora.cs (= texture layers).
PLANTS = ['grass_short_a', 'grass_short_b', 'grass_medium_a', 'grass_medium_b', 'grass_tall_a', 'grass_tall_b', 'grass_tall_c',
          'grass_tall_d', 'grass_lush', 'grass_seeded', 'wheat_young', 'wheat_golden', 'reeds_common', 'reeds_rush', 'cattails',
          'flowering_aquatic', 'lily_pads', 'water_lilies', 'duckweed', 'submerged_plant', 'seagrass', 'eelgrass_tall',
          'broadleaf_aquatic', 'feathery_aquatic', 'mixed_aquatic', 'green_algae', 'red_algae', 'kelp']
source = os.path.join(os.environ.get('WW_THORNWAKE', r'F:\Codex\ThornwakeNEW'), 'Assets', 'Flora')
target = os.path.join(here, '..', 'Saved', 'WwAssets', 'Plants')
os.makedirs(target, exist_ok=True)
for index, name in enumerate(PLANTS):
    path = os.path.join(source, name + '.png')
    if not os.path.isfile(path):
        sys.exit('Missing ' + path)
    image = Image.open(path).convert('RGBA')
    if image.width != 256:
        image = image.resize((256, 256), Image.LANCZOS)
    image.save(os.path.join(target, 'T_Plant_%02d_%s.png' % (index, name)))
print('Prepared %d plant sprites' % len(PLANTS))

# The two plant meshes of Thornwake's Flora.cs, written as glTF (metres, Y up) for the importer.
#   upright: three 1 m quads crossing at 60 degrees, their tops leaning ~14 degrees outward, normals leaning up
#   flat:    one 1 m quad lying on the water
import base64
import json
import math
import struct


def gltf(name, positions, normals, uvs):
    count = len(positions)
    data = b''.join(struct.pack('<3f', *p) for p in positions) + b''.join(struct.pack('<3f', *n) for n in normals) \
        + b''.join(struct.pack('<2f', *t) for t in uvs) + b''.join(struct.pack('<H', i) for i in range(count))
    lo = [min(p[i] for p in positions) for i in range(3)]
    hi = [max(p[i] for p in positions) for i in range(3)]
    doc = {
        'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0, 'name': name}],
        'meshes': [{'name': name, 'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}, 'indices': 3, 'material': 0}]}],
        'materials': [{'name': 'M_Plant', 'doubleSided': True}],
        'buffers': [{'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(data).decode('ascii')}],
        'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': count * 12}, {'buffer': 0, 'byteOffset': count * 12, 'byteLength': count * 12},
                        {'buffer': 0, 'byteOffset': count * 24, 'byteLength': count * 8}, {'buffer': 0, 'byteOffset': count * 32, 'byteLength': count * 2}],
        'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': lo, 'max': hi},
                      {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
                      {'bufferView': 2, 'componentType': 5126, 'count': count, 'type': 'VEC2'},
                      {'bufferView': 3, 'componentType': 5123, 'count': count, 'type': 'SCALAR'}],
    }
    with open(os.path.join(target, name + '.gltf'), 'w', newline='\n') as f:
        json.dump(doc, f)


def norm(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


P, N, T = [], [], []
for q in range(3):
    a = q * math.pi / 3 + .35
    side = (math.cos(a) * .5, 0.0, math.sin(a) * .5)
    face = norm((-side[2], 0.0, side[0]))
    n = norm((face[0] * .3, .7, face[2] * .3))
    lean = .24 if q % 2 == 0 else -.24
    p0 = (-side[0], 0.0, -side[2]); p1 = side
    p2 = (side[0] + face[0] * lean, .97, side[2] + face[2] * lean); p3 = (-side[0] + face[0] * lean, .97, -side[2] + face[2] * lean)
    for p, uv in ((p0, (0, 1)), (p1, (1, 1)), (p2, (1, 0)), (p0, (0, 1)), (p2, (1, 0)), (p3, (0, 0))):
        P.append(p); N.append(n); T.append(uv)
gltf('SM_WwPlantUpright', P, N, T)
P, N, T = [], [], []
for p, uv in (((-.5, 0, .5), (0, 1)), ((.5, 0, .5), (1, 1)), ((.5, 0, -.5), (1, 0)), ((-.5, 0, .5), (0, 1)), ((.5, 0, -.5), (1, 0)), ((-.5, 0, -.5), (0, 0))):
    P.append(p); N.append((0.0, 1.0, 0.0)); T.append(uv)
gltf('SM_WwPlantFlat', P, N, T)
print('Prepared the plant meshes')
