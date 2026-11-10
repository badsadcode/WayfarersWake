"""Brings Tom's world texture sets (Art/Textures/<Set>: 16 ground_*, 6 stone_* and 6 cliff_* colour maps each) into the
project, each as three texture arrays the world's materials can be pointed at while the game runs:
/Game/Terrain/Sets/TA_Ground_<Set>, TA_Stone_<Set>, TA_Cliff_<Set>. The small pixel-art textures carried over from
Thornwake become the set "Pixel". The menu's View page chooses between the sets.
Run with Tools/ImportTextures.cmd. Safe to run again; a new folder under Art/Textures becomes a new set."""
import glob
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, '..', 'Art', 'Textures')
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
KINDS = (('Ground', 'ground_', 16), ('Stone', 'stone_', 6), ('Cliff', 'cliff_', 6))


def make_array(name, textures, smooth):
    path = '/Game/Terrain/Sets/' + name
    array = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, '/Game/Terrain/Sets', unreal.Texture2DArray, unreal.Texture2DArrayFactory())
    array.set_editor_property('source_textures', textures)
    array.set_editor_property('filter', unreal.TextureFilter.TF_DEFAULT if smooth else unreal.TextureFilter.TF_NEAREST)
    array.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eal.save_loaded_asset(array)
    return array


# The original pixel-art set, from the textures already in the project.
pixel = {
    'Ground': sorted(p.split('.')[0] for p in eal.list_assets('/Game/Terrain/Ground', recursive=False)),
    'Stone': ['/Game/Terrain/' + n for n in ('T_Cliff_Rock_128', 'T_Rock_Granite_128', 'T_Rock_Basalt_128', 'T_Rock_Sandstone_128', 'T_Rock_Mossy_128', 'T_Rock_Icy_128')],
    'Cliff': ['/Game/Terrain/Cliff/cliff_%s_tiled' % n for n in ('rock_tom', 'alpine_rock', 'blue_gray_slate', 'granite_gray', 'mossy_stone', 'warm_sandstone')],
}
template = {}
for kind, _, count in KINDS:
    textures = [eal.load_asset(p) for p in pixel[kind]]
    if len(textures) != count or any(t is None for t in textures):
        raise RuntimeError('WwTextures: the original %s textures are not all in the project (run Tools/ImportAssets.cmd)' % kind)
    template[kind] = textures[0]
    make_array('TA_%s_Pixel' % kind, textures, False)
unreal.log('WwTextures: set Pixel (the original pixel art)')
sets = ['Pixel']

for folder in sorted(os.listdir(ART)):
    root = os.path.join(ART, folder)
    if not os.path.isdir(root):
        continue
    found = {kind: sorted(glob.glob(os.path.join(root, '**', prefix + '*_albedo.png'), recursive=True)) for kind, prefix, _ in KINDS}
    if any(len(found[kind]) != count for kind, _, count in KINDS):
        unreal.log_warning('WwTextures: %s is not a full set (%s): skipped' % (folder, ', '.join('%d %s' % (len(found[k]), k) for k, _, _ in KINDS)))
        continue
    for kind, _, _ in KINDS:
        new = []
        for path in found[kind]:
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', path)
            task.set_editor_property('destination_path', '/Game/Terrain/Sets/%s' % folder)
            task.set_editor_property('automated', True)
            task.set_editor_property('save', True)
            task.set_editor_property('replace_existing', True)
            tools.import_asset_tasks([task])
            texture = eal.load_asset('/Game/Terrain/Sets/%s/%s' % (folder, os.path.splitext(os.path.basename(path))[0]))
            if texture is None:
                raise RuntimeError('WwTextures: could not import ' + path)
            # (the same kind of numbers as the layer it stands in for: the shaders expect that)
            for prop in ('srgb', 'compression_settings', 'mip_gen_settings'):
                texture.set_editor_property(prop, template[kind].get_editor_property(prop))
            texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
            texture.set_editor_property('filter', unreal.TextureFilter.TF_DEFAULT)
            eal.save_loaded_asset(texture)
            new.append(texture)
        make_array('TA_%s_%s' % (kind, folder), new, True)
    unreal.log('WwTextures: set %s (%d px)' % (folder, new[0].blueprint_get_size_x()))
    sets.append(folder)
# The sets' names for the game's menu (a packaged game cannot look for the files).
with open(os.path.join(HERE, '..', 'Content', 'Gameplay', 'texture_sets.txt'), 'w') as f:
    f.write('
'.join(sorted(sets)) + '
')
