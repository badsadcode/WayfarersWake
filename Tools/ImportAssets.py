# Imports Thornwake's own art into the project (run with Tools/ImportAssets.cmd; safe to run again).
# The source is read, never written. Its folder comes from the environment variable WW_THORNWAKE.
#   /Game/Terrain/T_*           rock textures (pixel art: nearest filtering, as in Thornwake)
#   /Game/Trees/<Species>_L0    full tree model      (TREE_<Species>.glb)
#   /Game/Trees/<Species>_L2    distant tree model   (TREE_<Species>_LOD2.glb)
# See ASSET_MANIFEST.md for provenance.
import os
import unreal

SOURCE = os.environ.get('WW_THORNWAKE', '')
VEGETATION = os.path.join(SOURCE, 'Assets', 'Vegetation')
SPECIES = ['Pine_A', 'Pine_B', 'Pine_C', 'Spruce_A', 'Spruce_B', 'Oak_A', 'Oak_B', 'Oak_C', 'Birch_Green_A', 'Birch_Green_B',
           'Birch_Green_C', 'Maple_Autumn_A', 'Poplar_A', 'Willow_A', 'Sparse_A', 'Birch_Autumn_A', 'Birch_Autumn_B']
ROCKS = ['T_Cliff_Rock_128', 'T_Rock_Granite_128', 'T_Rock_Basalt_128', 'T_Rock_Sandstone_128', 'T_Rock_Mossy_128', 'T_Rock_Icy_128']

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def task(filename, destination):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = destination
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


def pixel_art(folder, group):
    """Nearest filtering on every texture under a folder (Thornwake's art is pixel art)."""
    for path in eal.list_assets(folder, recursive=True, include_folder=False):
        asset = eal.load_asset(path)
        if isinstance(asset, unreal.Texture2D):
            asset.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
            asset.set_editor_property('lod_group', group)
            eal.save_loaded_asset(asset)


if not os.path.isdir(VEGETATION):
    raise RuntimeError('Set WW_THORNWAKE to the Thornwake project folder (not found: %s)' % VEGETATION)

tasks = [task(os.path.join(VEGETATION, 'Placeholder', name + '.png'), '/Game/Terrain') for name in ROCKS]
for species in SPECIES:
    tasks.append(task(os.path.join(VEGETATION, 'TREE_%s.glb' % species), '/Game/Trees/%s_L0' % species))
    tasks.append(task(os.path.join(VEGETATION, 'TREE_%s_LOD2.glb' % species), '/Game/Trees/%s_L2' % species))
missing = [t.filename for t in tasks if not os.path.isfile(t.filename)]
if missing:
    raise RuntimeError('Missing source files: %s' % ', '.join(missing))
if os.environ.get('WW_SETTINGS_ONLY', '') != '1':  # (1: only re-apply the texture settings to what is imported)
    tools.import_asset_tasks(tasks)
# Rock tiles over whole mountainsides: it needs mipmaps (nearest-filtered, as in Thornwake) or it shimmers far away.
pixel_art('/Game/Terrain', unreal.TextureGroup.TEXTUREGROUP_WORLD)
pixel_art('/Game/Trees', unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
meshes = [p for p in eal.list_assets('/Game/Trees', recursive=True, include_folder=False) if isinstance(eal.load_asset(p), unreal.StaticMesh)]
unreal.log('WwAssets: imported %d rock textures and %d tree meshes' % (len(ROCKS), len(meshes)))
for path in meshes:
    unreal.log('WwAssets: mesh ' + path)

# The trees are drawn as instances. The importer hangs their materials on the engine's glTF materials, which are not
# compiled for instancing (the default grey would show in the game), so they move to two plain parents of our own:
# texture colour, rough, nearest-filtered by the texture's own setting; the masked one is two-sided with alpha cut-out.
mel = unreal.MaterialEditingLibrary
TEXTURE = 'BaseColorTexture'


def tree_parent(name, masked):
    full = '/Game/Trees/' + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mat = tools.create_asset(name, '/Game/Trees', unreal.Material, unreal.MaterialFactoryNew())
    sample = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    sample.set_editor_property('parameter_name', TEXTURE)
    mel.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    rough.set_editor_property('r', 0.9)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 400)
    spec.set_editor_property('r', 0.2)
    mel.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
    if masked:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property('two_sided', True)
        mel.connect_material_property(sample, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


opaque = tree_parent('M_WwTreeOpaque', False)
masked = tree_parent('M_WwTreeMasked', True)
count = 0
for path in eal.list_assets('/Game/Trees', recursive=True, include_folder=False):
    asset = eal.load_asset(path)
    if not isinstance(asset, unreal.MaterialInstanceConstant):
        continue
    parent = asset.get_editor_property('parent')
    parent_name = parent.get_name() if parent else ''
    if parent_name.startswith('M_WwTree'):
        continue  # (already moved by an earlier run)
    texture = None
    for name in mel.get_texture_parameter_names(asset):
        if str(name) == TEXTURE:
            texture = mel.get_material_instance_texture_parameter_value(asset, name)
    if texture is None:
        unreal.log_warning('WwAssets: %s has no %s (parameters: %s)' % (path, TEXTURE, [str(n) for n in mel.get_texture_parameter_names(asset)]))
        continue
    mel.set_material_instance_parent(asset, masked if 'Mask' in parent_name or 'Blend' in parent_name else opaque)
    mel.set_material_instance_texture_parameter_value(asset, TEXTURE, texture)
    mel.update_material_instance(asset)
    eal.save_loaded_asset(asset)
    count += 1
unreal.log('WwAssets: %d tree materials moved to M_WwTreeOpaque / M_WwTreeMasked' % count)
