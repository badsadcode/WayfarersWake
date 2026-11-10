"""Brings Tom's world textures (Art/Textures) into the project and puts their colour maps into the three texture
arrays the world's materials read: TA_WwGround (16 ground layers), TA_WwRock (6 stone looks), TA_WwCliff (6 cliff
faces), in place of the small pixel-art ones carried over from Thornwake. The arrays keep their names, so nothing else
changes; the new textures take each array's old settings except that they are filtered smoothly, not blockily.
Run with Tools/ImportTextures.cmd, then Tools/CreateAssets.cmd (the ground material holds each layer's mean colour)."""
import glob
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, '..', 'Art', 'Textures')
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

for folder, array_name in (('Ground', 'TA_WwGround'), ('Stone', 'TA_WwRock'), ('Cliff', 'TA_WwCliff')):
    array = eal.load_asset('/Game/Terrain/' + array_name)
    if array is None:
        raise RuntimeError('WwTextures: /Game/Terrain/%s missing: run Tools/ImportAssets.cmd first' % array_name)
    old = list(array.get_editor_property('source_textures'))
    files = sorted(glob.glob(os.path.join(ART, folder, '*_albedo.png')))
    if len(files) != len(old):
        raise RuntimeError('WwTextures: %s has %d albedo textures, the array %d layers' % (folder, len(files), len(old)))
    new = []
    for index, path in enumerate(files):
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', path)
        task.set_editor_property('destination_path', '/Game/Terrain/Noor2/' + folder)
        task.set_editor_property('automated', True)
        task.set_editor_property('save', True)
        task.set_editor_property('replace_existing', True)
        tools.import_asset_tasks([task])
        name = os.path.splitext(os.path.basename(path))[0]
        texture = eal.load_asset('/Game/Terrain/Noor2/%s/%s' % (folder, name))
        if texture is None:
            raise RuntimeError('WwTextures: could not import ' + path)
        # (as the layer it replaces was set up: the shaders expect the same kind of numbers from it)
        for prop in ('srgb', 'compression_settings', 'lod_group', 'mip_gen_settings'):
            texture.set_editor_property(prop, old[index].get_editor_property(prop))
        texture.set_editor_property('filter', unreal.TextureFilter.TF_DEFAULT)
        eal.save_loaded_asset(texture)
        new.append(texture)
    array.set_editor_property('source_textures', new)
    array.set_editor_property('filter', unreal.TextureFilter.TF_DEFAULT)
    eal.save_loaded_asset(array)
    unreal.log('WwTextures: %s now holds %d textures from Art/Textures/%s (%d px)' % (array_name, len(new), folder, new[0].blueprint_get_size_x()))
