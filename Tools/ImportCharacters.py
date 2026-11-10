"""Brings the rigged bodies (Art/Characters/*.fbx, made by Tools/RigCharacters.cmd) into the project: for each, a
skeletal mesh with its skeleton, its animations named A_<Body>_<Clip>, its texture and a material.
Run with Tools/ImportCharacters.cmd. Safe to run again: assets are replaced in place."""
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, '..', 'Art', 'Characters')
BODIES = ['HumanMale', 'HumanFemale', 'OrcMale', 'OrcFemale', 'DwarfMale', 'DwarfFemale']
CLIPS = ['Idle', 'Walk', 'Run', 'Swim', 'Attack', 'Crouch', 'Climb']
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_file(filename, folder, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', filename)
    task.set_editor_property('destination_path', folder)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    if options is not None:
        task.set_editor_property('options', options)
    tools.import_asset_tasks([task])
    return [str(p) for p in task.get_editor_property('imported_object_paths')]


for body in BODIES:
    fbx = os.path.join(SOURCE, body + '.fbx')
    if not os.path.exists(fbx):
        unreal.log_warning('WwCharacters: %s is missing (run Tools/RigCharacters.cmd)' % fbx)
        continue
    folder = '/Game/Characters/' + body
    options = unreal.FbxImportUI()
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_as_skeletal', True)
    options.set_editor_property('import_animations', True)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    paths = import_file(fbx, folder, options)
    unreal.log('WwCharacters: %s imported: %s' % (body, ', '.join(paths)))

    # The animations get plain names the game can ask for.
    mesh = None
    for path in eal.list_assets(folder, recursive=True):
        asset = eal.load_asset(path)
        if isinstance(asset, unreal.SkeletalMesh):
            mesh = asset
        if isinstance(asset, unreal.AnimSequence):
            name = asset.get_name()
            for clip in CLIPS:
                wanted = 'A_%s_%s' % (body, clip)
                if name != wanted and (name.endswith('_' + clip) or name.endswith(clip)) :
                    target = folder + '/' + wanted
                    if eal.does_asset_exist(target):
                        eal.delete_asset(target)
                    eal.rename_asset(path.split('.')[0], target)
    if mesh is None:
        unreal.log_error('WwCharacters: no skeletal mesh came in for ' + body)
        continue
    if mesh.get_name() != 'SK_' + body:
        target = folder + '/SK_' + body
        if eal.does_asset_exist(target):
            eal.delete_asset(target)
        eal.rename_asset(mesh.get_path_name().split('.')[0], target)
        mesh = eal.load_asset(target)

    # The texture and a plain matte material for it.
    png = os.path.join(SOURCE, body + '.png')
    texture = None
    if os.path.exists(png):
        import_file(png, folder)
        texture = eal.load_asset(folder + '/' + body)
        # (a colour picture: an earlier import of this file took it for a normal map and it kept those settings)
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property('srgb', True)
        eal.save_asset(folder + '/' + body)
    material_path = folder + '/M_' + body
    material = eal.load_asset(material_path) if eal.does_asset_exist(material_path) else tools.create_asset('M_' + body, folder, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(material)
    material.set_editor_property('used_with_skeletal_mesh', True)   # (without it the engine draws its grey default)
    if texture is not None:
        sample = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 0)
        sample.set_editor_property('texture', texture)
        sample.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        mel.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 300)
    rough.set_editor_property('r', 0.85)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    eal.save_asset(material_path)
    slots = mesh.get_editor_property('materials')
    if len(slots) > 0:
        slot = slots[0]
        slot.set_editor_property('material_interface', material)
        slots[0] = slot
        mesh.set_editor_property('materials', slots)
    eal.save_asset(mesh.get_path_name().split('.')[0])
    names = sorted(eal.load_asset(p).get_name() for p in eal.list_assets(folder, recursive=True))
    bounds = mesh.get_bounds()
    unreal.log('WwCharacters: %s: %s; height %.0f cm' % (body, ', '.join(names), bounds.box_extent.z * 2))
